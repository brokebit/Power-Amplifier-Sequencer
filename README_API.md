# Web Interface & REST API

When connected to WiFi, the device runs an HTTP server on port 80. Open `http://<device-ip>/` in a browser to see the live dashboard, or use the REST API with `curl` or any HTTP client.

## Live State (WebSocket)

Connect to `ws://<device-ip>/ws` for a live JSON stream pushed every 500 ms. The frame format matches `GET /api/state`, including chip 0 ADC readings (`adc_0_ch0`–`adc_0_ch3`) and channel names (`adc_0_ch_names`). The dashboard page (`/`) uses this automatically.

## REST Endpoints

All responses use the envelope `{"ok": true, "data": {...}}` or `{"ok": false, "error": "msg"}`.

### State & System
| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/state` | System state snapshot (PTT, sequencer, relays, power, SWR, temps, chip 0 ADC, WiFi) |
| GET | `/api/version` | Firmware version, IDF version, chip info |
| POST | `/api/reboot` | Reboot the device (2s delay) |
| POST | `/api/log` | Set log level — body: `{"level": "info", "tag": "monitor"}` |

### Configuration
| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/config` | Full config dump (thresholds, cal factors, per-channel dividers, ADC channel names, sequences, relay names) |
| POST | `/api/config` | Set a config key — body: `{"key": "swr_threshold", "value": "2.5"}` |
| POST | `/api/config/save` | Persist current config to NVS |
| POST | `/api/config/defaults` | Reset config to factory defaults (in memory only) |

### Relays & Sequencer
| Method | Path | Description |
|--------|------|-------------|
| POST | `/api/relay` | Set relay — body: `{"id": 1, "on": true}` |
| POST | `/api/relay/name` | Set relay alias — body: `{"id": 2, "name": "PA"}` |
| POST | `/api/fault/clear` | Clear latched fault, return to RX |
| POST | `/api/fault/inject` | Inject test fault — body: `{"type": "swr"}` |
| POST | `/api/seq` | Set sequence — body: `{"direction": "tx", "steps": [{"relay_id": 3, "state": true, "delay_ms": 50}]}` |
| POST | `/api/seq/apply` | Apply current config to running sequencer (must be in RX state) |

### ADC
| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/adc` | Read all 4 chip 1 ADC channels (voltage) |
| GET | `/api/adc?ch=0` | Read a single chip 1 channel (0-3) |
| POST | `/api/adc/name` | Set chip 0 channel name — body: `{"ch": 0, "name": "MySensor"}` (null to clear) |

### WiFi
| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/wifi/status` | Connection state, IP, RSSI, auto-connect setting |
| POST | `/api/wifi/config` | Set credentials — body: `{"ssid": "...", "password": "..."}` |
| POST | `/api/wifi/connect` | Connect using saved credentials |
| POST | `/api/wifi/disconnect` | Disconnect from AP |
| GET | `/api/wifi/scan` | Scan for available networks (blocks 1-3s) |
| POST | `/api/wifi/auto` | Enable/disable auto-connect — body: `{"enabled": true}` |
| POST | `/api/wifi/erase` | Erase saved WiFi credentials |

### OTA
| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/ota/status` | Running partition, version, app state, other slot info |
| GET | `/api/ota/repo` | Show configured GitHub repo |
| POST | `/api/ota/repo` | Set GitHub repo — body: `{"repo": "owner/repo"}` |
| POST | `/api/ota/update` | Start OTA update (async) — body: `{"target": "latest"}` |
| POST | `/api/ota/rollback` | Revert to previous firmware and reboot |
| POST | `/api/ota/validate` | Manually mark current firmware as valid |

## Examples

```bash
# Get live state
curl http://192.168.1.100/api/state

# Toggle a relay
curl -X POST http://192.168.1.100/api/relay -d '{"id":2,"on":true}'

# Update a config parameter
curl -X POST http://192.168.1.100/api/config -d '{"key":"swr_threshold","value":"2.5"}'

# Save config to NVS
curl -X POST http://192.168.1.100/api/config/save

# Trigger OTA update
curl -X POST http://192.168.1.100/api/ota/update -d '{"target":"latest"}'

# Set a chip 0 ADC channel name
curl -X POST http://192.168.1.100/api/adc/name -d '{"ch":0,"name":"Pressure"}'

# Set a per-channel resistor divider value
curl -X POST http://192.168.1.100/api/config -d '{"key":"adc_0a_r_top","value":"10000"}'
```
