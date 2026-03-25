# web_server

## Overview

The `web_server` component provides an embedded HTTP server for the ESP32-S3 RF PA sequencer. It exposes a JSON REST API for controlling and monitoring the device, a WebSocket endpoint that pushes live state at 2 Hz, and a static file server backed by a SPIFFS partition. Built on `esp_http_server`, it runs on port 80 and is intended to be the primary remote interface for the sequencer alongside the serial CLI.

The component is a pure consumer of the system: it reads shared state from the `system_state` blackboard, reads and writes the `app_config_t` configuration struct owned by `main`, and delegates all real hardware actions to the relevant subsystem APIs (`sequencer`, `relays`, `monitor`, `wifi_sta`, `ota`).

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

### Configuration pointer

`web_server_init(app_config_t *cfg)` receives a pointer to the live configuration struct owned by `main`. This pointer is stored in a file-scoped static (`s_cfg`) and exposed to all API handlers via `web_get_config()`. Handlers read and write config fields in-place. Changes do not take effect on the sequencer until `POST /api/seq/apply` is called, and do not persist to NVS until `POST /api/config/save`.

### JSON response envelope

All API endpoints return responses in a consistent envelope:

```json
{ "ok": true, "data": { ... } }       // success
{ "ok": false, "error": "message" }    // error
```

This is enforced by the `web_json_ok()` and `web_json_error()` helpers in `web_json.c`. The helpers take ownership of any `cJSON*` data object passed to them.

### Request body parsing

`web_parse_body()` reads the full request body (maximum 2048 bytes), parses it as JSON, and returns a `cJSON*` that the caller must free. On failure it automatically sends a 400 error response and returns `NULL`.

### State snapshot (via system_state)

The `web_build_state_json()` function (in `api_state.c`) takes an atomic snapshot of `system_state_t` via `system_state_get()` and serializes it to JSON. This same function is reused by the WebSocket push task, ensuring REST and WebSocket clients see identical state shapes:

```json
{
  "ptt": false,
  "seq_state": "RX",
  "seq_fault": "none",
  "relays": [false, false, false, false, false, false],
  "relay_names": ["", "PA", "", "", "", ""],
  "fwd_w": 0.0,
  "ref_w": 0.0,
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
| GET | `/api/config` | -- | Full configuration dump (thresholds, calibration, thermistor params, sequences, relay names) |
| POST | `/api/config` | `{"key":"swr_threshold","value":3.0}` | Set a single config key. Valid keys: `swr_threshold`, `temp1_threshold`, `temp2_threshold`, `pa_relay`, `fwd_cal`, `ref_cal`, `therm_beta`, `therm_r0`, `therm_rseries` |
| POST | `/api/config/save` | -- | Persist current config to NVS |
| POST | `/api/config/defaults` | -- | Reset in-memory config to factory defaults (does not save) |

### Relay Control

| Method | URI | Body | Description |
|---|---|---|---|
| POST | `/api/relay` | `{"id":2,"on":true}` | Set a single relay on/off. `id` is 1-indexed (1--6) |
| POST | `/api/relay/name` | `{"id":2,"name":"PA"}` | Set or clear (omit `name` or null) a relay's display name |

### Fault Management

| Method | URI | Body | Description |
|---|---|---|---|
| POST | `/api/fault/clear` | -- | Clear active fault and return to RX. Returns 409 if not in FAULT state |
| POST | `/api/fault/inject` | `{"type":"swr"}` | Inject a fault for testing. Types: `swr`, `temp1`, `temp2`, `emergency` |

### Sequencing

| Method | URI | Body | Description |
|---|---|---|---|
| POST | `/api/seq` | `{"direction":"tx","steps":[{"relay_id":1,"state":true,"delay_ms":50},...]}`| Replace the TX or RX step sequence in the in-memory config. 1--8 steps allowed |
| POST | `/api/seq/apply` | -- | Push in-memory config to the sequencer and monitor. Returns 409 if not in RX state |

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

A dedicated FreeRTOS task (`ws_push`) broadcasts the same JSON payload as `GET /api/state` to all connected WebSocket clients every 500 ms. The connection is server-push only; incoming frames from clients are read and discarded.

Key implementation details:

- **Client limit:** `WS_MAX_CLIENTS` is set to 3. Additional connections are silently rejected.
- **Thread safety:** A FreeRTOS mutex protects the client file descriptor array. The push task holds the lock only during the send loop.
- **Cleanup:** The httpd `close_fn` callback (`ws_close_fd`) removes disconnected sockets from the tracking array, regardless of whether the close was initiated by the client, the server, or a network error. This replaces the default httpd close behavior (it calls `close(fd)` itself after removing the client).
- **No LRU purge:** `lru_purge_enable` is set to `false` because the default LRU eviction can close WebSocket sockets mid-push, breaking active connections.

## Architecture Decisions

- **Flat API file split:** Each API domain (state, config, relay, fault, etc.) lives in its own `api_*.c` file rather than a monolithic handler. This keeps individual files short and makes it easy to add new endpoint groups without touching existing code. All files link against the same `web_json.h` helpers.

- **Config modify-then-apply pattern:** The web API mirrors the CLI's edit/apply/save workflow. `POST /api/config` and `POST /api/seq` modify the in-memory `app_config_t` but do not activate changes on the sequencer. `POST /api/seq/apply` pushes the config live, and `POST /api/config/save` persists to NVS. This prevents partial configuration from being applied mid-edit.

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
- `config` -- `app_config_t` struct, `config_save()`, `config_defaults()`, `config_set_by_key()`
- `system_state` -- `system_state_get()` for atomic state snapshots (blackboard reader)
- `sequencer` -- `sequencer_clear_fault()`, `sequencer_update_config()`, `sequencer_get_event_queue()`, state/fault enums
- `monitor` -- `monitor_update_config()`, `monitor_read_channel()`
- `ads1115` -- ADC channel type enum
- `relays` -- `relay_set()`
- `wifi_sta` -- Full WiFi lifecycle (connect, disconnect, scan, credentials, enable)
- `ota` -- OTA status, repo management, update, rollback, validate
- `hw_config` -- `HW_RELAY_COUNT` constant (6)

## Usage Notes

- **Initialization order:** `web_server_init()` must be called after `app_wifi_init()` since the server needs the network stack to be ready.

- **Config pointer lifetime:** The `app_config_t*` passed to `web_server_init()` must outlive the server. The web server does not copy the struct; all handlers read and write through the pointer.

- **No authentication:** The API has no authentication or authorization. It is designed for use on a trusted local network.

- **Body size limit:** All POST endpoints reject request bodies larger than 2048 bytes.

- **Relay IDs are 1-indexed:** Consistent with the rest of the project (matching schematic labels), relay IDs in the API are 1--6, not 0--5.

- **Fault injection:** `POST /api/fault/inject` posts events directly to the sequencer's event queue. The `emergency` type sends `SEQ_EVENT_EMERGENCY_PA_OFF` (which immediately de-energises the PA relay), while other types send `SEQ_EVENT_FAULT`.

- **WebSocket reconnection:** Clients should implement reconnection logic. If all 3 WebSocket slots are occupied, new connections are silently dropped. Failed sends also remove clients from the tracking list.

- **SPIFFS partition:** Static files are served from a SPIFFS partition labeled `"storage"`, mounted at `/www`. The partition is formatted if mount fails. Maximum 5 files can be open simultaneously.
