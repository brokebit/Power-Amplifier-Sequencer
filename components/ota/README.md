# OTA

## Overview

The OTA component manages over-the-air firmware updates for the ESP32-S3 sequencer. It downloads firmware binaries over HTTPS -- either from GitHub Releases or an arbitrary URL -- writes them to the inactive OTA partition, and reboots into the new image. It also handles post-update boot validation, manual rollback to the previous firmware, and persists the GitHub repository identifier in NVS so that version-tag-based updates work without re-specifying the source each time.

The component is a pure library with no tasks or event loops of its own. It exposes a blocking `app_ota_update()` function that callers wrap as needed (the CLI calls it synchronously on the REPL task; the web server spawns a dedicated FreeRTOS task).

## Key Data Structures

### `ota_status_t`

Snapshot of the current OTA state, used by both the CLI `ota status` command and the `GET /api/ota/status` REST endpoint.

```c
typedef struct {
    char version[32];              // Running firmware version (from app descriptor)
    char running_partition[16];    // Partition label the app booted from (e.g. "ota_0")
    char boot_partition[16];       // Partition the bootloader will try next boot
    char next_update_partition[16];// Partition that would receive the next OTA image
    char app_state[24];            // OTA image state: VALID, PENDING_VERIFY, NEW, etc.
    char other_version[32];        // Version string in the other OTA slot, or "(empty)"
} ota_status_t;
```

### NVS Storage

The component uses its own NVS namespace `ota_cfg` with a single key:

| Key    | Type   | Description                                           |
|--------|--------|-------------------------------------------------------|
| `repo` | string | GitHub repository in `owner/repo` format (max 127 chars) |

### Partition Layout

The project's `partitions.csv` defines two 3 MB OTA slots (`ota_0` at 0x20000, `ota_1` at 0x320000) plus a 2-sector `otadata` partition that tracks which slot is active. The ESP-IDF bootloader alternates between slots on each successful update.

## Public API

All functions use the `app_ota_` prefix and are declared in `include/ota.h`.

| Function               | Description                                                                                              |
|------------------------|----------------------------------------------------------------------------------------------------------|
| `app_ota_init()`       | Call once at boot from `app_main()`. Marks the running image as valid if the bootloader left it in `PENDING_VERIFY` state. Idempotent -- safe on every boot. |
| `app_ota_get_version()`| Returns a pointer to the static firmware version string from the app descriptor.                         |
| `app_ota_get_status()` | Fills an `ota_status_t` with partition and version information.                                          |
| `app_ota_print_status()`| Human-readable status dump to stdout (CLI use).                                                         |
| `app_ota_set_repo()`   | Validates and stores a GitHub `owner/repo` string in NVS.                                                |
| `app_ota_get_repo()`   | Reads the stored repo string. Returns `ESP_ERR_NVS_NOT_FOUND` if not configured.                        |
| `app_ota_update()`     | Downloads and installs firmware. Blocks until complete. Reboots on success (does not return). See below.  |
| `app_ota_rollback()`   | Marks the current image invalid and reboots into the other OTA slot. Does not return on success.         |
| `app_ota_validate()`   | Manually marks the running image as valid, cancelling any pending rollback window.                       |

## Event Flow

### Update (`app_ota_update`)

The `target` parameter determines URL resolution:

1. **Full URL** (starts with `http`) -- used verbatim.
2. **`"latest"`** -- resolved to `https://github.com/{repo}/releases/latest/download/firmware.bin` using the NVS-stored repo.
3. **Version tag** (e.g. `"v1.2.0"`) -- resolved to `https://github.com/{repo}/releases/download/{tag}/firmware.bin`.

The update sequence:

```
WiFi connected? --no--> return ESP_ERR_INVALID_STATE
       |
      yes
       |
build_url() resolves target to HTTPS URL
       |
esp_https_ota_begin() -- opens connection, validates header
       |
esp_https_ota_perform() loop -- streams firmware to flash
  (prints progress every 10% if Content-Length is known,
   or every 100 KB if unknown)
       |
Verify complete data received
       |
esp_https_ota_finish() -- validates image, sets boot partition
       |
vTaskDelay(2s) --> esp_restart()
```

On any failure, the OTA handle is aborted and an error is returned without rebooting.

### Boot Validation (anti-bricking)

The bootloader's built-in rollback protection works with `app_ota_init()`:

1. After a successful OTA write, the new image is marked `PENDING_VERIFY` by `esp_https_ota_finish()`.
2. On the next boot, `app_ota_init()` is called after all critical initialization succeeds in `app_main()`.
3. If the image state is `PENDING_VERIFY`, it calls `esp_ota_mark_app_valid_cancel_rollback()`.
4. If `app_main()` crashes before reaching `app_ota_init()`, the watchdog fires and the bootloader automatically rolls back to the previous slot on the next boot.

This placement is intentional: the firmware only declares itself valid after the application has initialized far enough to be considered functional.

### Rollback

`app_ota_rollback()` checks that the other partition contains valid firmware (by reading its app descriptor), then calls `esp_ota_mark_app_invalid_rollback_and_reboot()`. The device reboots immediately into the previous image.

## Architecture Decisions

- **No background task**: The component deliberately avoids owning a FreeRTOS task. `app_ota_update()` blocks on the caller's task, which keeps the component simple and lets each consumer decide how to manage concurrency. The web server wraps it in an 8 KB task; the CLI runs it inline on the REPL task.

- **GitHub Releases as the update source**: Rather than requiring a custom firmware server, the component constructs download URLs from the standard GitHub Releases asset path (`/releases/download/{tag}/firmware.bin`). This means CI just needs to attach a `firmware.bin` asset to a release and devices can pull it by tag.

- **NVS-backed repo configuration**: The repo identifier is stored in NVS rather than compiled in. This allows a single firmware binary to be deployed across boards pointed at different forks or repositories, and lets the repo be changed at runtime via CLI or REST API without reflashing.

- **Repo format validation**: `app_ota_set_repo()` enforces the `owner/repo` format (exactly one `/`, no leading or trailing slashes, neither segment empty) at write time so that URL construction cannot produce malformed URLs later.

- **TLS via certificate bundle**: The HTTP client uses `esp_crt_bundle_attach` (the ESP-IDF x509 certificate bundle) rather than pinning a specific CA. This covers GitHub's certificate chain without requiring manual certificate management but does increase the binary size.

- **Conservative buffer sizes**: Both the receive and transmit HTTP buffers are set to 1024 bytes. On a memory-constrained ESP32-S3 that is simultaneously running a web server, sequencer FSM, and sensor monitoring, this keeps the OTA download's RAM footprint predictable at the cost of slightly reduced throughput.

- **Progress reporting to stdout**: Progress is printed directly to stdout rather than through a callback or event mechanism. This works well for the CLI use case. The web server's update endpoint returns `{"status": "started"}` immediately and relies on the device rebooting as the implicit completion signal.

## Dependencies

### ESP-IDF Components

| Component            | Purpose                                           |
|----------------------|---------------------------------------------------|
| `esp_https_ota`      | High-level OTA over HTTPS with streaming flash writes |
| `esp_http_client`    | HTTP/HTTPS client (underlying transport)          |
| `app_update`         | Partition state management and rollback API        |
| `nvs_flash`          | Non-volatile storage for repo configuration        |
| `esp_partition`      | Partition table queries                            |
| `bootloader_support` | Boot partition selection                           |
| `mbedtls`            | TLS for HTTPS connections                          |

### Internal Components

| Component   | Purpose                                            |
|-------------|----------------------------------------------------|
| `wifi_sta`  | `app_wifi_is_connected()` -- gate updates on active WiFi connection |

### Consumers

| Consumer      | Integration                                                     |
|---------------|-----------------------------------------------------------------|
| `main.c`      | Calls `app_ota_init()` during startup after critical init       |
| `cli/cmd_ota` | Registers `ota` CLI command with subcommands: `status`, `repo`, `update`, `rollback`, `validate` |
| `web_server/api_ota` | REST endpoints under `/api/ota/` -- status, repo get/set, update (async), rollback, validate |

## Usage Notes

- **WiFi must be connected** before calling `app_ota_update()`. The function checks this upfront and returns `ESP_ERR_INVALID_STATE` with a user-facing error message if not.

- **`app_ota_update()` does not return on success**. It calls `esp_restart()` after a 2-second delay. Callers should treat any return from this function as a failure.

- **The repo must be configured before tag-based updates**. If `app_ota_update("latest")` or `app_ota_update("v1.0.0")` is called without a repo set, it returns `ESP_ERR_NVS_NOT_FOUND`. Full URLs bypass this requirement entirely.

- **The firmware binary must be named `firmware.bin`** in the GitHub Release assets. This filename is hardcoded in the URL construction logic.

- **Rollback requires a valid image in the other slot**. On a fresh device with only one partition ever written, `app_ota_rollback()` will fail because the other slot has no valid app descriptor.

- **The 15-second receive timeout** (`OTA_RECV_TIMEOUT`) applies per HTTP chunk, not to the total download. Slow but steady connections will succeed; stalled connections will time out.
