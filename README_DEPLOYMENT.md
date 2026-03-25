# Deployment

## Creating a Release

### Prerequisites

- [GitHub CLI](https://cli.github.com/) (`brew install gh` on macOS) — optional, for command-line releases
- PlatformIO installed and project builds successfully

### Steps

1. **Update the version** in [CMakeLists.txt](CMakeLists.txt):

   ```cmake
   set(PROJECT_VER "1.2.0")
   ```

2. **Build the firmware:**

   ```bash
   pio run
   ```

3. **Generate the combined binary** (includes bootloader, partition table, and firmware in one file for first-time USB flashing):

   ```bash
   esptool.py --chip esp32s3 merge_bin -o .pio/build/esp32-s3-devkitm-1/combined.bin \
     --flash_mode dio --flash_freq 80m --flash_size 8MB \
     0x0     .pio/build/esp32-s3-devkitm-1/bootloader.bin \
     0x8000  .pio/build/esp32-s3-devkitm-1/partitions.bin \
     0xf000  .pio/build/esp32-s3-devkitm-1/ota_data_initial.bin \
     0x20000 .pio/build/esp32-s3-devkitm-1/firmware.bin
   ```

   If `esptool.py` is not on your PATH, use the copy bundled with PlatformIO at `~/.platformio/packages/tool-esptoolpy/esptool.py`.

4. **Commit, tag, and push:**

   ```bash
   git add -A
   git commit -m "Release v1.2.0"
   git tag v1.2.0
   git push origin main v1.2.0
   ```

5. **Create the GitHub release** with both binaries attached:

   ```bash
   gh release create v1.2.0 \
     .pio/build/esp32-s3-devkitm-1/firmware.bin \
     .pio/build/esp32-s3-devkitm-1/combined.bin \
     --title "v1.2.0" \
     --notes "Release notes here."
   ```

   Alternatively, create the release through the GitHub web UI and drag/drop both files.

### Release assets

Each release should contain two files:

| File | Purpose |
|------|---------|
| `firmware.bin` | OTA updates — devices already running this firmware download this file |
| `combined.bin` | First-time USB flash — includes bootloader, partition table, OTA data, and firmware in a single file |

## Flashing Pre-Built Firmware (USB)

If you have a `combined.bin` from a GitHub release and want to flash an ESP32-S3 for the first time without building from source:

### macOS / Linux

1. Install `esptool`:

   ```bash
   pip install esptool
   ```

2. Connect the ESP32-S3-DevKitM-1 via USB

3. Identify the serial port:
   - macOS: `/dev/tty.usbmodem*` or `/dev/cu.usbmodem*`
   - Linux: `/dev/ttyACM0` or `/dev/ttyUSB0`

4. Flash the combined binary:

   ```bash
   esptool.py --chip esp32s3 --port /dev/ttyACM0 write_flash 0x0 combined.bin
   ```

   Replace `/dev/ttyACM0` with your actual port.

5. Open a serial terminal at 115200 baud to access the CLI:

   ```bash
   screen /dev/ttyACM0 115200
   ```

   Or use PlatformIO's monitor: `pio device monitor`

### Windows

1. Install [Python](https://www.python.org/downloads/) if not already installed (check "Add to PATH" during install)

2. Install `esptool` from a Command Prompt or PowerShell:

   ```
   pip install esptool
   ```

3. Install the USB driver if needed. The ESP32-S3-DevKitM-1 uses a built-in USB-JTAG/CDC interface. Windows 10/11 typically recognises it automatically. If not, install the [ESP-IDF USB drivers](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/establish-serial-connection.html).

4. Connect the ESP32-S3-DevKitM-1 via USB and find the COM port:
   - Open **Device Manager** > **Ports (COM & LPT)**
   - Look for "USB Serial Device" or "USB JTAG/serial debug unit" — note the COM port (e.g., `COM3`)

5. Flash the combined binary:

   ```
   esptool.py --chip esp32s3 --port COM3 write_flash 0x0 combined.bin
   ```

   Replace `COM3` with your actual port.

6. Open a serial terminal at 115200 baud to access the CLI. Use [PuTTY](https://www.putty.org/) or the built-in Windows Terminal:
   - **PuTTY:** Connection type "Serial", Serial line `COM3`, Speed `115200`, click Open
   - **Windows Terminal / PowerShell:** `pio device monitor` (if PlatformIO is installed)

### Initial setup after flashing

Once at the `seq>` prompt:

```
seq> wifi config <ssid> <password>
seq> wifi connect
seq> ota repo owner/repo
seq> version
```

The device will auto-connect to WiFi on future boots. Subsequent firmware updates can be done over the air with `ota update latest`.

## Testing the REST API

An integration test suite in [tests/](tests/) exercises every REST and WebSocket endpoint against a live device.

### Prerequisites

Python 3.8+ and the test dependencies:

```bash
pip install -r tests/requirements.txt
```

This installs `pytest`, `requests`, and `websocket-client`.

### Running tests

All commands are run from the project root. Pass the device's IP address with `--device-ip`:

```bash
# Run all tests (destructive tests are skipped by default)
pytest tests/ --device-ip 192.168.4.1

# Run a specific test file
pytest tests/test_relay.py --device-ip 192.168.4.1

# Run a specific test class
pytest tests/test_relay.py::TestPostRelay --device-ip 192.168.4.1

# Include destructive tests (reboot, OTA, WiFi erase)
pytest tests/ --device-ip 192.168.4.1 --run-destructive

# Verbose output
pytest tests/ --device-ip 192.168.4.1 -v
```

### Test markers

Tests are tagged with markers defined in [tests/pytest.ini](tests/pytest.ini):

| Marker | Meaning |
|--------|---------|
| `write` | Modifies device state (relays, config, faults). State is restored after each test. |
| `destructive` | Reboots the device, triggers OTA, or erases WiFi credentials. Skipped unless `--run-destructive` is passed. |

### Test coverage

The suite contains 142 tests across 10 files:

| File | Endpoints | Tests |
|------|-----------|-------|
| `test_state.py` | `GET /api/state`, `GET /api/version` | 13 |
| `test_config.py` | `GET /api/config`, `POST /api/config`, save, defaults | 36 |
| `test_relay.py` | `POST /api/relay`, `POST /api/relay/name` | 16 |
| `test_fault.py` | `POST /api/fault/inject`, `POST /api/fault/clear` | 8 |
| `test_seq.py` | `POST /api/seq`, `POST /api/seq/apply` | 14 |
| `test_adc.py` | `GET /api/adc`, `GET /api/adc?ch=N` | 9 |
| `test_wifi.py` | WiFi status, config, scan, connect, auto, erase | 9 |
| `test_ota.py` | OTA status, repo, update, rollback, validate | 17 |
| `test_system.py` | `POST /api/log`, `POST /api/reboot` | 11 |
| `test_websocket.py` | `ws://<device>/ws` live state push | 9 |

### How the tests work

Tests run against a real device over the network — there are no mocks. The `conftest.py` provides a session-scoped `api` fixture (an `ApiClient` instance) with helpers that validate the JSON response envelope (`{"ok": true, "data": ...}` or `{"ok": false, "error": "..."}`):

- `api.get_ok(path)` — GET, assert 200, return `data`
- `api.post_ok(path, json=...)` — POST, assert 200, return `data`
- `api.post_error(path, json=..., expected_status=400)` — POST, assert error

Tests that modify state (marked `write`) restore original values in teardown. The `test_seq.py` file uses an autouse `restore_default_sequences` fixture to reset TX/RX sequences after each test.
