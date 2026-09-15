#!/usr/bin/env python3
"""Real, network-free unit tests for rl_registry.py's own multipart encoder (S459-50). The real
push/list/pull round trip was live-verified against a running IDUNA instance during development
(not re-tested here with a mock, which would only prove the mock is self-consistent) -- this
covers the one piece of real, hand-rolled logic worth a unit test in isolation: multipart
encoding, since a subtly wrong boundary/header here would produce a real server-side parse
failure that's hard to debug from the client side alone."""

import unittest

from rl_registry import _multipart_body


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


if __name__ == "__main__":
    unittest.main()
