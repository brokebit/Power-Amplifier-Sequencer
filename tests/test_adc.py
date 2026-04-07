"""Tests for /api/adc endpoints."""

import pytest


class TestGetAdcAll:
    """GET /api/adc — read all 4 channels."""

    def test_returns_ok(self, api):
        data = api.get_ok("/api/adc")
        assert data is not None

    def test_has_channels_array(self, api):
        data = api.get_ok("/api/adc")
        assert isinstance(data["channels"], list)
        assert len(data["channels"]) == 4

    def test_each_channel_has_fields(self, api):
        data = api.get_ok("/api/adc")
        expected_names = ["fwd_power", "ref_power", "temp1", "temp2"]
        for i, ch in enumerate(data["channels"]):
            assert ch["ch"] == i
            assert ch["name"] == expected_names[i]
            # voltage may be null on read error, or a number
            assert ch["voltage"] is None or isinstance(ch["voltage"], (int, float))


class TestGetAdcSingle:
    """GET /api/adc?ch=N — read single channel."""

    @pytest.mark.parametrize("ch,name", [
        (0, "fwd_power"),
        (1, "ref_power"),
        (2, "temp1"),
        (3, "temp2"),
    ])
    def test_read_each_channel(self, api, ch, name):
        data = api.get_ok(f"/api/adc?ch={ch}")
        assert data["ch"] == ch
        assert data["name"] == name
        assert isinstance(data["voltage"], (int, float))

    def test_invalid_channel_negative(self, api):
        resp = api.get("/api/adc?ch=-1")
        body = resp.json()
        assert body["ok"] is False

    def test_invalid_channel_four(self, api):
        resp = api.get("/api/adc?ch=4")
        body = resp.json()
        assert body["ok"] is False


@pytest.mark.write
class TestSetAdcName:
    """POST /api/adc/name — set/clear chip 0 channel names."""

    def test_set_channel_name(self, api):
        api.post_ok("/api/adc/name", json={"ch": 0, "name": "MySensor"})
        data = api.get_ok("/api/config")
        assert data["adc_0_ch_names"][0] == "MySensor"

    def test_clear_channel_name(self, api):
        api.post_ok("/api/adc/name", json={"ch": 0, "name": "Temp"})
        api.post_ok("/api/adc/name", json={"ch": 0, "name": None})
        data = api.get_ok("/api/config")
        assert data["adc_0_ch_names"][0] == ""

    def test_invalid_channel(self, api):
        resp = api.post("/api/adc/name", json={"ch": 5, "name": "Bad"})
        body = resp.json()
        assert body["ok"] is False

    def test_missing_ch_field(self, api):
        resp = api.post("/api/adc/name", json={"name": "NoChannel"})
        body = resp.json()
        assert body["ok"] is False
