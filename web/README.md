# PA Sequencer — Web Interface

## Project Structure

```
web/
  build/
    build.sh            # Master build script
    tailwind.config.js  # Tailwind CSS configuration
    input.css           # Tailwind input directives
    .bin/               # Auto-downloaded tools (gitignored)
  static/
    index.html          # Source HTML (page skeleton, all markup)
    js/
      app.js            # Entry point, tab switching, API helpers, toast
      dashboard.js      # PTT badge, fault banner, WiFi icon updates
      i18n.js           # Internationalization (JSON language files)
      seq-editor.js     # TX/RX sequence editor with SortableJS
      config.js         # Thresholds, calibration, relay names, OTA, system
      wifi.js           # WiFi status, credentials, scan, connect/disconnect
      theme.js          # Theme switching (light/dark, localStorage)
      ws.js             # WebSocket manager with reconnect & listeners
    lang/
      en.json           # English UI strings (~85 keys)
    lib/                # Vendored JS libraries (checked into git)
    themes/
      dark.css          # Dark theme (default) — CSS custom properties
      light.css         # Light theme — CSS custom properties
data/                   # Build output → SPIFFS image
```

- **`web/static/`** — Source files, edited by developers
- **`web/build/`** — Build tooling and configuration
- **`data/`** — Build output directory, flashed to the ESP32's SPIFFS partition

## Prerequisites

- `curl` — for downloading the Tailwind CLI (first run only)
- `gzip` — for compressing build output

No Node.js or npm required. The Tailwind CSS standalone CLI binary is
auto-downloaded on first build.

## Building

```sh
./web/build/build.sh
```

The script will:

1. Download the Tailwind CLI to `web/build/.bin/` if not already present
2. Clean the `data/` directory
3. Build CSS with Tailwind (purges unused classes, minifies)
4. Copy static assets (HTML, JS, libraries, language files, themes)
5. Gzip all files with maximum compression
6. Print a size report

## Flashing to Device

Use PlatformIO's built-in SPIFFS upload:

```sh
pio run -t uploadfs
```

This packages the `data/` directory into a SPIFFS image and flashes it
to the `storage` partition.

## Updating Vendored Libraries

Libraries in `web/static/lib/` are checked into git. To update:

1. Download the new version from the project's GitHub releases page
2. Replace the file in `web/static/lib/`
3. Update the version comment at the top of the file
4. Commit

| Library    | File                  | Source |
|------------|-----------------------|--------|
| Chart.js   | `lib/chart.min.js`    | [GitHub Releases](https://github.com/chartjs/Chart.js/releases) — download `chart.umd.min.js` |
| SortableJS | `lib/sortable.min.js` | [GitHub Releases](https://github.com/SortableJS/Sortable/releases) — download `Sortable.min.js` |

## JavaScript Modules

Scripts load in order via `<script>` tags (no bundler):

1. **`lib/chart.min.js`** — Chart.js library (loaded first, used by dashboard)
2. **`lib/sortable.min.js`** — SortableJS library (used by sequence editor)
3. **`i18n.js`** — exposes `window.I18n` (`init`, `load`, `t`, `apply`)
4. **`theme.js`** — exposes `window.Theme` (`init`, `toggle`, `current`)
5. **`ws.js`** — exposes `window.WS` (`init`, `addListener`, `getState`,
   `connectionState`)
6. **`dashboard.js`** — exposes `window.Dashboard` (`init`, `loadConfig`)
7. **`seq-editor.js`** — exposes `window.SeqEditor` (`init`, `loadConfig`)
8. **`config.js`** — exposes `window.Config` (`init`, `loadConfig`)
9. **`wifi.js`** — exposes `window.WiFi` (`init`, `loadStatus`)
10. **`app.js`** — exposes `window.App` (`switchTab`, `apiGet`, `apiPost`,
    `toast`); coordinates init on `DOMContentLoaded`

`app.js` is the entry point. On DOM ready it initializes all modules in
order: Theme → I18n → tabs → WS → Dashboard → SeqEditor → Config → WiFi.

## Dashboard Widgets

All dashboard widgets receive data from the WebSocket state push
(500ms server interval). Chart redraws are throttled to ~2 Hz.

### Relay Status Row
- 6 toggle buttons in a grid, labeled from `relay_names[]` or "Relay N"
- Interactive only when `seq_state === "RX"`, otherwise disabled + dimmed
- Click sends `POST /api/relay { id, on }` with optimistic UI (reverts on error)

### Power Meters (FWD / REF)
- Chart.js horizontal bar charts with numeric readout (e.g. "125.3 W")
- Auto-scaling: tracks peak value with slow decay, sets max to peak × 1.2
  rounded to a clean number

### SWR Meter
- Chart.js horizontal bar chart, range 1.0 to max
- Color zones: green (< 2.0), yellow (< SWR threshold), red (≥ threshold)
- SWR threshold fetched from `GET /api/config` on page load

### Temperature Chart
- Chart.js line chart, two datasets (Temp 1, Temp 2)
- History dropdown: 5 min / 15 min (default) / 30 min / 1 hour
- Circular buffer stores up to 1 hour of data (7200 points at 500ms)
- Changing the history duration adjusts the visible window; data outside
  the window is retained so switching back shows it

## Sequence Editor

The sequence editor (`seq-editor.js`) provides side-by-side TX and RX
step editors on the Configuration tab.

### Step Rows

Each step has:
- **Drag handle** (≡) — reorder via SortableJS (handle-only drag)
- **Relay dropdown** — options 1–6, labeled with custom relay names
- **State dropdown** — ON / OFF
- **Delay input** — 0–10000 ms
- **Delete button** (×)

### SortableJS Integration

`Sortable.create()` is initialized on each step list container with
`handle: '.seq-handle'`. The `onEnd` callback rebuilds the backing
array from the new DOM order and re-renders the list.

### Add Step

Appends `{ relay_id: 1, state: false, delay_ms: 0 }`. Button is
disabled when the step count reaches 8 (`SEQ_MAX_STEPS`).

### Pending Changes

After any edit (add, delete, reorder, field change), the current step
arrays are JSON-serialized and compared to the `applied*` snapshots
taken at last load/apply. A yellow "Unsaved changes" indicator appears
when they differ.

### Apply Workflow

1. `POST /api/seq` with `{ direction: "tx", steps: [...] }`
2. `POST /api/seq` with `{ direction: "rx", steps: [...] }`
3. `POST /api/seq/apply`

The Apply button is disabled unless:
- There are pending changes, AND
- The sequencer is in RX state (tracked via WebSocket listener)

On success, the applied snapshots are updated and the pending indicator
clears. On error, a toast shows the server error message.

### Data Loading

`SeqEditor.loadConfig()` is called each time the user switches to the
Configuration tab. It fetches `GET /api/config` and populates both step
lists from `tx_steps` and `rx_steps`. Relay names for the dropdowns are
kept in sync via the WebSocket state push.

## Configuration Settings

The config module (`config.js`) manages the Thresholds & Calibration
section and Relay Names section on the Configuration tab.

### Edit-on-Blur Pattern

Each threshold/calibration input sends `POST /api/config` with
`{ key, value }` on blur (or Enter). On success the input border
flashes green briefly; on error it flashes red and a toast shows the
server error message. No explicit "save field" button — edits are
submitted individually as the user moves between fields.

### Field Groups

Fields are organized into four sub-sections within Thresholds &
Calibration, each with its own heading:

- **Fault Thresholds** — SWR threshold, Temp 1/2 thresholds
- **PA Control** — PA relay ID (1–6)
- **Power Calibration** — FWD/REF cal factors
- **Thermistor Calibration** — Beta, R0, R Series

### Relay Names

Six text inputs (max 15 chars each). On blur, sends
`POST /api/relay/name { id, name }`. An empty field sends `name: null`
to clear the custom name. Relay name changes propagate to:
- Dashboard relay labels (via next WS push)
- Sequence editor relay dropdowns (via WS listener + next loadConfig)

### Save to NVS / Reset to Defaults

- **Save to NVS** — `POST /api/config/save`, success toast
- **Reset to Defaults** — themed confirm dialog, then
  `POST /api/config/defaults`. On success, reloads both the config
  fields and the sequence editor.

### Collapsible Sections

All config tab sections (Sequences, Thresholds, Relay Names, WiFi, OTA,
System) have clickable headings that collapse/expand the section content.
Collapsed state is persisted in `localStorage` (`pa-seq-collapsed` key).
All sections default to expanded on first visit.

## WiFi Management

The WiFi module (`wifi.js`) handles the WiFi section on the
Configuration tab.

### Status Display

Shows a connected/disconnected badge, IP address, and RSSI with signal
strength bars. Status updates live via the WebSocket state push and
refreshes from `GET /api/wifi/status` each time the config tab opens.

### Credentials & Connect

SSID and password inputs with a show/hide toggle on the password field.
"Connect" button calls `POST /api/wifi/config` then
`POST /api/wifi/connect` in sequence.

### Network Scan

"Scan Networks" button calls `GET /api/wifi/scan`. Results display in a
table with SSID, signal bars (derived from RSSI), channel, and
open/secured icon. Clicking a row populates the SSID field.

### Additional Controls

- **Auto-connect toggle** — `POST /api/wifi/auto { enabled: bool }`
- **Disconnect** — `POST /api/wifi/disconnect` (visible when connected)
- **Erase Credentials** — themed confirm dialog, then
  `POST /api/wifi/erase`

## OTA Update

The OTA section (in `config.js`) shows firmware version, running
partition, and app state from `GET /api/ota/status`.

- **Repository field** — editable, sends `POST /api/ota/repo` on blur.
  Loaded from `GET /api/ota/repo`.
- **Update to Latest** — `POST /api/ota/update { target: "latest" }`.
  Fire-and-forget: button disables and shows "Update started" message.
  The device reboots automatically on success.

## System

The System section (in `config.js`) shows firmware info from
`GET /api/version` (project name, version, IDF version, CPU cores).

- **Log level** — dropdown (Off/Error/Warn/Info/Debug/Verbose) with
  optional tag input. Sends `POST /api/log { level, tag }` on change.
- **Reboot** — themed confirm dialog, then `POST /api/reboot`.

## WebSocket Connection

`ws.js` manages the WebSocket connection to `ws://<host>/ws`:

- Server pushes state JSON every 500ms (no client polling)
- On disconnect: exponential backoff reconnect (1s → 2s → 4s ... → 30s cap)
- Status dot in header: green (connected), amber (reconnecting), red (disconnected)
- **Listener pattern**: modules call `WS.addListener(callback)` to receive
  state updates. The callback receives the parsed JSON object. If state is
  already available when a listener registers, it gets an immediate delivery.
- `WS.getState()` returns the last received state object (or `null`)

## API Helpers

`App.apiGet(url)` and `App.apiPost(url, body)` are convenience wrappers
around `fetch()`. The server wraps all responses in `{"ok": true, "data": {...}}`
envelopes. `apiGet` automatically unwraps the `data` field, so callers
receive the inner object directly. Both reject with an `Error` whose
message is the server's error string.

`App.toast(message, type)` shows a brief notification at the top-right.
Types: `'error'` (red, 8s), `'success'` (green, 5s), `'info'` (blue, 5s).
Click any toast to dismiss it immediately.

`App.confirm(message)` shows a themed modal dialog with Cancel/Confirm
buttons. Returns a promise resolving to `true` (confirmed) or `false`
(cancelled). Escape key and clicking outside the modal also cancel.
Used by: Reset to Defaults, Erase WiFi Credentials, Reboot.

## Theming

Themes are CSS files that define `--color-*` custom properties on `:root`.
Tailwind references these variables through the extended color palette in
`tailwind.config.js`, so theme switching works without rebuilding CSS.

- **Dark** (`themes/dark.css`) — default, matches radio equipment convention
- **Light** (`themes/light.css`) — light backgrounds, adjusted state colors
- Selected theme stored in `localStorage` (`pa-seq-theme` key)
- Toggle via sun/moon icon in the header bar

### Adding a Custom Theme

1. Create `web/static/themes/mytheme.css` defining all `--color-*` variables
2. Add a `<link>` tag in `index.html` with `id="theme-mytheme"` and `disabled`
3. Add `'mytheme'` to the `THEMES` array in `theme.js`
4. Rebuild and deploy

## Internationalization (i18n)

All UI strings live in JSON files under `web/static/lang/`. The i18n module
scans `[data-i18n]` attributes and replaces `textContent` with the matching
key. Inline English text in the HTML serves as fallback if the language file
fails to load.

- `I18n.t(key)` — lookup a string in JS code
- `I18n.apply()` — re-scan DOM and apply translations (call after dynamic HTML)
- Language stored in `localStorage` (`pa-seq-lang` key)

### Adding a Language

1. Copy `web/static/lang/en.json` to `web/static/lang/xx.json`
2. Translate all values (keys stay the same)
3. Rebuild and deploy — no code changes needed

### String Key Conventions

Keys use dot notation grouped by section:
`dashboard.*`, `config.*`, `wifi.*`, `ota.*`, `system.*`, `fault.*`,
`general.*`, `error.*`, `confirm.*`

## Error Handling & Connection State

### Connection Lost Banner

A full-width warning banner ("Connection lost — retrying...") appears
below the header when the WebSocket disconnects. It hides automatically
when the connection is re-established.

### Stale Data

When the WebSocket disconnects, the dashboard content gets a `.stale`
CSS class that reduces opacity and disables pointer events. A "Xs ago"
timestamp appears next to the WS status dot, updating every second. On
reconnect the stale state clears and normal rendering resumes.

### API Error Handling

All API calls use `App.apiGet`/`App.apiPost` which reject on non-OK
responses. Callers show error details via `App.toast(err.message, 'error')`.
Form state is never cleared on error — the user can retry.

## How Gzip Serving Works

The firmware's static file handler (`api_static.c`) checks for a `.gz`
version of each requested file:

1. Browser requests `/js/app.js`
2. Server checks if `/www/js/app.js.gz` exists on SPIFFS
3. If found and the browser sends `Accept-Encoding: gzip`, the `.gz` file
   is served with `Content-Encoding: gzip`
4. The browser transparently decompresses it
5. If no `.gz` file exists, the original file is served normally
