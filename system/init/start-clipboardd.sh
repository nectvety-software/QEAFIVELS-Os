#!/system/bin/sh
# QEAF service hook to be invoked by the real PID 1 after /run is mounted.
# Required environment: QEAF_CLIPBOARDD, QEAF_CLIPBOARD_GID.
set -eu
DAEMON="${QEAF_CLIPBOARDD:-/system/bin/qeaf-clipboardd}"
SOCKET="${QEAF_CLIPBOARD_SOCKET:-/run/qeaf/clipboard.sock}"
GID="${QEAF_CLIPBOARD_GID:?QEAF_CLIPBOARD_GID must be configured by init}"
[ -x "$DAEMON" ] || { echo "qeaf-init: missing clipboard daemon: $DAEMON" >&2; exit 1; }
mkdir -p /run/qeaf
QEAF_CLIPBOARD_SOCKET="$SOCKET" QEAF_CLIPBOARD_GID="$GID" "$DAEMON" </dev/null >/dev/null 2>&1 &
PID=$!
i=0
while [ "$i" -lt 50 ]; do
    [ -S "$SOCKET" ] && { echo "$PID" > /run/qeaf/clipboardd.pid; exit 0; }
    kill -0 "$PID" 2>/dev/null || { echo "qeaf-init: clipboard daemon exited" >&2; exit 1; }
    i=$((i + 1))
    sleep 0.1
done
kill "$PID" 2>/dev/null || true
echo "qeaf-init: clipboard socket readiness timeout" >&2
exit 1
