#!/usr/bin/env bash
set -euo pipefail
echo "ERROR: generic sm6150.dts is not a Violet board DTB." >&2
echo "This legacy target is disabled to prevent packaging a SoC reference tree." >&2
echo "Use scripts/build_violet_board_dtb.sh; its output is reference-only until kernel ABI matches." >&2
exit 78
