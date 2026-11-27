#!/usr/bin/env python3
"""Run the independent Linux x86_64/QEMU boot gate."""
import argparse
import build_linux_x86_64 as baseline


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--timeout", type=int, default=45)
    args = parser.parse_args()
    baseline.verify_artifacts()
    baseline.boot_and_record(args.timeout)
    baseline.verify_artifacts()
    print("Linux x86_64 QEMU gate PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
