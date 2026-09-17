#!/usr/bin/env python3
"""Real, network-free unit tests for rl_registry.py's own multipart encoder (S459-50). The real
push/list/pull round trip was live-verified against a running IDUNA instance during development
(not re-tested here with a mock, which would only prove the mock is self-consistent) -- this
covers the one piece of real, hand-rolled logic worth a unit test in isolation: multipart
encoding, since a subtly wrong boundary/header here would produce a real server-side parse
failure that's hard to debug from the client side alone."""

import json
import unittest
from unittest.mock import MagicMock, patch

from rl_registry import _multipart_body, push_heartbeat


class TestMultipartBody(unittest.TestCase):
    def test_encodes_fields_and_one_file(self):
        body, content_type = _multipart_body(
            {"role": "main", "generation": 1, "elo": 1500.0, "source_location": "colab"},
            [("file", "checkpoint.zip", b"fake zip bytes")],
        )
        self.assertTrue(content_type.startswith("multipart/form-data; boundary="))
        boundary = content_type.split("boundary=")[1]
        text = body.decode(errors="replace")
        self.assertIn(f"--{boundary}", text)
        self.assertIn('name="role"', text)
        self.assertIn("main", text)
        self.assertIn('name="file"; filename="checkpoint.zip"', text)
        self.assertIn("Content-Type: application/zip", text)
        self.assertTrue(body.endswith(f"--{boundary}--\r\n".encode()))

    def test_no_files_still_encodes_fields(self):
        body, content_type = _multipart_body({"a": 1}, [])
        self.assertIn(b'name="a"', body)
        self.assertIn("boundary=", content_type)


class TestPushHeartbeat(unittest.TestCase):
    """S474 (founder real-time: "can we have more debugging in the heartbeat"). Real, network-
    free coverage of push_heartbeat's own request-building/error-handling logic -- the actual
    live endpoint (POST /services/collector) was confirmed working directly during development
    (curl against localhost:8080 and, once sudo-queue/81 lands, the public okemily.com/services/
    path), same "hand-rolled logic worth a unit test in isolation" scoping this file's own module
    doc comment already establishes for _multipart_body above."""

    def test_no_token_is_a_real_noop_no_network_call(self):
        with patch("rl_registry.urllib.request.urlopen") as mock_urlopen:
            result = push_heartbeat("https://okemily.com", None, "opponent_chosen", {"generation": 1})
        self.assertFalse(result)
        mock_urlopen.assert_not_called()

    def test_real_token_sends_splunk_hec_shaped_request(self):
        mock_resp = MagicMock()
        mock_resp.status = 200
        mock_resp.__enter__.return_value = mock_resp
        with patch("rl_registry.urllib.request.urlopen", return_value=mock_resp) as mock_urlopen:
            result = push_heartbeat("https://okemily.com", "real-token", "eval_result",
                                     {"generation": 5, "role": "main", "score_a": 1.0})
        self.assertTrue(result)
        mock_urlopen.assert_called_once()
        req = mock_urlopen.call_args[0][0]
        self.assertEqual(req.full_url, "https://okemily.com/services/collector")
        self.assertEqual(req.get_header("Authorization"), "Splunk real-token")
        body = json.loads(req.data)
        self.assertEqual(body["sourcetype"], "shankpit:rl:heartbeat")
        self.assertEqual(body["event"]["event_type"], "eval_result")
        self.assertEqual(body["event"]["generation"], 5)
        self.assertEqual(body["event"]["score_a"], 1.0)

    def test_network_error_returns_false_not_an_exception(self):
        with patch("rl_registry.urllib.request.urlopen", side_effect=OSError("connection refused")):
            result = push_heartbeat("https://okemily.com", "real-token", "opponent_chosen", {})
        self.assertFalse(result)


if __name__ == "__main__":
    unittest.main()
