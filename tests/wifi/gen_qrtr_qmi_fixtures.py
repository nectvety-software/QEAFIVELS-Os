#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Generate labelled FIXTURE wire packets for QRTR/QMI protocol tests.

These are hand-built from the public wire format, not captured credentials.
Production code never reads these files; only tests/wifi/test_qrtr_qmi.c does.
"""
from __future__ import annotations

import struct
from pathlib import Path

OUT = Path(__file__).resolve().parent / "fixtures"


def le32(v):
    return struct.pack("<I", v)


def qmi(txn, msg_id, tlv_type, payload):
    tlv = bytes([tlv_type]) + struct.pack("<H", len(payload)) + payload
    return bytes([2]) + struct.pack("<HHH", txn, msg_id, len(tlv)) + tlv


def main():
    OUT.mkdir(exist_ok=True)
    # QRTR control: cmd + service + verinst + node + port (20 bytes).
    fixtures = {
        "FIXTURE_qrtr_new_server_45.bin": le32(4) + le32(0x45) +
            le32((1 << 8) | 1) + le32(2) + le32(0x1234),
        "FIXTURE_qrtr_del_server_45.bin": le32(5) + le32(0x45) +
            le32((1 << 8) | 1) + le32(2) + le32(0x1234),
        "FIXTURE_qrtr_short.bin": le32(4) + le32(0x45),
        "FIXTURE_qmi_response_ok.bin": qmi(1, 0x20, 0,
                                           struct.pack("<HH", 0, 0)),
        "FIXTURE_qmi_response_failure.bin": qmi(1, 0x20, 0,
                                                struct.pack("<HH", 1, 94)),
        "FIXTURE_qmi_indication.bin": (
            bytes([4]) + struct.pack("<HHH", 99, 0x20, 0)),
    }
    for name, data in fixtures.items():
        (OUT / name).write_bytes(data)
        print("wrote %s (%d bytes)" % (name, len(data)))


if __name__ == "__main__":
    main()
