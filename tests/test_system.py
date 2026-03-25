"""Tests for /api/reboot and /api/log endpoints."""

import pytest


@pytest.mark.write
class TestPostLog:
    """POST /api/log — set log level."""

    @pytest.mark.parametrize("level", [
        "off", "none", "error", "warn", "info", "debug", "verbose"
    ])
    def test_set_global_log_level(self, api, level):
        api.post_ok("/api/log", json={"level": level})

    def test_set_log_level_with_tag(self, api):
        api.post_ok("/api/log", json={"level": "debug", "tag": "monitor"})

    def test_set_log_level_with_wildcard_tag(self, api):
        api.post_ok("/api/log", json={"level": "info", "tag": "*"})

    def test_invalid_level_returns_error(self, api):
        error = api.post_error("/api/log", json={"level": "bogus"})
        assert "level" in error.lower()

    def test_missing_level_returns_error(self, api):
        error = api.post_error("/api/log", json={})
        assert len(error) > 0

    def test_empty_body_returns_error(self, api):
        resp = api.post("/api/log")
        body = resp.json()
        assert body["ok"] is False

    def test_restore_log_off(self, api):
        """Restore logging to off (default state)."""
        api.post_ok("/api/log", json={"level": "off"})


@pytest.mark.destructive
class TestPostReboot:
    """POST /api/reboot — reboot the device."""

    def test_reboot_returns_ok(self, api):
        """Verify the endpoint responds before rebooting."""
        resp = api.post("/api/reboot")
        body = resp.json()
        assert body["ok"] is True
