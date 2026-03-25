"""Tests for /api/ota endpoints.

OTA update and rollback are destructive (reboot the device),
so they're marked @destructive and skipped by default.
"""

import pytest


class TestOtaStatus:
    """GET /api/ota/status — OTA partition and version info."""

    def test_returns_ok(self, api):
        data = api.get_ok("/api/ota/status")
        assert data is not None

    def test_has_version(self, api):
        data = api.get_ok("/api/ota/status")
        assert isinstance(data["version"], str)
        assert len(data["version"]) > 0

    def test_has_running_partition(self, api):
        data = api.get_ok("/api/ota/status")
        assert isinstance(data["running_partition"], str)
        assert data["running_partition"] in ("ota_0", "ota_1", "factory")

    def test_has_boot_partition(self, api):
        data = api.get_ok("/api/ota/status")
        assert isinstance(data["boot_partition"], str)

    def test_has_next_update_partition(self, api):
        data = api.get_ok("/api/ota/status")
        assert isinstance(data["next_update_partition"], str)

    def test_has_app_state(self, api):
        data = api.get_ok("/api/ota/status")
        assert isinstance(data["app_state"], str)

    def test_has_other_version(self, api):
        data = api.get_ok("/api/ota/status")
        assert isinstance(data["other_version"], str)


class TestOtaRepoGet:
    """GET /api/ota/repo — show configured GitHub repo."""

    def test_returns_ok(self, api):
        data = api.get_ok("/api/ota/repo")
        assert "repo" in data
        assert isinstance(data["repo"], str)


@pytest.mark.write
class TestOtaRepoSet:
    """POST /api/ota/repo — set GitHub repo."""

    def test_set_repo(self, api):
        """Set a repo and verify it."""
        # Save original
        original = api.get_ok("/api/ota/repo")["repo"]

        api.post_ok("/api/ota/repo", json={"repo": "testowner/testrepo"})
        data = api.get_ok("/api/ota/repo")
        assert data["repo"] == "testowner/testrepo"

        # Restore original if it was set
        if original:
            api.post_ok("/api/ota/repo", json={"repo": original})

    def test_invalid_repo_no_slash(self, api):
        error = api.post_error("/api/ota/repo", json={"repo": "noslash"})
        assert "owner/repo" in error.lower() or "format" in error.lower()

    def test_invalid_repo_double_slash(self, api):
        error = api.post_error("/api/ota/repo", json={"repo": "a/b/c"})
        assert len(error) > 0

    def test_invalid_repo_empty(self, api):
        error = api.post_error("/api/ota/repo", json={"repo": ""})
        assert len(error) > 0

    def test_missing_repo_field(self, api):
        error = api.post_error("/api/ota/repo", json={})
        assert len(error) > 0


@pytest.mark.write
class TestOtaValidate:
    """POST /api/ota/validate — manually mark firmware valid."""

    def test_validate_returns_ok(self, api):
        api.post_ok("/api/ota/validate")


@pytest.mark.destructive
class TestOtaUpdate:
    """POST /api/ota/update — start OTA update (reboots on success)."""

    def test_update_returns_started(self, api):
        """Verify the async response. The actual OTA will likely fail
        without a real firmware URL, but the endpoint should accept it."""
        data = api.post_ok("/api/ota/update", json={"target": "latest"})
        assert data["status"] == "started"

    def test_missing_target(self, api):
        error = api.post_error("/api/ota/update", json={})
        assert len(error) > 0


@pytest.mark.destructive
class TestOtaRollback:
    """POST /api/ota/rollback — revert to previous firmware (reboots)."""

    def test_rollback_responds(self, api):
        """Rollback will reboot the device. Just verify we get a response."""
        resp = api.post("/api/ota/rollback")
        # May succeed (reboots) or fail (no other partition)
        assert resp.status_code == 200
