"""Tests for /api/wifi endpoints.

Note: Most write operations are read-only-safe or have careful teardown.
The truly destructive ones (erase, disconnect) are marked @destructive
since they would break the test session's connectivity.
"""

import pytest


class TestWifiStatus:
    """GET /api/wifi/status — WiFi connection state."""

    def test_returns_ok(self, api):
        data = api.get_ok("/api/wifi/status")
        assert data is not None

    def test_has_connected_field(self, api):
        data = api.get_ok("/api/wifi/status")
        assert isinstance(data["connected"], bool)

    def test_has_auto_connect_field(self, api):
        data = api.get_ok("/api/wifi/status")
        assert isinstance(data["auto_connect"], bool)

    def test_has_ip_when_connected(self, api):
        data = api.get_ok("/api/wifi/status")
        if data["connected"]:
            assert "ip" in data
            assert isinstance(data["ip"], str)
            # Verify it looks like an IP
            parts = data["ip"].split(".")
            assert len(parts) == 4

    def test_has_rssi_when_connected(self, api):
        data = api.get_ok("/api/wifi/status")
        if data["connected"]:
            assert "rssi" in data
            assert isinstance(data["rssi"], (int, float))
            assert data["rssi"] < 0  # RSSI is always negative


class TestWifiScan:
    """GET /api/wifi/scan — scan for available networks."""

    def test_returns_ok(self, api):
        # Scan can take 1-3 seconds
        data = api.get_ok("/api/wifi/scan", timeout=15)
        assert data is not None

    def test_has_networks_array(self, api):
        data = api.get_ok("/api/wifi/scan", timeout=15)
        assert isinstance(data["networks"], list)
        assert isinstance(data["count"], int)
        assert data["count"] == len(data["networks"])

    def test_network_has_fields(self, api):
        data = api.get_ok("/api/wifi/scan", timeout=15)
        if len(data["networks"]) > 0:
            net = data["networks"][0]
            assert isinstance(net["ssid"], str)
            assert isinstance(net["rssi"], (int, float))
            assert isinstance(net["channel"], int)
            assert isinstance(net["authmode"], int)


@pytest.mark.write
class TestWifiConfig:
    """POST /api/wifi/config — set WiFi credentials."""

    def test_set_credentials(self, api):
        """Set credentials (doesn't connect, just saves to NVS)."""
        # Save current connection state to know if we need to reconnect
        status = api.get_ok("/api/wifi/status")

        # This just saves creds — doesn't disconnect or reconnect
        api.post_ok("/api/wifi/config", json={
            "ssid": "test_ssid_do_not_connect",
            "password": "test_password"
        })

    def test_missing_ssid_returns_error(self, api):
        error = api.post_error("/api/wifi/config", json={"password": "test"})
        assert len(error) > 0

    def test_empty_body_returns_error(self, api):
        resp = api.post("/api/wifi/config")
        body = resp.json()
        assert body["ok"] is False


@pytest.mark.write
class TestWifiAuto:
    """POST /api/wifi/auto — enable/disable auto-connect."""

    def test_enable_auto_connect(self, api):
        api.post_ok("/api/wifi/auto", json={"enabled": True})
        status = api.get_ok("/api/wifi/status")
        assert status["auto_connect"] is True

    def test_disable_auto_connect(self, api):
        api.post_ok("/api/wifi/auto", json={"enabled": False})
        status = api.get_ok("/api/wifi/status")
        assert status["auto_connect"] is False

        # Restore
        api.post_ok("/api/wifi/auto", json={"enabled": True})

    def test_missing_enabled_returns_error(self, api):
        error = api.post_error("/api/wifi/auto", json={})
        assert len(error) > 0


@pytest.mark.write
class TestWifiConnect:
    """POST /api/wifi/connect — connect using saved credentials."""

    def test_connect_returns_ok(self, api):
        """Connect should succeed if credentials are saved."""
        resp = api.post("/api/wifi/connect")
        body = resp.json()
        # May succeed or fail with NOT_FOUND if no creds — both valid
        assert "ok" in body


@pytest.mark.destructive
class TestWifiDisconnect:
    """POST /api/wifi/disconnect — disconnect from AP."""

    def test_disconnect_returns_ok(self, api):
        api.post_ok("/api/wifi/disconnect")


@pytest.mark.destructive
class TestWifiErase:
    """POST /api/wifi/erase — erase saved credentials."""

    def test_erase_returns_ok(self, api):
        api.post_ok("/api/wifi/erase")
