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
2. **`i18n.js`** — exposes `window.I18n` (`init`, `load`, `t`, `apply`)
3. **`theme.js`** — exposes `window.Theme` (`init`, `toggle`, `current`)
4. **`ws.js`** — exposes `window.WS` (`init`, `addListener`, `getState`,
   `connectionState`)
5. **`dashboard.js`** — exposes `window.Dashboard` (`init`, `loadConfig`)
6. **`app.js`** — exposes `window.App` (`switchTab`, `apiGet`, `apiPost`,
   `toast`); coordinates init on `DOMContentLoaded`

`app.js` is the entry point. On DOM ready it initializes all modules in
order: Theme → I18n → tabs → WS → Dashboard.

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
around `fetch()`. They return a promise resolving to the parsed JSON response,
or reject with an `Error` whose message is the server's error string.

`App.toast(message, type)` shows a brief notification at the bottom-right.
Types: `'error'` (red), `'success'` (green), `'info'` (blue). Auto-dismisses
after 5 seconds.

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

## How Gzip Serving Works

The firmware's static file handler (`api_static.c`) checks for a `.gz`
version of each requested file:

1. Browser requests `/js/app.js`
2. Server checks if `/www/js/app.js.gz` exists on SPIFFS
3. If found and the browser sends `Accept-Encoding: gzip`, the `.gz` file
   is served with `Content-Encoding: gzip`
4. The browser transparently decompresses it
5. If no `.gz` file exists, the original file is served normally
