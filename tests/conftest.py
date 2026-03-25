"""
Shared fixtures and CLI options for live-device API testing.

Usage:
    pytest tests/ --device-ip 192.168.1.100
    pytest tests/ --device-ip 192.168.1.100 -k "not destructive"
    pytest tests/ --device-ip 192.168.1.100 --run-destructive
"""

import pytest
import requests


def pytest_addoption(parser):
    parser.addoption(
        "--device-ip",
        action="store",
        required=True,
        help="IP address of the live ESP32 device",
    )
    parser.addoption(
        "--run-destructive",
        action="store_true",
        default=False,
        help="Run destructive tests (reboot, OTA update, wifi erase)",
    )


def pytest_configure(config):
    config.addinivalue_line("markers", "destructive: marks tests that reboot, erase, or OTA update the device")
    config.addinivalue_line("markers", "write: marks tests that modify device state (relays, config, faults)")


def pytest_collection_modifyitems(config, items):
    if not config.getoption("--run-destructive"):
        skip = pytest.mark.skip(reason="needs --run-destructive to run")
        for item in items:
            if "destructive" in item.keywords:
                item.add_marker(skip)


@pytest.fixture(scope="session")
def device_ip(request):
    """The IP address of the live device under test."""
    return request.config.getoption("--device-ip")


@pytest.fixture(scope="session")
def base_url(device_ip):
    """Base URL for all API requests."""
    return f"http://{device_ip}"


@pytest.fixture(scope="session")
def api(base_url):
    """Helper object for making API calls with common assertions."""
    return ApiClient(base_url)


class ApiClient:
    """Thin wrapper around requests with JSON envelope validation."""

    def __init__(self, base_url):
        self.base_url = base_url
        self.timeout = 10

    def get(self, path, **kwargs):
        kwargs.setdefault("timeout", self.timeout)
        return requests.get(f"{self.base_url}{path}", **kwargs)

    def post(self, path, json=None, **kwargs):
        kwargs.setdefault("timeout", self.timeout)
        return requests.post(f"{self.base_url}{path}", json=json, **kwargs)

    def get_ok(self, path, **kwargs):
        """GET and assert 200 + ok envelope. Returns the data field."""
        resp = self.get(path, **kwargs)
        assert resp.status_code == 200, f"GET {path}: {resp.status_code} {resp.text}"
        body = resp.json()
        assert body["ok"] is True, f"GET {path}: {body}"
        return body.get("data")

    def post_ok(self, path, json=None, **kwargs):
        """POST and assert 200 + ok envelope. Returns the data field."""
        resp = self.post(path, json=json, **kwargs)
        assert resp.status_code == 200, f"POST {path}: {resp.status_code} {resp.text}"
        body = resp.json()
        assert body["ok"] is True, f"POST {path}: {body}"
        return body.get("data")

    def post_error(self, path, json=None, expected_status=400, **kwargs):
        """POST and assert non-ok envelope. Returns the error string."""
        resp = self.post(path, json=json, **kwargs)
        body = resp.json()
        assert body["ok"] is False, f"POST {path}: expected error, got {body}"
        return body.get("error", "")
