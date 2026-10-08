#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# parity-run.sh - end-to-end MMIO parity check of the new octool_hwio module
# against the original .ko, using the unmodified octool binary as the workload.
#
# It never modifies octool. It:
#   1. checks that the old device exists (operator must verify module ownership);
#   2. captures the addresses octool reads during one real session
#      (LD_PRELOAD observer - read-only);
#   3. loads the NEW module beside it under a second name (/dev/mydev_v2);
#   4. replays the captured reads against both, back-to-back, and diffs.
#
# Reads only. It issues no hardware writes of its own.
#
# Usage:
#   sudo OCTOOL=/path/to/octool sh parity-run.sh
#
# Env:
#   OCTOOL        octool binary to run for capture (required unless CORPUS given)
#   OCTOOL_ARGS   args passed to octool (default: none; octool runs then you quit)
#   CORPUS        reuse an existing corpus file, skip capture
#   NEW_KO        path to the new octool_hwio.ko (default: ../kmod/octool_hwio.ko)
#   NEW_DEV       second device name for the new module (default: mydev_v2)
#   OLD_DEV       device the original module owns (default: /dev/mydev)
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
OLD_DEV=${OLD_DEV:-/dev/mydev}
NEW_DEV=${NEW_DEV:-mydev_v2}
NEW_KO=${NEW_KO:-$HERE/../kmod/octool_hwio.ko}
CORPUS=${CORPUS:-$HERE/octool_corpus.bin}

[ "$(id -u)" = 0 ] || { echo "run as root (needs insmod + CAP_SYS_RAWIO)"; exit 2; }
case "$NEW_DEV" in ''|*[!a-zA-Z0-9_-]*) echo 'NEW_DEV must be a device basename' >&2; exit 2;; esac
# Reject an obvious self-comparison before launching the capture workload.
# octool_parity also compares the character-device numbers before replay.
if [ "$OLD_DEV" -ef "/dev/$NEW_DEV" ]; then
	echo 'old/new refer to the same device; verify distinct module ownership' >&2
	exit 2
fi

echo ">> building capture shim + parity tool"
make -C "$HERE" octool_capture.so octool_parity >/dev/null

# 1. original module present?
if [ ! -e "$OLD_DEV" ]; then
	echo "!! $OLD_DEV not present - load the ORIGINAL module first, then re-run." >&2
	exit 2
fi

# 2. capture, unless a corpus was supplied
if [ -f "$CORPUS" ] && [ -z "${OCTOOL:-}" ]; then
	echo ">> reusing corpus $CORPUS"
else
	[ -n "${OCTOOL:-}" ] || { echo "set OCTOOL=/path/to/octool (or CORPUS=...)"; exit 2; }
	echo ">> capturing octool's reads against the ORIGINAL module ($OLD_DEV)"
	echo "   (use octool normally; exit it when you've exercised the panels you care about)"
	OCTOOL_CAP_DEV="$OLD_DEV" OCTOOL_CAP_OUT="$CORPUS" \
		LD_PRELOAD="$HERE/octool_capture.so" "$OCTOOL" ${OCTOOL_ARGS:-}
	echo ">> captured -> $CORPUS"
fi

# 3. load the new module beside the old one
[ -s "$CORPUS" ] || { echo "empty or missing corpus: $CORPUS" >&2; exit 2; }
# Reject incomplete capture before even loading the candidate module.
"$HERE/octool_parity" --check-trace --trace "$CORPUS"
if [ ! -e "/dev/$NEW_DEV" ]; then
	echo ">> loading NEW module as /dev/$NEW_DEV"
	insmod "$NEW_KO" devname="$NEW_DEV"
	CLEANUP_MOD=1
else
	echo ">> /dev/$NEW_DEV already present, using it"
	CLEANUP_MOD=0
fi
trap '[ "${CLEANUP_MOD:-0}" = 1 ] && rmmod octool_hwio 2>/dev/null || true' EXIT

# 4. diff
echo ">> comparing (live, back-to-back reads)"
"$HERE/octool_parity" --old "$OLD_DEV" --new "/dev/$NEW_DEV" --trace "$CORPUS"
