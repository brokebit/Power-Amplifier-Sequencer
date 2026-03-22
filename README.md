# RF Power Amplifier Sequencer

ESP32-S3 RF Power Amplifier TX/RX relay sequencer for 23cm band operation. Controls up to 6 relays in a configurable timed sequence to safely transition between receive and transmit states, with real-time monitoring of forward/reflected RF power, SWR, and PA temperatures.

Built with ESP-IDF (via PlatformIO) and FreeRTOS.

## Hardware

- **MCU:** ESP32-S3-DevKitM-1 (8 MB flash)
- **ADC:** Two ADS1115 16-bit ADCs on I2C (0x48 reserved, 0x49 active)
- **Relays:** 6 relay outputs (GPIOs 39, 40, 41, 42, 11, 12)
  - Relay 1: RX/TX path select
  - Relay 2: PA on/off (hardcoded emergency shutdown target)
  - Relay 3: LNA isolate
  - Relays 4-6: Spare
- **Sensors (on ADS1115 at 0x49):**
  - AIN0: Forward RF power (directional coupler detector)
  - AIN1: Reflected RF power
  - AIN2: Temperature right PA (100k NTC thermistor)
  - AIN3: Temperature left PA (100k NTC thermistor)
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
| [cli](components/cli/README.md) | Interactive serial console (REPL) on UART0 using esp_console. Runtime inspection, configuration, sequence editing, ADC diagnostics, and relay control. |
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
xTaskCreate(sequencer_task, ...)   — priority 10
xTaskCreate(monitor_task, ...)     — priority 7
cli_init()         — register commands, suppress logging, start REPL task (pri 5)
```

### Key Design Decisions

- **Single-consumer event queue.** All event sources post to one FreeRTOS queue; only `sequencer_task` reads it. No mutexes needed for relay control or state transitions.
- **Blackboard pattern for observable state.** `system_state` aggregates readings from all subsystems into a spinlock-protected struct. Consumers (display, console logger) get consistent atomic snapshots.
- **Latching faults.** The sequencer enters FAULT state and ignores all events until `sequencer_clear_fault()` is called externally. The monitor sends fault events every cycle — the sequencer is responsible for deduplication.
- **Emergency shutdown hardcodes relay 2.** Safety-critical: PA relay is de-energised before running the full RX sequence, even if config is malformed.
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

If a fault or emergency event arrives mid-sequence, the sequencer aborts immediately — relay 2 (PA) is forced off first, then the full RX sequence runs to restore a safe state.

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
- Relay 2 is hardcoded as the PA relay in emergency shutdown — if your PA is on a different relay, update `emergency_shutdown()` in [sequencer.c](components/sequencer/sequencer.c)

## Serial Console (CLI)

On boot, a `seq> ` prompt is available on UART0 (USB serial). ESP_LOG output is suppressed by default to keep the prompt clean. Type `help` for a command list.

### System

| Command | Description |
|---------|-------------|
| `status` | One-shot dump of PTT, state, fault, relays, power, SWR, temps |
| `version` | Firmware name/version, IDF version, chip info |
| `reboot` | Restart the ESP32 |
| `log on` | Enable log output (INFO level) |
| `log off` | Suppress all log output (default) |
| `log <level> [tag]` | Set level per-tag: `none`, `error`, `warn`, `info`, `debug`, `verbose` |

### Configuration

| Command | Description |
|---------|-------------|
| `config show` | Print all config fields (thresholds, calibration, sequences) |
| `config set <key> <value>` | Modify a field in memory (see keys below) |
| `config save` | Persist current config to NVS |
| `config defaults` | Reset to factory defaults (in memory only) |

Config keys: `swr_threshold`, `temp_threshold`, `fwd_cal`, `ref_cal`, `therm_beta`, `therm_r0`, `therm_rseries`

### Sequence Editing

| Command | Description |
|---------|-------------|
| `seq tx show` | Display current TX relay sequence |
| `seq rx show` | Display current RX relay sequence |
| `seq tx set R3:on:50 R1:on:50 R2:on:0` | Define TX sequence (up to 8 steps) |
| `seq rx set R2:off:50 R1:off:50 R3:off:0` | Define RX sequence |
| `seq save` | Persist sequences to NVS |
| `seq apply` | Hot-swap config into running sequencer + monitor (RX state only) |

Step format: `R<relay_id>:<on|off>:<delay_ms>` — e.g. `R3:on:50` means "energise relay 3, wait 50 ms".

### Relay & Fault Control

| Command | Description |
|---------|-------------|
| `relay show` | Display all relay states |
| `relay <1-6> on\|off` | Force a single relay (bypasses sequencer — use with caution) |
| `fault show` | Show sequencer state and fault code |
| `fault clear` | Clear a latched fault, return to RX |
| `fault inject <swr\|temp1\|temp2\|emergency>` | Inject a test fault event |

### ADC & Monitoring

| Command | Description |
|---------|-------------|
| `adc read <0-3>` | Read a single ADC channel (voltage) |
| `adc scan` | Read all 4 channels |
| `monitor [interval_ms]` | Continuous status output (default 1000 ms, Enter to stop) |

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
│   └── system_state/           # Shared observable state blackboard
├── platformio.ini              # PlatformIO build config
├── sdkconfig.defaults          # ESP-IDF Kconfig overrides (8MB flash)
└── CMakeLists.txt              # Top-level CMake (required by ESP-IDF)
```

Each component under `components/` has its own `CMakeLists.txt`, `include/` directory with public headers, and a `README.md` with detailed documentation of its data structures, event flow, and architecture decisions.

### TODO 
- Figure out the RF Head voltage to db math and proper calibration.
- Add a CSV mode to the monitor so applicaitons can consume the data
- Make the PA relay configurable vs. hardcoded
- Add ability to associate a name with a relay. i.e. R1 is LNA Coax Switch 
- Implement the Nextion display driver and UI.
- Implement a reset button to recover from EMERGENCY fault state without needing to power cycle.
- Add wifi and OTA updates for remote monitoring and firmware upgrades.
- Implement an API for external control of the sequencer (e.g. from a PC or microcontroller) via WiFi. 
- Add a web interface for monitoring and configuration over WiFi.
