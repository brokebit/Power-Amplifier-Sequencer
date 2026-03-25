# wifi_sta

## Overview

The `wifi_sta` component manages WiFi Station (STA) mode for the ESP32-S3 sequencer. It handles connecting to an access point, persisting credentials and enable/disable state in NVS, automatic reconnection with exponential backoff, network scanning, and publishing connection status to the shared `system_state` blackboard. The WiFi driver is always started in STA mode at init time; the `enabled` flag controls only whether an auto-connect attempt is made.

## Key Data Structures

### Public Types

| Type | Description |
|---|---|
| `wifi_scan_result_t` | Lightweight scan result struct: `ssid[33]`, `rssi` (int8), `channel` (uint8), `authmode` (uint8, cast from `wifi_auth_mode_t`). Returned from `app_wifi_scan_results()`; caller owns the allocation and must `free()` it. |

### NVS Schema

All WiFi state is persisted under the NVS namespace `"wifi_cfg"`:

| Key | Type | Description |
|---|---|---|
| `ssid` | string | AP SSID, max 32 characters |
| `pass` | string | AP password, max 64 characters (empty string for open networks) |
| `enabled` | uint8 | Auto-connect toggle: 1 = enabled (default), 0 = disabled |

### Internal State (module-static)

| Variable | Type | Purpose |
|---|---|---|
| `s_netif` | `esp_netif_t *` | The STA network interface handle |
| `s_wifi_eg` | `EventGroupHandle_t` | FreeRTOS event group; `CONNECTED_BIT` (BIT0) is set when an IP is obtained |
| `s_retry_timer` | `TimerHandle_t` | One-shot FreeRTOS software timer driving reconnect backoff |
| `s_retry_ms` | `uint32_t` | Current backoff interval, starts at 1 s, doubles up to 30 s |
| `s_connecting` | `bool` | When true, disconnection events trigger automatic reconnect |
| `s_ip_addr` | `uint32_t` | Cached IP address in network byte order; cleared on disconnect |

## Public API

All public symbols use the `app_wifi_` prefix to avoid collision with ESP-IDF's internal `wifi_sta_*` symbols.

| Function | Description |
|---|---|
| `app_wifi_init()` | Initializes the WiFi subsystem (event loop, netif, driver, retry timer). If enabled and credentials exist, auto-connects. Must be called after `nvs_flash_init()`. |
| `app_wifi_set_credentials(ssid, pass)` | Saves SSID/password to NVS. Does **not** connect -- call `app_wifi_connect()` separately. |
| `app_wifi_connect()` | Loads credentials from NVS and begins connection. Returns `ESP_ERR_NOT_FOUND` if no credentials are saved. |
| `app_wifi_disconnect()` | Disconnects from the AP, stops retry timer, clears `s_connecting`. Does **not** erase credentials. |
| `app_wifi_erase_credentials()` | Disconnects and removes SSID/password from NVS. |
| `app_wifi_is_connected()` | Returns `true` if the `CONNECTED_BIT` is set (i.e., an IP has been obtained). |
| `app_wifi_get_ip_str(buf, len)` | Writes the current IP as a dotted-quad string. Returns `ESP_ERR_INVALID_STATE` if not connected. |
| `app_wifi_get_rssi(rssi)` | Queries the driver for live RSSI. Returns `ESP_ERR_INVALID_STATE` if not connected. |
| `app_wifi_scan()` | Blocking active scan; prints a formatted table to stdout. Intended for CLI use. |
| `app_wifi_scan_results(results, count)` | Blocking active scan returning a heap-allocated array of `wifi_scan_result_t`. Caller must `free(*results)`. |
| `app_wifi_set_enabled(enabled)` | Persists the auto-connect flag to NVS. |
| `app_wifi_get_enabled()` | Reads the auto-connect flag from NVS. Defaults to `true` if the key has never been written. |

## Event Flow

### Initialization Sequence

1. `app_wifi_init()` creates the FreeRTOS event group and retry timer.
2. The default event loop and netif are initialized; a default STA netif is created.
3. The WiFi driver is initialized and set to `WIFI_MODE_STA`.
4. Event handlers are registered for all `WIFI_EVENT` IDs and `IP_EVENT_STA_GOT_IP`.
5. If `app_wifi_get_enabled()` is true and NVS contains credentials, the STA config is applied and `s_connecting` is set, causing `WIFI_EVENT_STA_START` to trigger `esp_wifi_connect()`.
6. The WiFi driver is always started via `esp_wifi_start()` regardless of whether auto-connect is enabled, so the driver is available for on-demand `connect` or `scan` calls.

### Connection State Machine (event-driven)

```
  WIFI_EVENT_STA_START
     |  (if s_connecting)
     v
  esp_wifi_connect()
     |
     +--- WIFI_EVENT_STA_CONNECTED ---> reset backoff to 1 s
     |
     +--- IP_EVENT_STA_GOT_IP -------> cache IP, read RSSI,
     |                                  publish to system_state,
     |                                  set CONNECTED_BIT
     |
     +--- WIFI_EVENT_STA_DISCONNECTED -> clear IP, clear CONNECTED_BIT,
                                          publish disconnected to system_state,
                                          if s_connecting: start retry timer
                                             |
                                             v
                                          retry_timer_cb fires
                                             -> esp_wifi_connect()
                                             -> double backoff (1s -> 2s -> ... -> 30s max)
```

### Reconnect Backoff

On disconnection (while `s_connecting` is true), a one-shot FreeRTOS timer is started with the current backoff interval. After each retry the interval doubles: 1 s, 2 s, 4 s, 8 s, 16 s, 30 s (capped). The backoff resets to 1 s on successful association (`WIFI_EVENT_STA_CONNECTED`) or when `app_wifi_connect()` is called explicitly.

### Disconnect / Erase

Calling `app_wifi_disconnect()` sets `s_connecting = false` and stops the retry timer, preventing automatic reconnection. `app_wifi_erase_credentials()` calls disconnect first, then removes keys from NVS.

## Architecture Decisions

- **`app_wifi_` prefix**: ESP-IDF reserves the `wifi_sta_` symbol namespace internally. All public symbols use `app_wifi_` to avoid linker collisions.

- **Credential storage separated from connection**: `set_credentials()` only persists to NVS; it does not connect. This two-step design lets the CLI and web API decouple configuration from activation, matching the project's broader edit/apply/save pattern.

- **Auto-connect defaults to enabled**: `app_wifi_get_enabled()` returns `true` when the NVS key has never been written. This means a device with saved credentials will auto-connect on first boot without explicit configuration of the enable flag.

- **Driver always started**: `esp_wifi_start()` is called unconditionally in `app_wifi_init()`, even when auto-connect is disabled or no credentials exist. This allows scan and on-demand connect to work without additional driver lifecycle management.

- **system_state as the sole status sink**: Connection state (connected flag, IP, RSSI) is published to the `system_state` blackboard via `system_state_set_wifi()`. Consumers (CLI status display, web API status endpoint, display) read from `system_state` rather than polling the WiFi driver directly. The component also maintains a local `CONNECTED_BIT` event group for synchronous `app_wifi_is_connected()` queries from the API layer.

- **Blocking scan by design**: Both scan functions block the calling task. This is acceptable because scans are user-initiated (CLI command or HTTP request) and take only a few seconds. No background scan task is needed.

- **FreeRTOS timer for retry backoff**: Using a software timer instead of a dedicated task or `vTaskDelay()` loop keeps the reconnect logic lightweight and avoids blocking any task during the backoff wait.

## Dependencies

### ESP-IDF Components (CMake REQUIRES)

| Component | Usage |
|---|---|
| `esp_wifi` | WiFi driver: connect, disconnect, scan, STA config |
| `esp_event` | Default event loop for WiFi and IP events |
| `esp_netif` | Network interface abstraction (default STA netif) |
| `nvs_flash` | Credential and enable-flag persistence |
| `system_state` | Shared blackboard for publishing connection status |

### FreeRTOS Primitives

- `EventGroupHandle_t` -- signaling connected/disconnected state to synchronous callers
- `TimerHandle_t` -- one-shot software timer for reconnect backoff

## Consumers

The component is called from three places:

- **`main.c`**: Calls `app_wifi_init()` during startup.
- **`cli/cmd_wifi.c`**: Registers the `wifi` CLI command with subcommands `status`, `config`, `connect`, `disconnect`, `scan`, `enable`, `disable`, `erase`.
- **`web_server/api_wifi.c`**: Exposes REST endpoints at `/api/wifi/{status,config,connect,disconnect,scan,auto,erase}`.
- **`ota/ota.c`**: Gates OTA updates on `app_wifi_is_connected()`.

## Usage Notes

- `app_wifi_init()` must be called after `nvs_flash_init()` (which happens inside `config_init()` in this project). Calling it earlier will fail to read credentials.
- The `app_wifi_scan_results()` caller **must** free the returned array. Forgetting to do so leaks heap memory proportional to the number of visible APs.
- `app_wifi_get_rssi()` queries the driver live each call; it is not a cached value. Avoid calling it in tight loops.
- Setting credentials does not trigger a connection. The intended workflow is: `set_credentials()` then `connect()`.
- Disabling WiFi via `app_wifi_set_enabled(false)` only affects the next boot's auto-connect behavior. To also disconnect immediately, call `app_wifi_disconnect()` (the CLI `disable` subcommand does both).
