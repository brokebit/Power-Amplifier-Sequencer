# Serial Console (CLI)

On boot, a `seq> ` prompt is available on UART0 (USB serial). ESP_LOG output is suppressed by default to keep the prompt clean. Type `help` for a command list.

## System

| Command | Description |
|---------|-------------|
| `status` | One-shot dump of PTT, state, fault, relays, power, SWR, temps, chip 0 ADC, WiFi |
| `version` | Firmware name/version, IDF version, chip info |
| `reboot` | Restart the ESP32 |
| `log on` | Enable log output (INFO level) |
| `log off` | Suppress all log output (default) |
| `log <level> [tag]` | Set level per-tag: `none`, `error`, `warn`, `info`, `debug`, `verbose` |

## Configuration

| Command | Description |
|---------|-------------|
| `config show` | Print all config fields (thresholds, calibration, sequences) |
| `config set <key> <value>` | Modify a field in memory (see keys below) |
| `config save` | Persist current config to NVS |
| `config defaults` | Reset to factory defaults (in memory only) |

Config keys: `swr_threshold`, `temp1_threshold`, `temp2_threshold`, `pa_relay`, `fwd_slope`, `fwd_intercept`, `fwd_coupling`, `fwd_atten`, `ref_slope`, `ref_intercept`, `ref_coupling`, `ref_atten`, `adc_1a_r_top`, `adc_1a_r_bottom`, `adc_1b_r_top`, `adc_1b_r_bottom`, `adc_0a_r_top`, `adc_0a_r_bottom`, `adc_0b_r_top`, `adc_0b_r_bottom`, `adc_0c_r_top`, `adc_0c_r_bottom`, `adc_0d_r_top`, `adc_0d_r_bottom`, `therm_beta`, `therm_r0`, `therm_rseries`

## Sequence Editing

| Command | Description |
|---------|-------------|
| `seq tx show` | Display current TX relay sequence |
| `seq rx show` | Display current RX relay sequence |
| `seq tx set R3:on:50 R1:on:50 R2:on:0` | Define TX sequence (up to 8 steps) |
| `seq rx set R2:off:50 R1:off:50 R3:off:0` | Define RX sequence |
| `seq save` | Persist sequences to NVS |
| `seq apply` | Hot-swap config into running sequencer + monitor (RX state only) |

Step format: `R<relay_id>:<on|off>:<delay_ms>` — e.g. `R3:on:50` means "energise relay 3, wait 50 ms".

## Relay Aliases

Each relay can have an optional display name to make output more readable. Aliases are display-only — commands and sequence definitions still use R1-R6.

```
seq> relay name 2 PA
R2 = PA
Use 'config save' to persist

seq> relay name 3 LNA
R3 = LNA
Use 'config save' to persist

seq> status
PTT: off   State: RX     Fault: none
Relays: [R1:off] [R2/PA:off] [R3/LNA:off] [R4:off] [R5:off] [R6:off]
...

seq> seq tx show
TX sequence (3 steps):
  1: R3/LNA       ON  1000ms
  2: R1            ON  1000ms
  3: R2/PA         ON  0ms
```

Aliases are stored in the main config blob and persist across reboots after `config save`. Use `relay name <1-6>` (no label) to clear an alias, or `config defaults` to clear all.

## Relay & Fault Control

| Command | Description |
|---------|-------------|
| `relay show` | Display all relay states (with aliases if set) |
| `relay <1-6> on\|off` | Force a single relay (bypasses sequencer — use with caution) |
| `relay name` | Show all relay name aliases |
| `relay name <1-6> <label>` | Set a display alias for a relay (max 15 chars) |
| `relay name <1-6>` | Clear a relay's alias |
| `fault show` | Show sequencer state and fault code |
| `fault clear` | Clear a latched fault, return to RX |
| `fault inject <swr\|temp1\|temp2\|emergency>` | Inject a test fault event |

## ADC & Monitoring

| Command | Description |
|---------|-------------|
| `adc read <0-3>` | Read a single ADC channel from chip 1 (voltage) |
| `adc scan` | Read all 4 channels on both chips (chip 0 and chip 1) |
| `adc name` | Show all chip 0 channel name aliases |
| `adc name <0-3> <label>` | Set a display name for a chip 0 channel (max 15 chars) |
| `adc name <0-3>` | Clear a channel's name |
| `monitor [interval_ms] [csv]` | Continuous status output (default 1000 ms, Enter to stop). Optional `csv` for machine-readable output. Includes chip 0 ADC readings with channel names. |

## WiFi

| Command | Description |
|---------|-------------|
| `wifi status` | Show connection state, IP address, RSSI, auto-connect setting |
| `wifi config <ssid> [password]` | Save WiFi credentials to NVS (password optional for open networks) |
| `wifi connect` | Connect using saved credentials |
| `wifi disconnect` | Disconnect from AP (does not erase credentials) |
| `wifi scan` | Scan and list available networks |
| `wifi enable` | Enable auto-connect on boot (default) |
| `wifi disable` | Disable auto-connect and disconnect |
| `wifi erase` | Erase saved credentials from NVS |

WiFi credentials are stored in a separate NVS namespace (`wifi_cfg`) from the main config, so they survive `config defaults` resets and `app_config_t` struct changes.

## OTA Firmware Updates

| Command | Description |
|---------|-------------|
| `ota status` | Show running partition, firmware version, app state, other slot info |
| `ota repo` | Show configured GitHub repo |
| `ota repo <owner/repo>` | Set GitHub repo for version shorthand (saved to NVS) |
| `ota update latest` | Download latest release from configured GitHub repo |
| `ota update <vX.Y.Z>` | Download a specific release version |
| `ota update <https://...>` | Download firmware from any HTTPS URL |
| `ota rollback` | Revert to previously running firmware and reboot |
| `ota validate` | Manually mark current firmware as valid |

The device uses a dual-partition OTA scheme (ota_0 / ota_1, 3 MB each). After an OTA update and reboot, the new firmware must validate itself — `app_ota_init()` does this automatically at the end of boot if all subsystems initialise successfully. If the new firmware crashes before validation, the bootloader automatically rolls back to the previous working firmware.

GitHub release URLs support version shorthand once a repo is configured:

```
seq> ota repo myuser/sequencer-fw
GitHub repo set to 'myuser/sequencer-fw'

seq> ota update latest
OTA update from: https://github.com/myuser/sequencer-fw/releases/latest/download/firmware.bin
Starting download...
  Progress: 10% (104857 / 1048576 bytes)
  ...
OTA update successful! Rebooting in 2 seconds...
```

OTA repo configuration is stored in a separate NVS namespace (`ota_cfg`) and survives `config defaults` resets.

**Note:** The first flash after enabling OTA must be done over USB (`pio run -t upload`) to write the new partition table and bootloader. After that, firmware can be updated over WiFi.

## CSV Monitor Output

When `csv` is passed to the `monitor` command, output is one comma-separated line per sample with no headers or status messages. Column order:

| Column | Type | Description |
|--------|------|-------------|
| ptt | int | PTT state: 1 = active, 0 = inactive |
| state | string | Sequencer state: RX, SEQ_TX, TX, SEQ_RX, FAULT |
| fault | string | Fault code: none, HIGH_SWR, OVER_TEMP1, OVER_TEMP2, EMERGENCY |
| relay1..relay6 | int | Individual relay states: 1 = on, 0 = off |
| fwd_dbm | float | Forward power in dBm |
| fwd_w | float | Forward power in watts |
| ref_dbm | float | Reflected power in dBm |
| ref_w | float | Reflected power in watts |
| swr | float | Standing wave ratio |
| temp1_c | float | Temperature sensor 1 in degrees C |
| temp2_c | float | Temperature sensor 2 in degrees C |
| adc_0_ch0 | float | Chip 0 AIN0 corrected voltage (V) |
| adc_0_ch1 | float | Chip 0 AIN1 corrected voltage (V) |
| adc_0_ch2 | float | Chip 0 AIN2 corrected voltage (V) |
| adc_0_ch3 | float | Chip 0 AIN3 corrected voltage (V) |

Example: `0,RX,none,0,0,0,0,0,0,-999.0,0.0,-999.0,0.0,1.0,28.3,27.9,0.000,0.000,0.000,0.000`
