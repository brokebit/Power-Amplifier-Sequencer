# Release Notes

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
| `components/sequencer/sequencer.c` | PTT reconcile loop, RX sequence on fault clear, `sequencer_config_matches()` |
| `components/sequencer/include/sequencer.h` | Added `sequencer_config_matches()` declaration |
| `components/sequencer/CMakeLists.txt` | Added `driver` and `hw_config` to REQUIRES for GPIO read |
| `components/config/config.c` | Config mutex, internal locking, `pa_relay` range uses `HW_RELAY_COUNT` |
| `components/config/include/config.h` | Added `config_lock()`/`config_unlock()`, `SEQ_MAX_DELAY_MS` |
| `components/config/CMakeLists.txt` | Added `freertos` to REQUIRES |
| `components/monitor/monitor.c` | Lock around `memcpy` in `monitor_update_config()` |
| `components/cli/cmd_seq.c` | Config lock on step writes, relay/delay validation uses constants |
| `components/cli/cmd_relay.c` | Config lock on relay name writes |
| `components/web_server/api_config.c` | Snapshot copy under lock for GET, added `pending_apply` |
| `components/web_server/api_seq.c` | Config lock on step writes, relay/delay validation uses constants |
| `components/web_server/api_relay.c` | Config lock on name writes, relay ID error messages use `HW_RELAY_COUNT` |
| `tests/test_config.py` | `RELAY_COUNT` constant, `pending_apply` field and lifecycle tests |
| `tests/test_state.py` | `RELAY_COUNT` constant for array length checks |
| `tests/test_relay.py` | `RELAY_COUNT` constant, parametrize from constant, robust error assertions |
| `tests/test_seq.py` | Keyword-based error assertions instead of literal substrings |
| `tests/test_fault.py` | New `test_clear_restores_rx_relay_state` regression test |
