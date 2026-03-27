# RF Power Amplifier Sequencer

ESP32-S3 RF Power Amplifier TX/RX relay sequencer for 23cm band operation. Controls up to 6 relays in a configurable timed sequence to safely transition between receive and transmit states, with real-time monitoring of forward/reflected RF power, SWR, and PA temperatures.

Built with ESP-IDF (via PlatformIO) and FreeRTOS.

## Hardware

- **MCU:** ESP32-S3-DevKitM-1 (8 MB flash)
- **ADC:** Two ADS1115 16-bit ADCs on I2C (0x48 reserved, 0x49 active)
- **Relays:** 6 relay outputs (GPIOs 39, 40, 41, 42, 11, 12)
  - Relay 1: RX/TX path select
  - Relay 2: PA on/off (configurable emergency shutdown target, default)
  - Relay 3: LNA isolate
  - Relays 4-6: Spare
- **Sensors (on ADS1115 at 0x49):**
  - AIN0: Forward RF power (directional coupler detector)
  - AIN1: Reflected RF power
  - AIN2: Temperature Sensor 1 (100k NTC thermistor)
  - AIN3: Temperature Sensor 2 (100k NTC thermistor)
- **Inputs:**
  - PTT on GPIO 13 (active low)
  - 6 buttons (GPIO 4, 5, 6, 7, 48, 47) — BTN1 is emergency PA off, BTN2-6 spare
- **Display:** Nextion on UART1 (TX:17, RX:18, 9600 baud) — reserved, not yet implemented
- **I2C:** SDA GPIO 1, SCL GPIO 2, 400 kHz

## Architecture

The system is organized as ESP-IDF components under `components/`. Two FreeRTOS tasks run the core logic:

```
                    +-----------+
  PTT ISR -------->|           |
  Button ISR ----->| Sequencer |-----> relay_set()
  Monitor task --->|  (pri 10) |-----> system_state
                    +-----------+
                         ^
                         | event queue (depth 16)
                         |
                    +-----------+
                    |  Monitor  |-----> system_state
                    |  (pri 7)  |-----> fault events
                    +-----------+
                         |
                    ADS1115 (I2C)
```

### Components

| Component | Purpose |
|---|---|
| [sequencer](components/sequencer/README.md) | Central state machine (RX, SEQUENCING_TX, TX, SEQUENCING_RX, FAULT). Sole consumer of the event queue. Orchestrates relay switching in timed sequences. Latching fault mode requires explicit clearance. |
| [monitor](components/monitor/README.md) | Periodic ADC sensing task. Reads 4 channels via single-shot conversions (~500 ms cycle at 8 SPS). Computes power (W), SWR, and temperatures (C). Injects fault events on threshold breach. |
| [ads1115](components/ads1115/README.md) | Low-level I2C driver for ADS1115. Single-shot trigger/wait/read pattern. Caller owns synchronisation. |
| [system_state](components/system_state/README.md) | Shared blackboard (spinlock-protected struct). All subsystems publish here; consumers read atomic snapshots. |
| [config](components/config/) | NVS-backed runtime configuration. Relay sequences, fault thresholds, calibration factors, thermistor parameters. Writes defaults on first boot. |
| [relays](components/relays/) | GPIO driver for 6 relays. 1-indexed IDs matching schematic labels. |
| [ptt](components/ptt/) | PTT GPIO interrupt driver. Both-edge ISR posts assert/release events to sequencer queue. |
| [buttons](components/buttons/) | Debounced button driver (50 ms timer). BTN1 wired to emergency PA off event. BTN2-6 support optional callbacks. |
| [cli](components/cli/README.md) | Interactive serial console (REPL) on UART0. See [CLI Reference](README_CLI.md). |
| [wifi_sta](components/wifi_sta/) | WiFi Station mode manager. NVS-backed credentials in separate namespace. Auto-connects on boot with exponential backoff retry. Publishes connection state to system_state. |
| [ota](components/ota/) | OTA firmware update manager. HTTPS pull from GitHub Releases or direct URL. Dual-partition rollback support with automatic boot validation. NVS-backed repo configuration. |
| [web_server](components/web_server/) | HTTP server with REST API, WebSocket live state push, and SPIFFS static file serving. See [API Reference](README_API.md). |
| [hw_config](components/hw_config/) | Header-only pin definitions and peripheral addresses. |

### Initialization Order

The init order in `app_main()` matters — later components depend on earlier ones:

```
config_init()      — load NVS config (or write defaults)
relays_init()      — configure GPIO outputs before sequencer drives them
sequencer_init()   — create event queue (needed by PTT, buttons, monitor)
ptt_init()         — install GPIO ISR service, arm PTT interrupt
buttons_init()     — uses already-installed ISR service
monitor_init()     — I2C bus, ADS1115s, ALERT/RDY ISRs
app_wifi_init()    — event loop, netif, WiFi driver; auto-connect if creds saved
xTaskCreate(sequencer_task, ...)   — priority 10
xTaskCreate(monitor_task, ...)     — priority 7
cli_init()         — register commands, suppress logging, start REPL task (pri 5)
app_ota_init()     — validate firmware after OTA update (rollback gate)
web_server_init()  — HTTP server, REST API, WebSocket push, SPIFFS mount
```

### Key Design Decisions

- **Single-consumer event queue.** All event sources post to one FreeRTOS queue; only `sequencer_task` reads it. No mutexes needed for relay control or state transitions.
- **Blackboard pattern for observable state.** `system_state` aggregates readings from all subsystems into a spinlock-protected struct. Consumers (display, console logger) get consistent atomic snapshots.
- **Latching faults.** The sequencer enters FAULT state and ignores all events until `sequencer_clear_fault()` is called externally. The monitor sends fault events every cycle — the sequencer is responsible for deduplication.
- **Emergency shutdown uses configurable PA relay.** Safety-critical: the PA relay (configurable via `config set pa_relay <1-6>`, default: 2) is de-energised before running the full RX sequence.
- **Configuration snapshot at init, hot-swappable via CLI.** Both the sequencer and monitor copy `app_config_t` at init time. The CLI can modify config in-place and push updates via `sequencer_update_config()` and `monitor_update_config()` without restarting.

## Relay Sequences

The sequencer transitions between RX and TX by executing a configurable list of relay steps. Each direction (TX and RX) has its own sequence of up to 8 steps, stored in NVS and loaded at boot.

### Step Format

Each step is a `seq_step_t`:

| Field | Type | Description |
|---|---|---|
| `relay_id` | `uint8_t` | Relay number (1-6), matching schematic labels |
| `state` | `uint8_t` | 1 = energise (ON), 0 = release (OFF) |
| `delay_ms` | `uint16_t` | Milliseconds to wait *after* switching this relay, before the next step |

### How It Works

When PTT is asserted, the sequencer runs the TX step list top-to-bottom. When PTT is released, it runs the RX step list. The last step's `delay_ms` is typically 0 since there is nothing to wait for.

If a fault or emergency event arrives mid-sequence, the sequencer aborts immediately — the PA relay (configurable, default R2) is forced off first, then the full RX sequence runs to restore a safe state.

### Default Sequences

The factory defaults use a 3-step sequence for a typical PA setup (LNA isolate, path select, PA enable):

**TX sequence** (PTT assert — transition to transmit):

| Step | Relay | Action | Delay | Purpose |
|------|-------|--------|-------|---------|
| 1 | R3 | ON | 1000 ms | Isolate LNA from TX path |
| 2 | R1 | ON | 1000 ms | Switch antenna path to TX |
| 3 | R2 | ON | 0 ms | Enable PA |

**RX sequence** (PTT release — transition to receive):

| Step | Relay | Action | Delay | Purpose |
|------|-------|--------|-------|---------|
| 1 | R2 | OFF | 1000 ms | Disable PA first |
| 2 | R1 | OFF | 1000 ms | Switch antenna path to RX |
| 3 | R3 | OFF | 0 ms | Re-enable LNA path |

The ordering is critical for safety: on TX, the LNA is isolated *before* the PA is enabled. On RX, the PA is disabled *before* the LNA path is restored. This prevents hot-switching and protects the LNA from transmit power.

### Customising Sequences

Sequences are defined in `config_defaults()` in [config.c](components/config/config.c) and stored as an NVS blob. To change the default sequences, edit the step arrays:

```c
/* Example: 4-step TX sequence with faster timing */
cfg->tx_steps[0] = (seq_step_t){ .relay_id = 3, .state = 1, .delay_ms = 5 };
cfg->tx_steps[1] = (seq_step_t){ .relay_id = 4, .state = 1, .delay_ms = 5 };
cfg->tx_steps[2] = (seq_step_t){ .relay_id = 1, .state = 1, .delay_ms = 10 };
cfg->tx_steps[3] = (seq_step_t){ .relay_id = 2, .state = 1, .delay_ms = 0 };
cfg->tx_num_steps = 4;
```

After changing defaults, erase NVS and re-flash so the new defaults are written:

```
pio run -t erase && pio run -t upload
```

**Constraints:**
- Maximum 8 steps per direction (`SEQ_MAX_STEPS`)
- Relay IDs must be 1-6
- The RX sequence should be the logical reverse of the TX sequence
- The PA relay (used by emergency shutdown) is configurable via `config set pa_relay <1-6>` (default: 2)

## Documentation

| Document | Contents |
|----------|----------|
| [CLI Reference](README_CLI.md) | All serial console commands — system, config, sequences, relays, faults, ADC, WiFi, OTA |
| [REST API Reference](README_API.md) | HTTP endpoints, WebSocket live state, curl examples |
| [Deployment Guide](README_DEPLOYMENT.md) | Creating releases, flashing pre-built firmware (macOS/Linux/Windows), initial setup |

## Development Setup

### Prerequisites

- [Visual Studio Code](https://code.visualstudio.com/)
- [PlatformIO IDE extension](https://platformio.org/install/ide?install=vscode) for VS Code

### Setup

1. **Install PlatformIO extension** in VS Code:
   - Open Extensions (`Cmd+Shift+X` on macOS, `Ctrl+Shift+X` on Linux/Windows)
   - Search for "PlatformIO IDE" and install it
   - Restart VS Code when prompted

2. **Open the project:**
   - File > Open Folder > select the directory where you cloned this repository
   - PlatformIO will auto-detect the `platformio.ini` and configure the environment
   - Wait for PlatformIO to finish downloading the ESP32-S3 toolchain and ESP-IDF framework (first time only)

3. **Build:**
   - Click the checkmark icon in the PlatformIO toolbar (bottom status bar), or
   - Open a PlatformIO terminal and run: `pio run`

4. **Upload and monitor:**
   - Connect the ESP32-S3-DevKitM-1 via USB
   - Click the right-arrow icon to upload, or run: `pio run -t upload`
   - Click the plug icon to open the serial monitor, or run: `pio device monitor`
   - Combined: `pio run -t upload && pio device monitor`

5. **Erase NVS (force config reset to defaults):**
   ```
   pio run -t erase
   ```
   Then re-upload. The config component will write factory defaults on next boot.

### Project Structure

```
Sequencer/
├── src/
│   └── main.c                  # app_main — init sequence, starts REPL
├── components/
│   ├── ads1115/                # ADS1115 I2C driver
│   ├── buttons/                # Debounced button input
│   ├── cli/                    # Interactive serial console (REPL)
│   ├── config/                 # NVS-backed configuration
│   ├── hw_config/              # Pin/peripheral definitions (header only)
│   ├── monitor/                # ADC sensing task
│   ├── ptt/                    # PTT interrupt driver
│   ├── relays/                 # Relay GPIO driver
│   ├── sequencer/              # Core state machine
│   ├── system_state/           # Shared observable state blackboard
│   ├── wifi_sta/               # WiFi Station mode manager
│   ├── ota/                    # OTA firmware update manager
│   └── web_server/             # HTTP REST API, WebSocket, SPIFFS static serving
├── data/                       # SPIFFS filesystem content (web UI)
│   └── index.html              # Dashboard page served at http://<device>/
├── partitions.csv              # Custom partition table (dual OTA + SPIFFS)
├── platformio.ini              # PlatformIO build config
├── sdkconfig.defaults          # ESP-IDF Kconfig overrides (8MB flash, OTA rollback, WS)
└── CMakeLists.txt              # Top-level CMake (required by ESP-IDF)
```

Each component under `components/` has its own `CMakeLists.txt`, `include/` directory with public headers, and a `README.md` with detailed documentation of its data structures, event flow, and architecture decisions.

### TODO
- Implement SPIFFS updates via OTA mechanism for static web assets
- Figure out the RF Head voltage to db math and proper calibration
- Implement the Nextion display driver and UI
- Implement a reset button to recover from EMERGENCY fault state without needing to power cycle
- Build out the web dashboard UI (the current index.html is a minimal placeholder)
- Add authentication to the REST API write endpoints

### BUGS
- Thuroughly test wifi. Seems like I've noticed the device forgetting creds
- If PTT changes to quickly, especially if less than the delay time on a seq, state can get confused