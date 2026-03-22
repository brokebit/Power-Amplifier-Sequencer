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
| `sequencer` | Central state machine (RX, SEQUENCING_TX, TX, SEQUENCING_RX, FAULT). Sole consumer of the event queue. Orchestrates relay switching in timed sequences. Latching fault mode requires explicit clearance. |
| `monitor` | Periodic ADC sensing task. Reads 4 channels via single-shot conversions (~500 ms cycle at 8 SPS). Computes power (W), SWR, and temperatures (C). Injects fault events on threshold breach. |
| `ads1115` | Low-level I2C driver for ADS1115. Single-shot trigger/wait/read pattern. Caller owns synchronisation. |
| `system_state` | Shared blackboard (spinlock-protected struct). All subsystems publish here; consumers read atomic snapshots. |
| `config` | NVS-backed runtime configuration. Relay sequences, fault thresholds, calibration factors, thermistor parameters. Writes defaults on first boot. |
| `relays` | GPIO driver for 6 relays. 1-indexed IDs matching schematic labels. |
| `ptt` | PTT GPIO interrupt driver. Both-edge ISR posts assert/release events to sequencer queue. |
| `buttons` | Debounced button driver (50 ms timer). BTN1 wired to emergency PA off event. BTN2-6 support optional callbacks. |
| `hw_config` | Header-only pin definitions and peripheral addresses. |

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
```

### Key Design Decisions

- **Single-consumer event queue.** All event sources post to one FreeRTOS queue; only `sequencer_task` reads it. No mutexes needed for relay control or state transitions.
- **Blackboard pattern for observable state.** `system_state` aggregates readings from all subsystems into a spinlock-protected struct. Consumers (display, console logger) get consistent atomic snapshots.
- **Latching faults.** The sequencer enters FAULT state and ignores all events until `sequencer_clear_fault()` is called externally. The monitor sends fault events every cycle — the sequencer is responsible for deduplication.
- **Emergency shutdown hardcodes relay 2.** Safety-critical: PA relay is de-energised before running the full RX sequence, even if config is malformed.
- **Configuration snapshot at init.** Both the sequencer and monitor copy `app_config_t` at init time. Runtime config changes require a restart.

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
   - File > Open Folder > select the `Sequencer-Claude` directory
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
Sequencer-Claude/
├── src/
│   └── main.c                  # app_main — init sequence + console print loop
├── components/
│   ├── ads1115/                # ADS1115 I2C driver
│   ├── buttons/                # Debounced button input
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
