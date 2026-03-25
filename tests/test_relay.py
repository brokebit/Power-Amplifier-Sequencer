"""Tests for /api/relay endpoints."""

import pytest
import time


@pytest.mark.write
class TestPostRelay:
    """POST /api/relay — set relay on/off."""

    @pytest.mark.parametrize("relay_id", [1, 2, 3, 4, 5, 6])
    def test_relay_on_off(self, api, relay_id):
        """Turn each relay on, verify in state, turn off, verify."""
        # Turn on
        api.post_ok("/api/relay", json={"id": relay_id, "on": True})
        state = api.get_ok("/api/state")
        assert state["relays"][relay_id - 1] is True, f"Relay {relay_id} should be ON"

        # Turn off
        api.post_ok("/api/relay", json={"id": relay_id, "on": False})
        state = api.get_ok("/api/state")
        assert state["relays"][relay_id - 1] is False, f"Relay {relay_id} should be OFF"

    def test_invalid_relay_id_zero(self, api):
        error = api.post_error("/api/relay", json={"id": 0, "on": True})
        assert "relay" in error.lower() or "1-6" in error

    def test_invalid_relay_id_seven(self, api):
        error = api.post_error("/api/relay", json={"id": 7, "on": True})
        assert "relay" in error.lower() or "1-6" in error

    def test_missing_id_returns_error(self, api):
        error = api.post_error("/api/relay", json={"on": True})
        assert len(error) > 0

    def test_missing_on_returns_error(self, api):
        error = api.post_error("/api/relay", json={"id": 1})
        assert len(error) > 0


@pytest.mark.write
class TestPostRelayName:
    """POST /api/relay/name — set/clear relay alias."""

    def test_set_relay_name(self, api):
        """Set a name and verify it appears in state."""
        api.post_ok("/api/relay/name", json={"id": 1, "name": "TestName"})
        state = api.get_ok("/api/state")
        assert state["relay_names"][0] == "TestName"

        # Also check in config
        config = api.get_ok("/api/config")
        assert config["relay_names"][0] == "TestName"

    def test_clear_relay_name(self, api):
        """Set then clear a name."""
        api.post_ok("/api/relay/name", json={"id": 1, "name": "Temp"})
        api.post_ok("/api/relay/name", json={"id": 1, "name": ""})
        state = api.get_ok("/api/state")
        assert state["relay_names"][0] == ""

    def test_set_name_all_relays(self, api):
        """Set names on all 6 relays, then clear them."""
        for i in range(1, 7):
            api.post_ok("/api/relay/name", json={"id": i, "name": f"R{i}Test"})

        config = api.get_ok("/api/config")
        for i in range(6):
            assert config["relay_names"][i] == f"R{i+1}Test"

        # Clean up
        for i in range(1, 7):
            api.post_ok("/api/relay/name", json={"id": i, "name": ""})

    def test_name_truncated_at_15_chars(self, api):
        """Names longer than 15 chars should be truncated."""
        long_name = "A" * 20
        api.post_ok("/api/relay/name", json={"id": 1, "name": long_name})
        config = api.get_ok("/api/config")
        assert len(config["relay_names"][0]) <= 15

        # Clean up
        api.post_ok("/api/relay/name", json={"id": 1, "name": ""})

    def test_invalid_relay_id(self, api):
        error = api.post_error("/api/relay/name", json={"id": 0, "name": "Bad"})
        assert len(error) > 0

    def test_missing_id(self, api):
        error = api.post_error("/api/relay/name", json={"name": "Bad"})
        assert len(error) > 0
