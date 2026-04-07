"""Tests for GET /api/state and GET /api/version."""

# Must match HW_RELAY_COUNT in hw_config.h
RELAY_COUNT = 6


class TestGetState:
    """GET /api/state — system state snapshot."""

    def test_returns_ok(self, api):
        data = api.get_ok("/api/state")
        assert data is not None

    def test_has_ptt_field(self, api):
        data = api.get_ok("/api/state")
        assert "ptt" in data
        assert isinstance(data["ptt"], bool)

    def test_has_sequencer_state(self, api):
        data = api.get_ok("/api/state")
        assert data["seq_state"] in ("RX", "SEQ_TX", "TX", "SEQ_RX", "FAULT")

    def test_has_fault_field(self, api):
        data = api.get_ok("/api/state")
        assert data["seq_fault"] in (
            "none", "HIGH_SWR", "OVER_TEMP1", "OVER_TEMP2", "EMERGENCY"
        )

    def test_has_relays_array(self, api):
        data = api.get_ok("/api/state")
        assert isinstance(data["relays"], list)
        assert len(data["relays"]) == RELAY_COUNT
        for r in data["relays"]:
            assert isinstance(r, bool)

    def test_has_relay_names_array(self, api):
        data = api.get_ok("/api/state")
        assert isinstance(data["relay_names"], list)
        assert len(data["relay_names"]) == RELAY_COUNT
        for name in data["relay_names"]:
            assert isinstance(name, str)

    def test_has_power_readings(self, api):
        data = api.get_ok("/api/state")
        assert isinstance(data["fwd_w"], (int, float))
        assert isinstance(data["ref_w"], (int, float))
        assert isinstance(data["fwd_dbm"], (int, float))
        assert isinstance(data["ref_dbm"], (int, float))
        assert isinstance(data["swr"], (int, float))

    def test_has_temperature_readings(self, api):
        data = api.get_ok("/api/state")
        # Temps may be null if a thermistor is not connected
        assert data["temp1_c"] is None or isinstance(data["temp1_c"], (int, float))
        assert data["temp2_c"] is None or isinstance(data["temp2_c"], (int, float))

    def test_has_adc0_readings(self, api):
        data = api.get_ok("/api/state")
        for key in ("adc_0_ch0", "adc_0_ch1", "adc_0_ch2", "adc_0_ch3"):
            assert isinstance(data[key], (int, float)), f"{key} missing or wrong type"

    def test_has_adc0_channel_names(self, api):
        data = api.get_ok("/api/state")
        assert isinstance(data["adc_0_ch_names"], list)
        assert len(data["adc_0_ch_names"]) == 4
        for name in data["adc_0_ch_names"]:
            assert isinstance(name, str)

    def test_has_wifi_object(self, api):
        data = api.get_ok("/api/state")
        wifi = data["wifi"]
        assert isinstance(wifi, dict)
        assert "connected" in wifi
        assert isinstance(wifi["connected"], bool)

    def test_wifi_has_ip_when_connected(self, api):
        data = api.get_ok("/api/state")
        wifi = data["wifi"]
        if wifi["connected"]:
            assert "ip" in wifi
            assert isinstance(wifi["ip"], str)
            assert "rssi" in wifi


class TestGetVersion:
    """GET /api/version — firmware and chip info."""

    def test_returns_ok(self, api):
        data = api.get_ok("/api/version")
        assert data is not None

    def test_has_project_name(self, api):
        data = api.get_ok("/api/version")
        assert isinstance(data["project"], str)
        assert len(data["project"]) > 0

    def test_has_version_string(self, api):
        data = api.get_ok("/api/version")
        assert isinstance(data["version"], str)
        assert len(data["version"]) > 0

    def test_has_idf_version(self, api):
        data = api.get_ok("/api/version")
        assert isinstance(data["idf_version"], str)

    def test_has_core_count(self, api):
        data = api.get_ok("/api/version")
        assert isinstance(data["cores"], int)
        assert data["cores"] >= 1
