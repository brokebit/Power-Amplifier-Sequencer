"""Tests for WebSocket endpoint ws://<device>/ws."""

import json
import time

import pytest
import websocket


class TestWebSocket:
    """WebSocket /ws — live state push."""

    def test_connect_and_receive_frame(self, device_ip):
        """Connect and verify at least one JSON frame arrives."""
        ws = websocket.create_connection(
            f"ws://{device_ip}/ws",
            timeout=5,
        )
        try:
            frame = ws.recv()
            assert frame is not None
            assert len(frame) > 0

            data = json.loads(frame)
            assert isinstance(data, dict)
        finally:
            ws.close()

    def test_frame_has_state_fields(self, device_ip):
        """Verify the pushed frame has the same shape as GET /api/state."""
        ws = websocket.create_connection(
            f"ws://{device_ip}/ws",
            timeout=5,
        )
        try:
            frame = ws.recv()
            data = json.loads(frame)

            # Should match GET /api/state shape
            assert "ptt" in data
            assert "seq_state" in data
            assert "seq_fault" in data
            assert "relays" in data
            assert "fwd_w" in data
            assert "ref_w" in data
            assert "fwd_dbm" in data
            assert "ref_dbm" in data
            assert "swr" in data
            assert "temp1_c" in data
            assert "temp2_c" in data
            assert "wifi" in data
        finally:
            ws.close()

    def test_receives_multiple_frames(self, device_ip):
        """Verify multiple frames arrive over time."""
        ws = websocket.create_connection(
            f"ws://{device_ip}/ws",
            timeout=5,
        )
        try:
            # Discard first frame (may arrive immediately from buffer)
            ws.recv()

            frames = []
            timestamps = []
            for _ in range(3):
                frame = ws.recv()
                timestamps.append(time.time())
                frames.append(json.loads(frame))

            assert len(frames) == 3

            # Total time for 3 frames should be roughly 0.3-4 seconds
            # Push interval is 250ms, so 2 gaps between 3 frames ≈ 500ms
            total_time = timestamps[-1] - timestamps[0]
            assert 0.3 < total_time < 5.0, f"Total time for 3 frames: {total_time:.2f}s"
        finally:
            ws.close()

    def test_frame_relays_are_booleans(self, device_ip):
        """Verify relay array contains booleans."""
        ws = websocket.create_connection(
            f"ws://{device_ip}/ws",
            timeout=5,
        )
        try:
            frame = ws.recv()
            data = json.loads(frame)

            assert isinstance(data["relays"], list)
            assert len(data["relays"]) == 6
            for r in data["relays"]:
                assert isinstance(r, bool)
        finally:
            ws.close()

    def test_frame_wifi_object(self, device_ip):
        """Verify wifi sub-object in pushed frames."""
        ws = websocket.create_connection(
            f"ws://{device_ip}/ws",
            timeout=5,
        )
        try:
            frame = ws.recv()
            data = json.loads(frame)

            wifi = data["wifi"]
            assert isinstance(wifi, dict)
            assert "connected" in wifi
            assert isinstance(wifi["connected"], bool)
            if wifi["connected"]:
                assert "ip" in wifi
                assert "rssi" in wifi
        finally:
            ws.close()

    def test_multiple_clients(self, device_ip):
        """Verify two simultaneous WebSocket clients both receive frames."""
        time.sleep(1)  # Let server finish closing sockets from previous tests
        ws1 = websocket.create_connection(f"ws://{device_ip}/ws", timeout=10)
        time.sleep(0.5)  # Let server process first connection
        ws2 = websocket.create_connection(f"ws://{device_ip}/ws", timeout=10)
        try:
            frame1 = ws1.recv()
            frame2 = ws2.recv()

            data1 = json.loads(frame1)
            data2 = json.loads(frame2)

            # Both should be valid state frames
            assert "seq_state" in data1
            assert "seq_state" in data2
        finally:
            ws1.close()
            ws2.close()
