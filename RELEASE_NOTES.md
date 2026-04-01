# Release Notes

## v1.2.1
### ESP IDF Upgrade

Upgraded to ESP IDF 5.5.3

### Power Calibration Refactor: Log-Linear Detector Support

Replaced the square-law power calibration model (`P = cal_factor x V^2`) with a log-linear detector model supporting slope, intercept, coupler coupling factor, and attenuator offset per channel. This supports AD8307, AD8318, K9KJ dual RF head, and similar log-linear power detectors used in amateur radio.

**New config parameters** (per channel, FWD and REF independently):

| Key | Description | Default |
|-----|-------------|---------|
| `fwd_slope` / `ref_slope` | Detector slope (mV/dB) | -25.0 |
| `fwd_intercept` / `ref_intercept` | Detector intercept (dBm at 0V) | 0.0 |
| `fwd_coupling` / `ref_coupling` | Directional coupler factor (dB, negative) | 0.0 |
| `fwd_atten` / `ref_atten` | Inline attenuator (dB) | 0.0 |
| `adc_r_top` | ADC input divider top resistor (ohms) | 10000 |
| `adc_r_bottom` | ADC input divider bottom resistor (ohms) | 15000 |

**Removed config parameters:** `fwd_cal`, `ref_cal` (the old square-law calibration factors).

**dBm support:** Power readings are now computed in dBm first, then converted to watts. Both units are exposed in the REST API (`fwd_dbm`, `ref_dbm`), CLI status/monitor output, and the web dashboard.

**Conversion formula:**

```
V_det_mV   = (V_adc / divider_ratio) * 1000
dBm_det    = (V_det_mV / slope) + intercept
dBm_actual = dBm_det + attenuator + abs(coupling)
P_watts    = 10^((dBm_actual - 30) / 10)
```

**NVS config reset:** The `app_config_t` struct layout has changed. On first boot after upgrading, the firmware detects the size mismatch and resets all configuration to factory defaults. Users must re-enter their settings (sequences, thresholds, relay names, and the new power calibration values).

### Bug Fixes

**HTTP: API responses hold sockets open, exhausting pool under sustained requests**

API endpoints (`/api/state`, `/api/config`, etc.) returned responses without a `Connection: close` header, defaulting to HTTP/1.1 keep-alive. Under sustained API traffic (e.g., automated test suites), idle keep-alive sockets accumulated and consumed the `max_open_sockets=7` budget, preventing new connections including WebSocket upgrades.

Fixed by adding `Connection: close` to both `web_json_ok()` and `web_json_error()` in `web_json.c`. Since all API handlers funnel through these two functions, every API response now releases its socket immediately — matching the existing fix for static files in v1.2.0.

**WebSocket: stale sockets not reclaimed promptly on client disconnect**

When a WebSocket client sent a CLOSE frame, `ws_handler` removed the client from the push list but did not tell httpd to release the underlying socket. The socket lingered until httpd independently detected the TCP FIN in a later `select()` cycle. With `lru_purge_enable=false`, this delayed reclamation could leave sockets occupied well after the client disconnected.

Fixed by calling `httpd_sess_trigger_close()` after `ws_remove_client()` in the CLOSE frame handler, which queues an immediate session teardown on the httpd task.

### Web Dashboard: FWD & REF Power History Charts

Added real-time time-series charts for FWD and REF power (watts) to the web dashboard. Each chart has its own history window selector (5 min / 15 min / 30 min / 1 hour) and uses the same circular buffer and throttled-redraw pattern as the existing temperature chart. The two charts sit in a responsive two-column grid row between the instantaneous power meters and the temperature chart. Translations added for English, Polish, and Russian.

### Documentation

- Corrected WebSocket broadcast interval from "500 ms" to "250 ms" in `components/web_server/README.md` to match actual code (`pdMS_TO_TICKS(250)`).

### Tests

- `test_receives_multiple_frames`: Relaxed timing lower bound from 0.5s to 0.3s. With a 250ms push interval, 3 frames span ~500ms and jitter could land just under 0.5.
- `test_multiple_clients`: Added delays for server socket cleanup between connections.

### Files Changed

| File | Change |
|------|--------|
| `components/web_server/web_json.c` | Added `Connection: close` header to `web_json_ok()` and `web_json_error()` |
| `components/web_server/web_ws.c` | Added `httpd_sess_trigger_close()` on WebSocket CLOSE frame |
| `components/web_server/README.md` | Corrected broadcast interval to 250 ms |
| `tests/test_websocket.py` | Relaxed timing assertion; added inter-test cleanup delays |
| `web/static/index.html` | Added FWD and REF power history chart containers with history dropdowns |
| `web/static/js/dashboard.js` | Added power history buffers, chart creation, append/update logic, and history select handlers |
| `web/static/lang/en.json` | Added `dashboard.fwd_chart` and `dashboard.ref_chart` keys |
| `web/static/lang/pl.json` | Added Polish translations for power history chart titles |
| `web/static/lang/ru.json` | Added Russian translations for power history chart titles |

### Power Calibration Refactor: Log-Linear Detector Support

Replaced the square-law power calibration model (`P = cal_factor x V^2`) with a log-linear detector model supporting slope, intercept, coupler coupling factor, and attenuator offset per channel. This supports AD8307, AD8318, K9KJ dual RF head, and similar log-linear power detectors used in amateur radio.

**New config parameters** (per channel, FWD and REF independently):

| Key | Description | Default |
|-----|-------------|---------|
| `fwd_slope` / `ref_slope` | Detector slope (mV/dB) | -25.0 |
| `fwd_intercept` / `ref_intercept` | Detector intercept (dBm at 0V) | 0.0 |
| `fwd_coupling` / `ref_coupling` | Directional coupler factor (dB, negative) | 0.0 |
| `fwd_atten` / `ref_atten` | Inline attenuator (dB) | 0.0 |
| `adc_r_top` | ADC input divider top resistor (ohms) | 10000 |
| `adc_r_bottom` | ADC input divider bottom resistor (ohms) | 15000 |

**Removed config parameters:** `fwd_cal`, `ref_cal` (the old square-law calibration factors).

**dBm support:** Power readings are now computed in dBm first, then converted to watts. Both units are exposed in the REST API (`fwd_dbm`, `ref_dbm`), CLI status/monitor output, and the web dashboard.

**Conversion formula:**

```
V_det_mV   = (V_adc / divider_ratio) * 1000
dBm_det    = (V_det_mV / slope) + intercept
dBm_actual = dBm_det + attenuator + abs(coupling)
P_watts    = 10^((dBm_actual - 30) / 10)
```

**NVS config reset:** The `app_config_t` struct layout has changed. On first boot after upgrading, the firmware detects the size mismatch and resets all configuration to factory defaults. Users must re-enter their settings (sequences, thresholds, relay names, and the new power calibration values).

### Files Changed (Power Calibration)

| File | Change |
|------|--------|
| `components/config/include/config.h` | Replaced `fwd_power_cal_factor`/`ref_power_cal_factor` with 10 new power cal fields |
| `components/config/config.c` | New defaults, 10 new config key entries replacing old 2 |
| `components/monitor/monitor.c` | Log-linear conversion replacing square-law; `adc_voltage_to_dbm()` and `dbm_to_watts()` helpers |
| `components/system_state/include/system_state.h` | Added `fwd_power_dbm`, `ref_power_dbm` fields |
| `components/system_state/system_state.c` | Updated `system_state_set_sensors()` to accept dBm values |
| `components/web_server/api_state.c` | Added `fwd_dbm`, `ref_dbm` to state JSON |
| `components/web_server/api_config.c` | 10 new config keys in GET/POST replacing old 2 |
| `components/cli/cmd_config.c` | Updated `config show` and `config set` for new power cal fields |
| `components/cli/cmd_status.c` | Power line shows dBm alongside watts |
| `components/cli/cmd_monitor.c` | dBm columns in human-readable and CSV output |
| `web/static/index.html` | dBm readout elements next to power meters |
| `web/static/js/dashboard.js` | dBm display logic |
| `web/static/js/config.js` | New power cal field definitions, ADC Divider section |
| `web/static/lang/en.json` | i18n keys for new fields |
| `web/static/lang/ru.json` | Russian translations |
| `web/static/lang/pl.json` | Polish translations |

## v1.2.0

### New Feature: Web Interface

Full browser-based UI for the PA sequencer, served from the ESP32's SPIFFS partition. No app install or CLI access needed — connect to the device's IP and manage everything from a phone, tablet, or desktop browser.

**Build system**: Tailwind CSS standalone CLI (auto-downloaded, no Node.js required). `./web/build/build.sh` produces gzipped static assets in `data/`. Total build output: ~108 KB, well within the ~1 MB SPIFFS budget.

**Dashboard tab** — real-time monitoring via WebSocket (250 ms server push):

- PTT state badge (RX/TX/Sequencing/Fault) with color-coded background and pulse animation during sequencing
- Fault banner with fault type and one-click Clear Fault button
- Relay status row — 6 labeled toggle buttons, interactive only in RX state, with optimistic UI and server-driven state
- FWD and REF power meters (Chart.js horizontal bars) with numeric readout and auto-scaling peak tracking
- SWR meter with green/yellow/red color zones based on configured threshold
- Temperature chart (Chart.js line) with two datasets, selectable history window (5 min to 1 hour), and circular buffer storing up to 1 hour of data
- WiFi signal icon in header bar with RSSI tooltip, click navigates to WiFi settings

**Configuration tab** — all device settings editable from the browser:

- **Sequence editor** — side-by-side TX/RX step lists with SortableJS drag-to-reorder, relay/state/delay fields, add/delete steps, pending changes indicator, and Apply button (gated on RX state)
- **Thresholds & Calibration** — SWR threshold, temperature thresholds, PA relay ID, power cal factors, thermistor cal values. Edit-on-blur with green/red flash feedback
- **Relay names** — 6 text inputs, changes propagate to dashboard labels and sequence editor dropdowns via WebSocket
- **WiFi** — connection status with signal bars, SSID/password form, network scan with click-to-populate, auto-connect toggle, disconnect, erase credentials
- **OTA** — firmware version/partition/app state display, editable GitHub repo field, Update to Latest (fire-and-forget with automatic reboot)
- **System** — firmware info, language selector (English/Polish/Russian), log level dropdown with tag filter, reboot with confirmation
- **Collapsible sections** — all config sections collapse/expand with state persisted in localStorage
- **Save and Apply / Reset Defaults** — NVS persistence and factory reset with themed confirmation dialog

**Theming**: Dark (default) and light themes via CSS custom properties. Theme toggle in header bar, selection persisted in localStorage. Tailwind references theme variables so switching works without rebuilding CSS.

**Internationalization**: All UI strings driven by JSON language files (~85 keys each). Ships with English, Polish, and Russian. Language selector in the System section; selection persisted in localStorage. `data-i18n` attribute scanning with `I18n.t()` lookup for dynamic strings. Adding a language requires only a new JSON file.

**Error handling & polish**:

- Toast notifications (top-right, click to dismiss, 8 s for errors / 5 s otherwise)
- Connection lost banner on WebSocket disconnect
- Stale data indication — dashboard dims with reduced opacity and disabled interaction, "Xs ago" timestamp updates live
- Themed confirmation dialog (Cancel/Confirm, Escape/click-outside to dismiss) replacing native `confirm()`
- WebSocket reconnect with exponential backoff (1 s → 30 s cap), status dot in header (green/amber/red)

**Files added**:

| File | Purpose |
|------|---------|
| `web/build/build.sh` | Master build script (Tailwind CLI, copy, gzip, size report) |
| `web/build/tailwind.config.js` | Tailwind config with CSS variable color references |
| `web/build/input.css` | Tailwind input directives + stale data styles |
| `web/static/index.html` | Page skeleton — header, tabs, dashboard widgets, config sections, modals |
| `web/static/js/app.js` | Entry point, tab switching, API helpers, toast, confirm dialog |
| `web/static/js/ws.js` | WebSocket manager with reconnect, listener pattern, stale data handling |
| `web/static/js/dashboard.js` | PTT badge, fault banner, relay toggles, power/SWR/temp charts, WiFi icon |
| `web/static/js/seq-editor.js` | TX/RX sequence editor with SortableJS drag reorder |
| `web/static/js/config.js` | Thresholds, calibration, relay names, OTA, system settings |
| `web/static/js/wifi.js` | WiFi status, credentials, scan, connect/disconnect |
| `web/static/js/theme.js` | Dark/light theme switching with localStorage persistence |
| `web/static/js/i18n.js` | Internationalization — JSON language files, DOM scanning, `t()` lookup |
| `web/static/lang/en.json` | English UI strings (~85 keys) |
| `web/static/lang/pl.json` | Polish UI strings |
| `web/static/lang/ru.json` | Russian UI strings |
| `web/static/themes/dark.css` | Dark theme — CSS custom properties |
| `web/static/themes/light.css` | Light theme — CSS custom properties |
| `web/static/lib/chart.min.js` | Chart.js (vendored) |
| `web/static/lib/sortable.min.js` | SortableJS (vendored) |
| `web/README.md` | Web interface documentation |

**Firmware changes for web serving**:

| File | Change |
|------|---------|
| `components/web_server/api_static.c` | Gzip-aware file serving — checks for `.gz` variant, sets `Content-Encoding: gzip` |

### Bug Fixes

**HTTP: static file connections exhaust socket pool, blocking WebSocket**

The ESP-IDF HTTP server is configured with `max_open_sockets=7`. Browser requests for static assets (HTML, CSS, JS, fonts) used HTTP keep-alive by default, holding connections open after the response completed. A typical page load fetches 8+ files, saturating all available sockets before the browser could upgrade one to a WebSocket. The dashboard would render but the WS connection dot stayed red — no state updates, no relay control.

Fixed by adding `Connection: close` to all static file responses in `api_static.c`. Each static response now completes and releases its socket immediately, leaving a socket available for the WebSocket upgrade.

**Sequencer: reconcile loop never reaches steady TX or RX state**

After a relay sequence completed, the reconcile loop re-reads the PTT GPIO to check whether the hardware state matches the FSM direction. The condition `ptt && s_state != SEQ_STATE_TX` was used to detect a mismatch, but `s_state` was still `SEQ_STATE_SEQUENCING_TX` at that point — so the check was always true, causing the sequencer to immediately re-run the TX sequence in an infinite loop. The FSM never promoted to `SEQ_STATE_TX` or `SEQ_STATE_RX`, and the sequencer reported `SEQ_TX` or `SEQ_RX` indefinitely.

Fixed by tracking the direction of the last completed sequence (`sequenced_to_tx`) instead of comparing against the target steady state. The reconcile loop now checks whether the GPIO agrees with the direction just sequenced and only re-sequences if they disagree. When they match, the FSM promotes to the correct steady state.

### Files Changed

| File | Change |
|------|--------|
| `components/web_server/api_static.c` | Added `Connection: close` header to all static file responses |
| `components/sequencer/sequencer.c` | Fixed reconcile loop condition to use direction-based check instead of state equality |

## v1.1.6

### Bug Fixes

**WebSocket: crash during OTA update due to async task deletion**

`ota update latest` intermittently crashed with `assert failed: xQueueSemaphoreTake queue.c:1709 (( pxQueue ))`. The OTA SPIFFS update path calls `web_server_stop()`, which called `vTaskDelete()` on the WebSocket push task from the OTA task context. On ESP-IDF, `vTaskDelete()` of another task is asynchronous — the target task is not guaranteed to be dead when the call returns. `ws_stop()` then immediately deleted `s_mutex` with `vSemaphoreDelete()`. If the push task was mid-cycle or about to call `xSemaphoreTake(s_mutex, ...)`, it hit a NULL or dangling handle.

A second path to the same crash: `web_server_stop()` called `ws_stop()` (which deleted `s_mutex`) before `httpd_stop()`. When `httpd_stop()` closes all open sockets it fires the `ws_close_fd` callback for each WS client, which calls `ws_remove_client()` → `xSemaphoreTake(s_mutex, ...)` on the already-deleted mutex.

Fixed with a three-phase shutdown: (1) `ws_stop_task()` cooperatively stops the push task — sets a `volatile bool` flag, the task checks it at the top of each 500ms cycle, clears its own handle, and calls `vTaskDelete(NULL)` (self-delete is synchronous); the caller polls until the handle goes NULL. (2) `httpd_stop()` closes all sockets — `ws_close_fd` callbacks run with the mutex still valid. (3) `ws_stop_cleanup()` deletes the mutex after all callbacks have completed.

**Web API: stack buffer overflow in POST /api/seq**

A `POST /api/seq` request with more than 8 steps would overflow the stack-local `new_steps[SEQ_MAX_STEPS]` buffer before `config_set_sequence()` had a chance to reject the count. Added a bounds check on the JSON array size before the parsing loop.

**Config: relay name length rejection instead of truncation**

`config_set_relay_name()` rejected names >= `CFG_RELAY_NAME_LEN` (16 chars) with an error, but the existing contract and test suite expected long names to be silently truncated. Removed the length-rejection guard — the `strncpy` + null-termination already handled truncation correctly.

### Improvements

**ESP-IDF style guide compliance**

Systematic cleanup across all components following an automated style review:

- Added missing standard library includes (`<stdlib.h>`, `<stdio.h>`) in 3 web_server files that relied on transitive headers
- Fixed missing spaces around `=` in designated initializers across all 10 `api_*.c` files (29 occurrences)
- Corrected include group ordering in `ota.c`, `web_server.c`, `web_ws.c`, `relays.c`
- Moved `web_build_state_json()` from a forward declaration in `web_ws.c` to `web_json.h` for compile-time signature checking
- Removed column alignment in lookup tables and static variable blocks per style guide recommendation
- Removed decorative comment blocks between `#pragma once`/includes and `extern "C"` guards in 11 headers
- Added braces to braceless `if` statements in `ota.c`
- Standardised infinite loop style to `for (;;)` in `web_ws.c` and `ota.c`
- Removed dead `root` cJSON allocation in WebSocket push task
- Added `<stddef.h>` to `config.h` for portable `size_t`

### Files Changed

| File | Change |
|------|--------|
| `components/web_server/web_ws.c` | Cooperative shutdown for push task; removed dead cJSON allocation; added `<stdlib.h>`; `while(1)` → `for(;;)` |
| `components/web_server/api_seq.c` | Bounds check on step count before parsing loop |
| `components/config/config.c` | Removed relay name length rejection; column alignment cleanup |
| `components/config/include/config.h` | Added `<stddef.h>`; removed decorative comment block; column alignment cleanup |
| `components/web_server/web_json.h` | Added `web_build_state_json()` declaration |
| `components/web_server/web_json.c` | Added `<stdio.h>` |
| `components/web_server/api_wifi.c` | Added `<stdlib.h>`; initializer spacing |
| `components/web_server/api_state.c` | Initializer spacing |
| `components/web_server/api_config.c` | Initializer spacing |
| `components/web_server/api_relay.c` | Initializer spacing |
| `components/web_server/api_fault.c` | Initializer spacing |
| `components/web_server/api_adc.c` | Initializer spacing |
| `components/web_server/api_ota.c` | Initializer spacing |
| `components/web_server/api_system.c` | Initializer spacing |
| `components/web_server/api_static.c` | Initializer spacing |
| `components/web_server/web_server.c` | Include group separation |
| `components/ota/ota.c` | Include group ordering; braceless ifs; `while(1)` → `for(;;)` |
| `components/relays/relays.c` | Include group separation |
| `components/sequencer/sequencer.c` | Column alignment cleanup |
| `components/sequencer/include/sequencer.h` | Column alignment cleanup; removed decorative comment block |
| `components/monitor/include/monitor.h` | Removed decorative comment block |
| `components/system_state/include/system_state.h` | Removed decorative comment block |
| `components/relays/include/relays.h` | Removed decorative comment block |
| `components/ads1115/include/ads1115.h` | Removed decorative comment block |
| `components/ads1115/ads1115.c` | Column alignment cleanup |
| `components/buttons/include/buttons.h` | Removed decorative comment block |
| `components/ptt/include/ptt.h` | Removed decorative comment block |
| `components/hw_config/include/hw_config.h` | Removed decorative comment block |
| `components/ota/include/ota.h` | Removed decorative comment block |
| `components/wifi_sta/include/wifi_sta.h` | Removed decorative comment block |

## v1.1.5

### Refactor: Config Ownership & Service Layer

Centralised config ownership inside the `config` component and introduced a service-function API. Previously, `main.c` owned a `static app_config_t` and handed raw pointers to every subsystem — CLI handlers, web API handlers, sequencer, and monitor all read and wrote through the same shared pointer with inconsistent locking. This created real race windows on the dual-core ESP32-S3 and duplicated business logic across CLI and web entrypoints.

**What changed:**

- **Config owns the draft internally.** `config.c` holds a `static app_config_t s_draft` — no raw pointers leave the component. `config_init()`, `config_save()`, `config_defaults()`, and `config_set_by_key()` are now parameterless. Callers that removed: `cli_get_config()`, `web_get_config()`, `config_get_draft()`.

- **Snapshot-based reads.** All read paths use `config_snapshot()`, which copies the draft under the mutex. This eliminates unlocked reads that previously raced against mutations (e.g., `web_build_state_json()` reading `relay_names` without a lock, monitor reading thresholds mid-update).

- **Service functions for writes.** `config_set_relay_name()` and `config_set_sequence()` validate and write under the lock, replacing duplicated manual lock+memcpy patterns in 4 CLI and 4 web handler files. Validation (relay ID range, step count, state 0/1, delay bounds, name length) is now centralised.

- **Callback-based `config_apply()`.** Pushing draft config to live consumers (sequencer, monitor) goes through `config_apply()`, which calls registered callbacks in order and stops on the first failure. `config_pending_apply()` reports whether the draft differs from the last successful apply via `memcmp` against an internal `s_last_applied` snapshot. Replaces the removed `sequencer_config_matches()`.

- **Race-free sequencer config handoff.** `sequencer_update_config()` now uses a staging area (`s_cfg_pending`) with a `SEQ_EVENT_CONFIG_UPDATE` event and synchronous task-notification ack. The caller blocks up to 100ms for the sequencer task to commit the config in its RX state handler. If PTT races in, the apply fails with `ESP_ERR_TIMEOUT` rather than silently staging a config that may never be committed.

- **Race-free monitor config handoff.** `monitor_update_config()` uses a `portMUX_TYPE` spinlock instead of the config mutex. The monitor task snapshots `s_cfg` into a stack-local copy under the spinlock at the top of each ADC cycle, guaranteeing consistent thresholds throughout the cycle.

- **`sequencer_inject_fault()`.** Centralised fault event construction that was duplicated in `cmd_fault.c` and `api_fault.c`. Maps `SEQ_FAULT_EMERGENCY` to `SEQ_EVENT_EMERGENCY_PA_OFF` and all others to `SEQ_EVENT_FAULT`.

- **Parameterless init functions.** `sequencer_init()`, `monitor_init()`, `cli_init()`, and `web_server_init()` no longer take a config pointer. Each snapshots or registers callbacks internally. `main.c` no longer owns any config state.

### Race Conditions Fixed

| Race | Location | Fix |
|------|----------|-----|
| Sequencer partial-copy on dual-core | `sequencer_update_config()` vs `sequencer_task()` | Staging area + event + synchronous ack |
| Monitor mixed thresholds mid-cycle | `monitor_update_config()` vs `monitor_task()` ADC reads | Spinlock + per-cycle local snapshot |
| Unlocked relay name read from WS push | `web_build_state_json()` reading `cfg->relay_names` | `config_snapshot()` |
| Inconsistent locking across entrypoints | CLI direct pointer vs web lock+snapshot | All paths use `config_snapshot()` or service functions |

### API Changes

| Removed | Replacement |
|---------|-------------|
| `cli_get_config()` | `config_snapshot()` and service functions |
| `web_get_config()` | `config_snapshot()` and service functions |
| `config_get_draft()` | Service functions (`config_set_by_key()`, `config_set_relay_name()`, `config_set_sequence()`, `config_defaults()`) |
| `sequencer_config_matches()` | `config_pending_apply()` |

| Added | Purpose |
|-------|---------|
| `config_snapshot()` | Locked copy of draft for all read paths |
| `config_set_relay_name()` | Validated relay name write |
| `config_set_sequence()` | Validated sequence write (TX or RX) |
| `config_apply()` | Push draft to live consumers via callbacks |
| `config_pending_apply()` | Draft differs from last-applied? |
| `config_register_apply_cb()` | Register consumer callback for apply |
| `sequencer_inject_fault()` | Centralised fault event injection |
| `SEQ_EVENT_CONFIG_UPDATE` | New sequencer event type for config handoff |

### What Did NOT Change

- REST API contract — all endpoints, methods, request/response formats identical
- CLI syntax — all commands, arguments, output formats identical
- NVS format — same `app_config_t` blob, same namespace and key
- Init order — same boot sequence, same task priorities and stack sizes
- Component dependency graph — no new edges (callbacks are function pointers)

### Files Changed

| File | Change |
|------|--------|
| `components/config/include/config.h` | New service function declarations; existing signatures now parameterless |
| `components/config/config.c` | Internal `s_draft` ownership, service functions, callback registry, `config_apply()`, `config_pending_apply()` |
| `components/sequencer/include/sequencer.h` | `sequencer_inject_fault()`, `SEQ_EVENT_CONFIG_UPDATE`, parameterless `sequencer_init()`, removed `sequencer_config_matches()` |
| `components/sequencer/sequencer.c` | Fault inject helper, staging area + ack handshake, snapshot-based init, callback registration |
| `components/monitor/include/monitor.h` | Parameterless `monitor_init()` |
| `components/monitor/monitor.c` | Spinlock for config, per-cycle local snapshot, snapshot-based init, callback registration |
| `components/cli/include/cli.h` | Parameterless `cli_init()`, removed `cli_get_config()` |
| `components/cli/cli.c` | Removed `s_cfg` pointer and getter |
| `components/cli/cmd_seq.c` | Uses `config_set_sequence()`, `config_apply()`, `config_save()`, `config_snapshot()` |
| `components/cli/cmd_config.c` | Uses parameterless `config_set_by_key()`, `config_save()`, `config_defaults()`, `config_snapshot()` |
| `components/cli/cmd_relay.c` | Uses `config_set_relay_name()`, `config_snapshot()` |
| `components/cli/cmd_fault.c` | Uses `sequencer_inject_fault()` |
| `components/cli/cmd_status.c` | Uses `config_snapshot()` |
| `components/web_server/include/web_server.h` | Parameterless `web_server_init()`, removed `web_get_config()` |
| `components/web_server/web_server.c` | Removed `s_cfg` pointer and getter |
| `components/web_server/api_config.c` | Uses `config_snapshot()`, parameterless service functions, `config_pending_apply()` |
| `components/web_server/api_seq.c` | Uses `config_set_sequence()`, `config_apply()` |
| `components/web_server/api_relay.c` | Uses `config_set_relay_name()` |
| `components/web_server/api_fault.c` | Uses `sequencer_inject_fault()` |
| `components/web_server/api_state.c` | Uses `config_snapshot()` for relay names |
| `src/main.c` | Removed `static app_config_t cfg`, all init calls parameterless |

## v1.1.4

### Bug Fixes

**OTA: SPIFFS download failed due to GitHub redirect handling**

The SPIFFS OTA update added in v1.1.3 used a manual chunked download approach (`esp_http_client_open` / `_read`) that did not follow HTTP redirects. GitHub release asset URLs redirect twice (latest → tag → CDN), so the SPIFFS download silently received a redirect response body instead of the actual `spiffs.bin` image, corrupting the partition.

Fixed by rewriting `ota_update_spiffs()` to use `esp_http_client_perform()`, which follows redirects automatically. Data is now written to the partition incrementally via an `HTTP_EVENT_ON_DATA` event handler, which skips response bodies from non-200 responses (redirect hops). The handler erases the partition on the first data chunk of the final 200 response, validates image size against partition size, and reports write progress. This matches how the firmware OTA path already works.

### Files Changed

| File | Change |
|------|--------|
| `components/ota/ota.c` | Rewrote `ota_update_spiffs()` to use event-driven `esp_http_client_perform()` with redirect support; added `spiffs_http_event()` handler and `spiffs_ota_ctx_t` context struct |

## v1.1.3

### Improvements

**OTA: SPIFFS filesystem update alongside firmware**

`ota update latest` only wrote `firmware.bin` to the inactive app partition. The SPIFFS `storage` partition (web UI static files) was never updated over-the-air — users had to USB-flash to get web UI changes.

OTA now downloads and writes `spiffs.bin` to the `storage` partition before starting the firmware update. The web server is stopped and SPIFFS unmounted before the write. If the release has no `spiffs.bin` (HTTP 404), SPIFFS is skipped and the firmware update proceeds as before. If the SPIFFS download fails for any other reason, the entire update aborts before touching firmware. Image size is validated against the partition size before erasing. `build_url()` now accepts a filename parameter so the same function builds URLs for both assets.

### Files Changed

| File | Change |
|------|--------|
| `components/ota/ota.c` | SPIFFS partition update, `build_url()` takes filename, two-step OTA flow |
| `components/ota/CMakeLists.txt` | Added `spiffs` and `web_server` to REQUIRES |
| `README_DEPLOYMENT.md` | Added `spiffs.bin` to release assets and `gh release create` command |

## v1.1.2

### Bug Fixes

**Sequencer: PTT release lost during TX sequencing**

`run_sequence()` drains the event queue between relay steps to check for faults, but also discards PTT events. If the operator released PTT while the TX relay sequence was still running, the release event was consumed and the FSM promoted to steady TX with no release event left to process. The device would stay keyed until the operator toggled PTT again.

Fixed by adding a reconcile loop after every completed sequence. The sequencer re-reads the PTT GPIO and, if the hardware state disagrees with the FSM direction, immediately runs the opposite sequence. The loop handles rapid PTT toggling without nesting — each pass runs one sequence then re-checks. The same fix applies in both directions (release during TX sequencing, assert during RX sequencing).

**Sequencer: `sequencer_clear_fault()` used `relays_all_off()` instead of the configured RX sequence**

Clearing a fault forced all relays off, which only matched the factory-default RX sequence. If a user configured an RX sequence where a relay remains energised (e.g., a receive preamp bypass), clearing a fault would leave hardware in the wrong state.

Fixed by replacing `relays_all_off()` with `run_sequence(rx_steps)` so that fault recovery restores the actual configured RX relay state. If a new fault fires during the RX sequence replay, `run_sequence()` aborts and re-queues the fault — the correct behavior when the fault condition is still active.

**Config: unsynchronized access to shared `app_config_t` across tasks**

The shared `app_config_t` struct is mutated in-place by both the CLI task and the HTTP server task (config set, sequence edits, relay name updates). `monitor_update_config()` and `sequencer_update_config()` copy the struct with `memcpy()` while other tasks may be mid-write, creating a window for torn or mixed-field snapshots.

Fixed by adding a mutex to the `config` component (`config_lock()` / `config_unlock()`). All mutation and snapshot paths now hold the lock: `config_set_by_key()`, `config_defaults()`, and `config_save()` lock internally; direct struct writes (sequence steps, relay names) in the CLI and web API lock explicitly at the call site; `sequencer_update_config()`, `sequencer_config_matches()`, and `monitor_update_config()` lock around their `memcpy`; the `GET /api/config` handler takes a snapshot copy under the lock then builds JSON from the local copy.

### Improvements

**Config API: `pending_apply` field on `GET /api/config`**

The REST API's `POST /api/config` endpoint modifies the shared in-memory config, but the sequencer and monitor run on private snapshots that are only refreshed via the separate `POST /api/seq/apply` call. Previously there was no way for a client to know whether the displayed config matched what was actually running.

`GET /api/config` now includes a `"pending_apply"` boolean that is `true` when the in-memory config differs from the sequencer's live copy. Implemented via `sequencer_config_matches()`, which does a direct `memcmp` against the sequencer's private config — accurate regardless of whether edits came from the CLI or the web API.

**Validation: hardcoded relay count and delay limits**

Relay ID range checks used hardcoded `6` instead of `HW_RELAY_COUNT`, and sequence delay validation used hardcoded `10000` with no shared constant. If the hardware relay count or max delay ever changed, validation would silently diverge.

Added `SEQ_MAX_DELAY_MS` constant to `config.h`. Replaced all hardcoded relay ID and delay limit checks with `HW_RELAY_COUNT` and `SEQ_MAX_DELAY_MS` in the CLI, web API, and config key-value setter. Error messages now format dynamically from the constants.

**Shared enum↔string helpers for sequencer state and fault names**

State-name tables (`"RX"`, `"SEQ_TX"`, …), fault-name tables (`"none"`, `"HIGH_SWR"`, …), and fault-keyword parsing (`"swr"` → `SEQ_FAULT_HIGH_SWR`) were duplicated across four CLI files and two web API files. Adding a new state or fault type required updating every copy — easy to miss.

Moved the tables into the `sequencer` component as `seq_state_name()`, `seq_fault_name()`, and `seq_fault_parse()`. All CLI commands and web handlers now call the shared helpers. Removes ~60 lines of duplicated code and ensures a single source of truth for display strings and input parsing.

### Tests

**New regression tests**

- `test_config.py`: Added `TestPendingApply` class — verifies `pending_apply` is `false` after apply, `true` after an unaplied edit, and `false` again after a subsequent apply.
- `test_config.py`: Added `test_has_pending_apply` to verify the field exists and is boolean.
- `test_fault.py`: Added `test_clear_restores_rx_relay_state` — injects a fault, clears it, and verifies all relays match the expected RX sequence end state. Guards against regression to `relays_all_off()`.

**Hardcoded constant cleanup**

Replaced hardcoded `6` (relay count) and `"1-6"` string assertions across all test files with a `RELAY_COUNT` constant. Replaced fragile literal substring checks in error assertions with keyword matching (e.g., `"relay" in error.lower()` instead of `"1-6" in error`).

- `test_config.py`: `RELAY_COUNT` for `pa_relay` range and `relay_names` length
- `test_state.py`: `RELAY_COUNT` for `relays` and `relay_names` array lengths
- `test_relay.py`: `RELAY_COUNT` for parametrize range, loop bounds, and error tests
- `test_seq.py`: Keyword-based error assertions for relay ID and delay validation

### Files Changed

| File | Change |
|------|--------|
| `components/sequencer/sequencer.c` | PTT reconcile loop, RX sequence on fault clear, `sequencer_config_matches()`, shared `seq_state_name()`/`seq_fault_name()`/`seq_fault_parse()` |
| `components/sequencer/include/sequencer.h` | Added `sequencer_config_matches()`, `seq_state_name()`, `seq_fault_name()`, `seq_fault_parse()` declarations |
| `components/sequencer/CMakeLists.txt` | Added `driver` and `hw_config` to REQUIRES for GPIO read |
| `components/config/config.c` | Config mutex, internal locking, `pa_relay` range uses `HW_RELAY_COUNT` |
| `components/config/include/config.h` | Added `config_lock()`/`config_unlock()`, `SEQ_MAX_DELAY_MS` |
| `components/config/CMakeLists.txt` | Added `freertos` to REQUIRES |
| `components/monitor/monitor.c` | Lock around `memcpy` in `monitor_update_config()` |
| `components/cli/cmd_status.c` | Replaced local name tables with shared helpers |
| `components/cli/cmd_fault.c` | Replaced local name tables and fault parsing with shared helpers |
| `components/cli/cmd_monitor.c` | Replaced local name tables with shared helpers |
| `components/cli/cmd_seq.c` | Config lock on step writes, relay/delay validation uses constants |
| `components/cli/cmd_relay.c` | Config lock on relay name writes |
| `components/web_server/api_config.c` | Snapshot copy under lock for GET, added `pending_apply` |
| `components/web_server/api_state.c` | Replaced local name tables with shared helpers |
| `components/web_server/api_fault.c` | Replaced fault parsing with `seq_fault_parse()` |
| `components/web_server/api_seq.c` | Config lock on step writes, relay/delay validation uses constants |
| `components/web_server/api_relay.c` | Config lock on name writes, relay ID error messages use `HW_RELAY_COUNT` |
| `tests/test_config.py` | `RELAY_COUNT` constant, `pending_apply` field and lifecycle tests |
| `tests/test_state.py` | `RELAY_COUNT` constant for array length checks |
| `tests/test_relay.py` | `RELAY_COUNT` constant, parametrize from constant, robust error assertions |
| `tests/test_seq.py` | Keyword-based error assertions instead of literal substrings |
| `tests/test_fault.py` | New `test_clear_restores_rx_relay_state` regression test |
