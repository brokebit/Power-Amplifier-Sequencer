# web_server

## Overview

The `web_server` component provides an embedded HTTP server for the ESP32-S3 RF PA sequencer. It exposes a JSON REST API for controlling and monitoring the device, a WebSocket endpoint that pushes live state at 2 Hz, and a static file server backed by a SPIFFS partition. Built on `esp_http_server`, it runs on port 80 and is intended to be the primary remote interface for the sequencer alongside the serial CLI.

The component is a pure consumer of the system: it reads shared state from the `system_state` blackboard, reads configuration via `config_snapshot()`, writes configuration through the `config` service functions (`config_set_by_key()`, `config_set_sequence()`, `config_set_relay_name()`), and delegates all real hardware actions to the relevant subsystem APIs (`sequencer`, `relays`, `monitor`, `wifi_sta`, `ota`).

## Source File Layout

| File | Purpose |
|---|---|
| `web_server.c` | Server lifecycle (init, stop), SPIFFS mount, URI registration |
| `web_json.c` / `web_json.h` | JSON response envelope helpers and request body parser |
| `web_ws.c` / `web_ws.h` | WebSocket client tracking, push task, close callback |
| `api_state.c` | `GET /api/state`, `GET /api/version` |
| `api_config.c` | `GET /api/config`, `POST /api/config`, save, defaults |
| `api_relay.c` | `POST /api/relay`, `POST /api/relay/name` |
| `api_fault.c` | `POST /api/fault/clear`, `POST /api/fault/inject` |
| `api_seq.c` | `POST /api/seq`, `POST /api/seq/apply` |
| `api_adc.c` | `GET /api/adc` |
| `api_wifi.c` | WiFi status, config, connect/disconnect, scan, auto, erase |
| `api_ota.c` | OTA status, repo, update, rollback, validate |
| `api_system.c` | `POST /api/reboot`, `POST /api/log` |
| `api_static.c` | Wildcard catch-all serving files from `/www` on SPIFFS |

Each `api_*.c` file follows the same pattern: static handler functions plus a `web_register_api_*()` function called from `web_server_init()`.

## Key Data Structures

### Configuration access

`web_server_init()` takes no parameters. There is no shared config pointer or accessor function. All API handlers access configuration through the `config` component's service API:

- **Reads:** `config_snapshot(&snap)` copies the current draft config under the config mutex. Every handler that needs config data (state, config dump, sequences) takes a snapshot rather than holding a raw pointer.
- **Scalar writes:** `config_set_by_key(key, value, err_msg, err_len)` validates and sets a single field by name.
- **Sequence writes:** `config_set_sequence(is_tx, steps, count, err_msg, err_len)` replaces an entire TX or RX sequence.
- **Relay name writes:** `config_set_relay_name(relay_id, name, err_msg, err_len)` sets or clears a relay alias.
- **Apply:** `config_apply()` pushes the draft to all live consumers (sequencer, monitor) via registered callbacks.
- **Save:** `config_save()` persists the draft to NVS.
- **Defaults:** `config_defaults()` resets the draft to factory defaults without touching NVS.

Changes do not take effect on the sequencer until `POST /api/seq/apply` is called (which invokes `config_apply()`), and do not persist to NVS until `POST /api/config/save`.

### JSON response envelope

All API endpoints return responses in a consistent envelope:

```json
{ "ok": true, "data": { ... } }       // success
{ "ok": false, "error": "message" }    // error
```

This is enforced by the `web_json_ok()` and `web_json_error()` helpers in `web_json.c`. The helpers take ownership of any `cJSON*` data object passed to them.

### Request body parsing

`web_parse_body()` reads the full request body (maximum 2048 bytes), parses it as JSON, and returns a `cJSON*` that the caller must free. On failure it automatically sends a 400 error response and returns `NULL`.

### State snapshot (via system_state and config_snapshot)

The `web_build_state_json()` function (in `api_state.c`) takes an atomic snapshot of `system_state_t` via `system_state_get()` and a config snapshot via `config_snapshot()` (for relay names), then serializes both to JSON. This same function is reused by the WebSocket push task, ensuring REST and WebSocket clients see identical state shapes:

```json
{
  "ptt": false,
  "seq_state": "RX",
  "seq_fault": "none",
  "relays": [false, false, false, false, false, false],
  "relay_names": ["", "PA", "", "", "", ""],
  "fwd_w": 0.0,
  "ref_w": 0.0,
  "fwd_dbm": -999.0,
  "ref_dbm": -999.0,
  "swr": 1.0,
  "temp1_c": 25.3,
  "temp2_c": 24.8,
  "wifi": { "connected": true, "ip": "192.168.1.42", "rssi": -55 }
}
```

## REST API Reference

### State & Info

| Method | URI | Description |
|---|---|---|
| GET | `/api/state` | Full device state snapshot (PTT, sequencer state/fault, relays, sensors, WiFi) |
| GET | `/api/version` | Firmware project name, version, IDF version, core count |

### Configuration

| Method | URI | Body | Description |
|---|---|---|---|
| GET | `/api/config` | -- | Full configuration dump via `config_snapshot()` (thresholds, calibration, thermistor params, sequences, relay names, `pending_apply` flag) |
| POST | `/api/config` | `{"key":"swr_threshold","value":3.0}` | Set a single config key via `config_set_by_key()`. Valid keys: `swr_threshold`, `temp1_threshold`, `temp2_threshold`, `pa_relay`, `fwd_slope`, `fwd_intercept`, `fwd_coupling`, `fwd_atten`, `ref_slope`, `ref_intercept`, `ref_coupling`, `ref_atten`, `adc_r_top`, `adc_r_bottom`, `therm_beta`, `therm_r0`, `therm_rseries` |
| POST | `/api/config/save` | -- | Persist draft config to NVS via `config_save()` |
| POST | `/api/config/defaults` | -- | Reset draft config to factory defaults via `config_defaults()` (does not save) |

### Relay Control

| Method | URI | Body | Description |
|---|---|---|---|
| POST | `/api/relay` | `{"id":2,"on":true}` | Set a single relay on/off. `id` is 1-indexed (1--6) |
| POST | `/api/relay/name` | `{"id":2,"name":"PA"}` | Set or clear (omit `name` or null) a relay's display name via `config_set_relay_name()` |

### Fault Management

| Method | URI | Body | Description |
|---|---|---|---|
| POST | `/api/fault/clear` | -- | Clear active fault and return to RX. Returns 409 if not in FAULT state |
| POST | `/api/fault/inject` | `{"type":"swr"}` | Inject a fault for testing via `sequencer_inject_fault()`. Types: `swr`, `temp1`, `temp2`, `emergency` |

### Sequencing

| Method | URI | Body | Description |
|---|---|---|---|
| POST | `/api/seq` | `{"direction":"tx","steps":[{"relay_id":1,"state":true,"delay_ms":50},...]}`| Replace the TX or RX step sequence in the draft config via `config_set_sequence()`. 1--8 steps allowed |
| POST | `/api/seq/apply` | -- | Push draft config to all live consumers via `config_apply()`. Returns 409 if not in RX state or PTT is active |

### ADC / Sensors

| Method | URI | Description |
|---|---|---|
| GET | `/api/adc` | Read all 4 ADC channels (fwd_power, ref_power, temp1, temp2) with raw voltages |
| GET | `/api/adc?ch=N` | Read a single channel (0--3) |

### WiFi

| Method | URI | Body | Description |
|---|---|---|---|
| GET | `/api/wifi/status` | -- | Connection status, auto-connect flag, IP, RSSI |
| POST | `/api/wifi/config` | `{"ssid":"...","password":"..."}` | Set WiFi credentials (password optional for open networks) |
| POST | `/api/wifi/connect` | -- | Initiate connection using saved credentials. 404 if no credentials saved |
| POST | `/api/wifi/disconnect` | -- | Disconnect from WiFi |
| GET | `/api/wifi/scan` | -- | Trigger scan and return list of networks (ssid, rssi, channel, authmode) |
| POST | `/api/wifi/auto` | `{"enabled":true}` | Enable or disable auto-connect on boot |
| POST | `/api/wifi/erase` | -- | Erase stored WiFi credentials from NVS |

### OTA

| Method | URI | Body | Description |
|---|---|---|---|
| GET | `/api/ota/status` | -- | Running/boot/next partitions, app state, current and other version |
| GET | `/api/ota/repo` | -- | Get configured GitHub repo (`owner/repo`) |
| POST | `/api/ota/repo` | `{"repo":"owner/repo"}` | Set the OTA source repo |
| POST | `/api/ota/update` | `{"target":"v1.2.3"}` | Start OTA update in background task. Reboots on success |
| POST | `/api/ota/rollback` | -- | Roll back to previous partition and reboot |
| POST | `/api/ota/validate` | -- | Mark current firmware as valid (prevents automatic rollback) |

### System

| Method | URI | Body | Description |
|---|---|---|---|
| POST | `/api/reboot` | -- | Responds then reboots after a 2-second delay |
| POST | `/api/log` | `{"level":"debug","tag":"web_ws"}` | Set ESP log level. Levels: `off`, `error`, `warn`, `info`, `debug`, `verbose`. Optional `tag` (default `"*"` for all) |

### Static Files

| Method | URI | Description |
|---|---|---|
| GET | `/*` | Wildcard catch-all serving files from the `/www` SPIFFS mount. `/` maps to `/www/index.html` |

## WebSocket (Live State Push)

**URI:** `ws://<device-ip>/ws`

A dedicated FreeRTOS task (`ws_push`) broadcasts the same JSON payload as `GET /api/state` to all connected WebSocket clients every 250 ms. The connection is server-push only; incoming frames from clients are read and discarded.

Key implementation details:

- **Client limit:** `WS_MAX_CLIENTS` is set to 3. Additional connections are silently rejected.
- **Thread safety:** A FreeRTOS mutex protects the client file descriptor array. The push task holds the lock only during the send loop.
- **Cleanup:** The httpd `close_fn` callback (`ws_close_fd`) removes disconnected sockets from the tracking array, regardless of whether the close was initiated by the client, the server, or a network error. This replaces the default httpd close behavior (it calls `close(fd)` itself after removing the client).
- **No LRU purge:** `lru_purge_enable` is set to `false` because the default LRU eviction can close WebSocket sockets mid-push, breaking active connections.

## Architecture Decisions

- **Flat API file split:** Each API domain (state, config, relay, fault, etc.) lives in its own `api_*.c` file rather than a monolithic handler. This keeps individual files short and makes it easy to add new endpoint groups without touching existing code. All files link against the same `web_json.h` helpers.

- **Config modify-then-apply pattern:** The web API mirrors the CLI's edit/apply/save workflow. `POST /api/config` and `POST /api/seq` modify the draft `app_config_t` through the `config` service functions but do not activate changes on the sequencer. `POST /api/seq/apply` calls `config_apply()` to push the draft to all live consumers (sequencer, monitor) via registered callbacks. `POST /api/config/save` persists to NVS. The `GET /api/config` response includes a `pending_apply` boolean (via `config_pending_apply()`) so the UI can indicate when unapplied edits exist. This prevents partial configuration from being applied mid-edit.

- **No shared config pointer:** Unlike earlier versions, the web server does not hold or expose a raw `app_config_t*`. All reads go through `config_snapshot()` (locked copy) and all writes go through validated service functions (`config_set_by_key()`, `config_set_sequence()`, `config_set_relay_name()`). This eliminates the previous coupling where handlers could mutate the config struct without validation or locking.

- **Shared state builder for REST and WebSocket:** `web_build_state_json()` is defined in `api_state.c` and forward-declared in `web_ws.c`. This ensures both the polling REST endpoint and the push WebSocket send identical JSON structures without code duplication.

- **SPIFFS failure is non-fatal:** If SPIFFS fails to mount (missing partition, corrupt filesystem), the server still starts and all API endpoints remain available. The static file handler will return 404 for all files, but the API is fully functional. This is intentional -- the REST API is the primary interface; the web UI is a convenience.

- **OTA runs in a background task:** `POST /api/ota/update` spawns a dedicated FreeRTOS task (8 KB stack) so the HTTP response can be sent immediately. The OTA task reboots the device on success; on failure, it frees its resources and deletes itself.

- **Reboot delay:** `POST /api/reboot` responds to the client first, then delays 2 seconds before calling `esp_restart()` in a background task. This ensures the HTTP response reaches the client.

- **Socket budget:** The server is configured with `max_open_sockets = 7` (3 WS + 4 HTTP). Combined with LRU purge being disabled, this prevents HTTP requests from evicting active WebSocket connections.

- **Static route is registered last:** The `/*` wildcard catch-all is registered after all `/api/*` routes so that the httpd URI matcher finds specific routes first.

- **Path traversal protection:** The static file handler rejects any URI containing `..` with a 403 response.

## Dependencies

### ESP-IDF Components
- `esp_http_server` -- HTTP server with WebSocket support
- `spiffs` -- Flash filesystem for static web assets
- `json` (cJSON) -- JSON serialization and parsing
- `esp_app_format` -- Firmware version/description access
- `esp_wifi` -- WiFi status queries (private dependency)

### Internal Project Components
- `config` -- `app_config_t` struct, `config_snapshot()`, `config_save()`, `config_defaults()`, `config_set_by_key()`, `config_set_sequence()`, `config_set_relay_name()`, `config_apply()`, `config_pending_apply()`
- `system_state` -- `system_state_get()` for atomic state snapshots (blackboard reader)
- `sequencer` -- `sequencer_clear_fault()`, `sequencer_inject_fault()`, `seq_state_name()`, `seq_fault_name()`, `seq_fault_parse()`, state/fault enums
- `monitor` -- `monitor_read_channel()`
- `ads1115` -- ADC channel type enum
- `relays` -- `relay_set()`
- `wifi_sta` -- Full WiFi lifecycle (connect, disconnect, scan, credentials, enable)
- `ota` -- OTA status, repo management, update, rollback, validate
- `hw_config` -- `HW_RELAY_COUNT` constant (6)

## Usage Notes

- **Initialization order:** `web_server_init()` must be called after `config_init()` and `app_wifi_init()` since the server needs the config subsystem and network stack to be ready.

- **No authentication:** The API has no authentication or authorization. It is designed for use on a trusted local network.

- **Body size limit:** All POST endpoints reject request bodies larger than 2048 bytes.

- **Relay IDs are 1-indexed:** Consistent with the rest of the project (matching schematic labels), relay IDs in the API are 1--6, not 0--5.

- **Fault injection:** `POST /api/fault/inject` calls `sequencer_inject_fault()` with a parsed `seq_fault_t` value. The fault type string is parsed by `seq_fault_parse()`, which accepts `swr`, `temp1`, `temp2`, or `emergency`.

- **WebSocket reconnection:** Clients should implement reconnection logic. If all 3 WebSocket slots are occupied, new connections are silently dropped. Failed sends also remove clients from the tracking list.

- **SPIFFS partition:** Static files are served from a SPIFFS partition labeled `"storage"`, mounted at `/www`. The partition is formatted if mount fails. Maximum 5 files can be open simultaneously.
