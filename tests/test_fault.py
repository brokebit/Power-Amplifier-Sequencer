"""Tests for /api/fault endpoints."""

import pytest
import time


def wait_for_state(api, expected_state, timeout=2.0):
    """Poll /api/state until seq_state matches or timeout."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        state = api.get_ok("/api/state")
        if state["seq_state"] == expected_state:
            return state
        time.sleep(0.1)
    return api.get_ok("/api/state")


@pytest.mark.write
class TestFaultInject:
    """POST /api/fault/inject — inject test faults."""

    @pytest.mark.parametrize("fault_type,expected_fault", [
        ("swr", "HIGH_SWR"),
        ("temp1", "OVER_TEMP1"),
        ("temp2", "OVER_TEMP2"),
        ("emergency", "EMERGENCY"),
    ])
    def test_inject_fault(self, api, fault_type, expected_fault):
        """Inject each fault type and verify the sequencer enters FAULT state."""
        # Ensure clean state first
        api.post("/api/fault/clear")
        time.sleep(0.2)

        api.post_ok("/api/fault/inject", json={"type": fault_type})
        state = wait_for_state(api, "FAULT")

        assert state["seq_state"] == "FAULT", (
            f"Expected FAULT after injecting {fault_type}, got {state['seq_state']}"
        )
        assert state["seq_fault"] == expected_fault

        # Clean up
        api.post_ok("/api/fault/clear")
        wait_for_state(api, "RX")

    def test_inject_invalid_type(self, api):
        error = api.post_error("/api/fault/inject", json={"type": "bogus"})
        assert "swr" in error.lower() or "type" in error.lower()

    def test_inject_missing_type(self, api):
        error = api.post_error("/api/fault/inject", json={})
        assert len(error) > 0


@pytest.mark.write
class TestFaultClear:
    """POST /api/fault/clear — clear latched fault."""

    def test_clear_after_inject(self, api):
        """Inject a fault, then clear it and verify return to RX."""
        api.post_ok("/api/fault/inject", json={"type": "swr"})
        wait_for_state(api, "FAULT")

        api.post_ok("/api/fault/clear")
        state = wait_for_state(api, "RX")

        assert state["seq_state"] == "RX"
        assert state["seq_fault"] == "none"

    def test_clear_when_not_in_fault(self, api):
        """Clear when already in RX — may return error or succeed as no-op."""
        # Make sure we're in RX first
        api.post("/api/fault/clear")
        wait_for_state(api, "RX")

        resp = api.post("/api/fault/clear")
        body = resp.json()
        # Should either succeed (no-op) or return 409 "not in FAULT state"
        if not body["ok"]:
            assert "fault" in body["error"].lower() or "state" in body["error"].lower()
