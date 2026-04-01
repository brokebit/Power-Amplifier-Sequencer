"""Tests for /api/config endpoints."""

import pytest

# Must match HW_RELAY_COUNT in hw_config.h
RELAY_COUNT = 6


class TestGetConfig:
    """GET /api/config — full config dump."""

    def test_returns_ok(self, api):
        data = api.get_ok("/api/config")
        assert data is not None

    def test_has_thresholds(self, api):
        data = api.get_ok("/api/config")
        assert isinstance(data["swr_threshold"], (int, float))
        assert isinstance(data["temp1_threshold"], (int, float))
        assert isinstance(data["temp2_threshold"], (int, float))

    def test_has_pa_relay(self, api):
        data = api.get_ok("/api/config")
        assert isinstance(data["pa_relay"], int)
        assert 1 <= data["pa_relay"] <= RELAY_COUNT

    def test_has_power_calibration(self, api):
        data = api.get_ok("/api/config")
        for key in ("fwd_slope", "fwd_intercept", "fwd_coupling", "fwd_atten",
                     "ref_slope", "ref_intercept", "ref_coupling", "ref_atten",
                     "adc_r_top", "adc_r_bottom"):
            assert isinstance(data[key], (int, float)), f"{key} missing or wrong type"

    def test_has_thermistor_params(self, api):
        data = api.get_ok("/api/config")
        assert isinstance(data["therm_beta"], (int, float))
        assert isinstance(data["therm_r0"], (int, float))
        assert isinstance(data["therm_rseries"], (int, float))

    def test_has_relay_names(self, api):
        data = api.get_ok("/api/config")
        assert isinstance(data["relay_names"], list)
        assert len(data["relay_names"]) == RELAY_COUNT

    def test_has_tx_steps(self, api):
        data = api.get_ok("/api/config")
        assert isinstance(data["tx_steps"], list)
        for step in data["tx_steps"]:
            assert "relay_id" in step
            assert "state" in step
            assert "delay_ms" in step

    def test_has_rx_steps(self, api):
        data = api.get_ok("/api/config")
        assert isinstance(data["rx_steps"], list)
        for step in data["rx_steps"]:
            assert "relay_id" in step
            assert "state" in step
            assert "delay_ms" in step

    def test_has_pending_apply(self, api):
        data = api.get_ok("/api/config")
        assert "pending_apply" in data
        assert isinstance(data["pending_apply"], bool)


@pytest.mark.write
class TestPostConfig:
    """POST /api/config — set individual config keys."""

    def test_set_swr_threshold(self, api):
        """Set swr_threshold and verify it changed."""
        # Read original
        original = api.get_ok("/api/config")["swr_threshold"]

        # Set to a different value
        new_val = 2.5 if original != 2.5 else 3.5
        data = api.post_ok("/api/config", json={"key": "swr_threshold", "value": str(new_val)})
        assert data["key"] == "swr_threshold"

        # Verify it changed
        updated = api.get_ok("/api/config")["swr_threshold"]
        assert abs(updated - new_val) < 0.01

        # Restore original
        api.post_ok("/api/config", json={"key": "swr_threshold", "value": str(original)})

    def test_set_temp1_threshold(self, api):
        original = api.get_ok("/api/config")["temp1_threshold"]
        new_val = 70.0 if original != 70.0 else 65.0
        api.post_ok("/api/config", json={"key": "temp1_threshold", "value": str(new_val)})
        updated = api.get_ok("/api/config")["temp1_threshold"]
        assert abs(updated - new_val) < 0.01
        api.post_ok("/api/config", json={"key": "temp1_threshold", "value": str(original)})

    def test_set_temp2_threshold(self, api):
        original = api.get_ok("/api/config")["temp2_threshold"]
        new_val = 70.0 if original != 70.0 else 65.0
        api.post_ok("/api/config", json={"key": "temp2_threshold", "value": str(new_val)})
        updated = api.get_ok("/api/config")["temp2_threshold"]
        assert abs(updated - new_val) < 0.01
        api.post_ok("/api/config", json={"key": "temp2_threshold", "value": str(original)})

    def test_set_pa_relay(self, api):
        original = api.get_ok("/api/config")["pa_relay"]
        new_val = 3 if original != 3 else 2
        api.post_ok("/api/config", json={"key": "pa_relay", "value": str(new_val)})
        updated = api.get_ok("/api/config")["pa_relay"]
        assert updated == new_val
        api.post_ok("/api/config", json={"key": "pa_relay", "value": str(original)})

    def test_set_fwd_slope(self, api):
        original = api.get_ok("/api/config")["fwd_slope"]
        new_val = -54.2 if original != -54.2 else -25.0
        api.post_ok("/api/config", json={"key": "fwd_slope", "value": str(new_val)})
        updated = api.get_ok("/api/config")["fwd_slope"]
        assert abs(updated - new_val) < 0.1
        api.post_ok("/api/config", json={"key": "fwd_slope", "value": str(original)})

    def test_set_fwd_intercept(self, api):
        original = api.get_ok("/api/config")["fwd_intercept"]
        new_val = 30.1 if original != 30.1 else 0.0
        api.post_ok("/api/config", json={"key": "fwd_intercept", "value": str(new_val)})
        updated = api.get_ok("/api/config")["fwd_intercept"]
        assert abs(updated - new_val) < 0.1
        api.post_ok("/api/config", json={"key": "fwd_intercept", "value": str(original)})

    def test_set_fwd_coupling(self, api):
        original = api.get_ok("/api/config")["fwd_coupling"]
        new_val = -42.3 if original != -42.3 else 0.0
        api.post_ok("/api/config", json={"key": "fwd_coupling", "value": str(new_val)})
        updated = api.get_ok("/api/config")["fwd_coupling"]
        assert abs(updated - new_val) < 0.1
        api.post_ok("/api/config", json={"key": "fwd_coupling", "value": str(original)})

    def test_set_fwd_atten(self, api):
        original = api.get_ok("/api/config")["fwd_atten"]
        new_val = 10.0 if original != 10.0 else 0.0
        api.post_ok("/api/config", json={"key": "fwd_atten", "value": str(new_val)})
        updated = api.get_ok("/api/config")["fwd_atten"]
        assert abs(updated - new_val) < 0.1
        api.post_ok("/api/config", json={"key": "fwd_atten", "value": str(original)})

    def test_set_ref_slope(self, api):
        original = api.get_ok("/api/config")["ref_slope"]
        new_val = -54.8 if original != -54.8 else -25.0
        api.post_ok("/api/config", json={"key": "ref_slope", "value": str(new_val)})
        updated = api.get_ok("/api/config")["ref_slope"]
        assert abs(updated - new_val) < 0.1
        api.post_ok("/api/config", json={"key": "ref_slope", "value": str(original)})

    def test_set_therm_beta(self, api):
        original = api.get_ok("/api/config")["therm_beta"]
        new_val = 4000.0 if original != 4000.0 else 3950.0
        api.post_ok("/api/config", json={"key": "therm_beta", "value": str(new_val)})
        updated = api.get_ok("/api/config")["therm_beta"]
        assert abs(updated - new_val) < 1.0
        api.post_ok("/api/config", json={"key": "therm_beta", "value": str(original)})

    def test_set_therm_r0(self, api):
        original = api.get_ok("/api/config")["therm_r0"]
        new_val = 50000.0 if original != 50000.0 else 100000.0
        api.post_ok("/api/config", json={"key": "therm_r0", "value": str(new_val)})
        updated = api.get_ok("/api/config")["therm_r0"]
        assert abs(updated - new_val) < 1.0
        api.post_ok("/api/config", json={"key": "therm_r0", "value": str(original)})

    def test_set_therm_rseries(self, api):
        original = api.get_ok("/api/config")["therm_rseries"]
        new_val = 50000.0 if original != 50000.0 else 100000.0
        api.post_ok("/api/config", json={"key": "therm_rseries", "value": str(new_val)})
        updated = api.get_ok("/api/config")["therm_rseries"]
        assert abs(updated - new_val) < 1.0
        api.post_ok("/api/config", json={"key": "therm_rseries", "value": str(original)})

    def test_set_with_numeric_value(self, api):
        """Value passed as number instead of string."""
        original = api.get_ok("/api/config")["swr_threshold"]
        api.post_ok("/api/config", json={"key": "swr_threshold", "value": 4.0})
        updated = api.get_ok("/api/config")["swr_threshold"]
        assert abs(updated - 4.0) < 0.01
        api.post_ok("/api/config", json={"key": "swr_threshold", "value": str(original)})

    def test_invalid_key_returns_error(self, api):
        error = api.post_error("/api/config", json={"key": "nonexistent", "value": "1"})
        assert "unknown" in error.lower() or "key" in error.lower()

    def test_out_of_range_returns_error(self, api):
        error = api.post_error("/api/config", json={"key": "swr_threshold", "value": "0"})
        assert "range" in error.lower()

    def test_invalid_number_returns_error(self, api):
        error = api.post_error("/api/config", json={"key": "swr_threshold", "value": "abc"})
        assert "invalid" in error.lower() or "number" in error.lower()

    def test_missing_key_returns_error(self, api):
        error = api.post_error("/api/config", json={"value": "1"})
        assert len(error) > 0

    def test_missing_value_returns_error(self, api):
        error = api.post_error("/api/config", json={"key": "swr_threshold"})
        assert len(error) > 0

    def test_empty_body_returns_error(self, api):
        resp = api.post("/api/config")
        body = resp.json()
        assert body["ok"] is False


@pytest.mark.write
class TestConfigSave:
    """POST /api/config/save — persist to NVS."""

    def test_save_returns_ok(self, api):
        api.post_ok("/api/config/save")


@pytest.mark.write
class TestConfigDefaults:
    """POST /api/config/defaults — reset to factory defaults."""

    def test_defaults_returns_ok(self, api):
        """Reset to defaults and verify known default values."""
        api.post_ok("/api/config/defaults")
        data = api.get_ok("/api/config")
        assert abs(data["swr_threshold"] - 3.0) < 0.01
        assert abs(data["temp1_threshold"] - 65.0) < 0.01
        assert abs(data["temp2_threshold"] - 65.0) < 0.01
        assert data["pa_relay"] == 2
        assert abs(data["fwd_slope"] - (-25.0)) < 0.1
        assert abs(data["fwd_intercept"] - 0.0) < 0.1
        assert abs(data["ref_slope"] - (-25.0)) < 0.1
        assert abs(data["ref_intercept"] - 0.0) < 0.1


@pytest.mark.write
class TestPendingApply:
    """pending_apply flag lifecycle."""

    def test_pending_false_after_apply(self, api):
        """After applying, pending_apply should be false."""
        # Ensure clean state: defaults then apply
        api.post_ok("/api/config/defaults")
        api.post("/api/fault/clear")
        api.post_ok("/api/seq/apply")

        data = api.get_ok("/api/config")
        assert data["pending_apply"] is False

    def test_pending_true_after_edit(self, api):
        """Editing a config value without applying should set pending_apply."""
        # Start from a clean applied state
        api.post_ok("/api/config/defaults")
        api.post("/api/fault/clear")
        api.post_ok("/api/seq/apply")

        # Modify a threshold — this changes the in-memory config
        # but the sequencer still has the old copy
        original = api.get_ok("/api/config")["swr_threshold"]
        new_val = 2.5 if original != 2.5 else 3.5
        api.post_ok("/api/config", json={"key": "swr_threshold", "value": str(new_val)})

        data = api.get_ok("/api/config")
        assert data["pending_apply"] is True

        # Apply and verify it clears
        api.post_ok("/api/seq/apply")
        data = api.get_ok("/api/config")
        assert data["pending_apply"] is False

        # Restore
        api.post_ok("/api/config", json={"key": "swr_threshold", "value": str(original)})
        api.post_ok("/api/seq/apply")
