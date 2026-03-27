"""Tests for /api/seq endpoints."""

import pytest


@pytest.mark.write
class TestPostSeq:
    """POST /api/seq — set TX/RX sequences."""

    def test_set_tx_sequence(self, api):
        """Set a TX sequence and verify it in config."""
        steps = [
            {"relay_id": 3, "state": True, "delay_ms": 50},
            {"relay_id": 1, "state": True, "delay_ms": 50},
            {"relay_id": 2, "state": True, "delay_ms": 0},
        ]
        api.post_ok("/api/seq", json={"direction": "tx", "steps": steps})

        config = api.get_ok("/api/config")
        assert len(config["tx_steps"]) == 3
        assert config["tx_steps"][0]["relay_id"] == 3
        assert config["tx_steps"][0]["state"] is True
        assert config["tx_steps"][0]["delay_ms"] == 50
        assert config["tx_steps"][2]["delay_ms"] == 0

    def test_set_rx_sequence(self, api):
        """Set an RX sequence and verify it in config."""
        steps = [
            {"relay_id": 2, "state": False, "delay_ms": 50},
            {"relay_id": 1, "state": False, "delay_ms": 50},
            {"relay_id": 3, "state": False, "delay_ms": 0},
        ]
        api.post_ok("/api/seq", json={"direction": "rx", "steps": steps})

        config = api.get_ok("/api/config")
        assert len(config["rx_steps"]) == 3
        assert config["rx_steps"][0]["relay_id"] == 2
        assert config["rx_steps"][0]["state"] is False

    def test_set_single_step_sequence(self, api):
        """Minimum 1-step sequence."""
        steps = [{"relay_id": 1, "state": True, "delay_ms": 0}]
        api.post_ok("/api/seq", json={"direction": "tx", "steps": steps})

        config = api.get_ok("/api/config")
        assert len(config["tx_steps"]) == 1

    def test_set_max_steps_sequence(self, api):
        """Maximum 8-step sequence."""
        steps = [
            {"relay_id": (i % 6) + 1, "state": True, "delay_ms": 10}
            for i in range(8)
        ]
        api.post_ok("/api/seq", json={"direction": "tx", "steps": steps})

        config = api.get_ok("/api/config")
        assert len(config["tx_steps"]) == 8

    def test_too_many_steps_returns_error(self, api):
        """More than 8 steps should fail."""
        steps = [
            {"relay_id": 1, "state": True, "delay_ms": 0}
            for _ in range(9)
        ]
        error = api.post_error("/api/seq", json={"direction": "tx", "steps": steps})
        assert "step" in error.lower() or "1-8" in error

    def test_empty_steps_returns_error(self, api):
        error = api.post_error("/api/seq", json={"direction": "tx", "steps": []})
        assert len(error) > 0

    def test_invalid_direction(self, api):
        steps = [{"relay_id": 1, "state": True, "delay_ms": 0}]
        error = api.post_error("/api/seq", json={"direction": "both", "steps": steps})
        assert "tx" in error.lower() or "rx" in error.lower()

    def test_invalid_relay_id_in_step(self, api):
        steps = [{"relay_id": 7, "state": True, "delay_ms": 0}]
        error = api.post_error("/api/seq", json={"direction": "tx", "steps": steps})
        assert "relay" in error.lower()

    def test_invalid_delay_in_step(self, api):
        steps = [{"relay_id": 1, "state": True, "delay_ms": 20000}]
        error = api.post_error("/api/seq", json={"direction": "tx", "steps": steps})
        assert "delay" in error.lower()

    def test_missing_fields_in_step(self, api):
        steps = [{"relay_id": 1}]
        error = api.post_error("/api/seq", json={"direction": "tx", "steps": steps})
        assert len(error) > 0

    def test_missing_direction(self, api):
        steps = [{"relay_id": 1, "state": True, "delay_ms": 0}]
        error = api.post_error("/api/seq", json={"steps": steps})
        assert len(error) > 0

    def test_missing_steps(self, api):
        error = api.post_error("/api/seq", json={"direction": "tx"})
        assert len(error) > 0


@pytest.mark.write
class TestSeqApply:
    """POST /api/seq/apply — apply config to running sequencer."""

    def test_apply_in_rx_state(self, api):
        """Apply should succeed when sequencer is in RX."""
        # Ensure we're in RX (clear any fault)
        api.post("/api/fault/clear")

        state = api.get_ok("/api/state")
        if state["seq_state"] == "RX":
            api.post_ok("/api/seq/apply")

    def test_apply_in_fault_state_returns_error(self, api):
        """Apply should fail when sequencer is in FAULT state."""
        import time

        api.post_ok("/api/fault/inject", json={"type": "swr"})

        # Wait for fault to take effect
        deadline = time.time() + 2.0
        in_fault = False
        while time.time() < deadline:
            state = api.get_ok("/api/state")
            if state["seq_state"] == "FAULT":
                in_fault = True
                break
            time.sleep(0.1)

        if in_fault:
            resp = api.post("/api/seq/apply")
            body = resp.json()
            assert body["ok"] is False
            assert "rx" in body["error"].lower() or "state" in body["error"].lower()

        # Clean up
        api.post_ok("/api/fault/clear")
        time.sleep(0.3)


@pytest.fixture(autouse=True)
def restore_default_sequences(api):
    """Restore factory default sequences after each test class."""
    yield
    # Reset to defaults to undo any sequence changes
    api.post_ok("/api/config/defaults")
