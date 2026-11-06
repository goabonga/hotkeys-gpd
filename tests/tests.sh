#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris <goabonga@pm.me>
#
# Non-root tests of the daemon: --test-event on a fake backlight.
set -u
# --backlight-dir is refused as root: the tests must run unprivileged.
if [ "$(id -u)" -eq 0 ]; then
	echo "tests.sh must not run as root" >&2
	exit 2
fi
BIN=${1:-./build/gpd-hotkeysd}
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
ok=0
failed=0

check() { # check <description> <command...>
	d=$1
	shift
	if "$@" >/dev/null 2>&1; then
		ok=$((ok + 1))
	else
		failed=$((failed + 1))
		echo "FAIL: $d"
	fi
}

# check_out <description> <pattern> <command...>: stderr+stdout matches
check_out() {
	d=$1
	p=$2
	shift 2
	if "$@" 2>&1 | grep -q "$p"; then
		ok=$((ok + 1))
	else
		failed=$((failed + 1))
		echo "FAIL: $d"
	fi
}

mkdir "$T/bl"
echo 100 >"$T/bl/max_brightness"
echo 13 >"$T/bl/brightness"
: >"$T/conf"

# dry-run: nothing changes
check "dry-run brightness up" "$BIN" -n -c "$T/conf" -b "$T/bl" -t KEY_BRIGHTNESSUP
check "dry-run leaves brightness" grep -qx 13 "$T/bl/brightness"
check_out "dry-run message" 'brightness 13 -> 18' \
	"$BIN" -n -c "$T/conf" -b "$T/bl" -t KEY_BRIGHTNESSUP

# real writes to the fake directory
check "brightness up" "$BIN" -c "$T/conf" -b "$T/bl" -t KEY_BRIGHTNESSUP
check "brightness 18" grep -qx 18 "$T/bl/brightness"
echo 13 >"$T/bl/brightness"
check "brightness down" "$BIN" -c "$T/conf" -b "$T/bl" -t KEY_BRIGHTNESSDOWN
check "down stops at the 10 % threshold" grep -qx 10 "$T/bl/brightness"
for _ in $(seq 24); do
	"$BIN" -c "$T/conf" -b "$T/bl" -t brightnessdown 2>/dev/null
done
check "floor of 1" grep -qx 1 "$T/bl/brightness"
echo 98 >"$T/bl/brightness"
"$BIN" -c "$T/conf" -b "$T/bl" -t KEY_BRIGHTNESSUP
check "ceiling of max_brightness" grep -qx 100 "$T/bl/brightness"

# configuration
printf 'brightness_step = 10\n' >"$T/conf-step"
echo 50 >"$T/bl/brightness"
"$BIN" -c "$T/conf-step" -b "$T/bl" -t KEY_BRIGHTNESSUP
check "configured step" grep -qx 60 "$T/bl/brightness"
printf 'bogus_key = 3\n' >"$T/conf-bad"
check_out "unknown key reported" 'unknown key' \
	"$BIN" -n -c "$T/conf-bad" -b "$T/bl" -t KEY_BRIGHTNESSUP
{
	printf '# '
	head -c 509 /dev/zero | tr '\0' x
	printf 'brightness_step = 40\n'
} >"$T/conf-long"
echo 50 >"$T/bl/brightness"
check_out "long line reported" 'line too long' \
	"$BIN" -n -c "$T/conf-long" -b "$T/bl" -t KEY_BRIGHTNESSUP
check_out "rest of a long line ignored" 'brightness 50 -> 55' \
	"$BIN" -n -c "$T/conf-long" -b "$T/bl" -t KEY_BRIGHTNESSUP

# sound keys: the test card does not exist
printf 'card = missing-card-test\n' >"$T/conf-nocard"
for k in KEY_VOLUMEUP KEY_VOLUMEDOWN KEY_MUTE; do
	check "dry-run $k without card" "$BIN" -n -c "$T/conf-nocard" -t $k
	check_out "dry-run $k reports the card" 'card "missing-card-test" unavailable' \
		"$BIN" -n -c "$T/conf-nocard" -t $k
	check "$k without card fails" sh -c "! '$BIN' -c '$T/conf-nocard' -t $k"
done
printf 'volume_step = 99%%\n' >"$T/conf-badpct"
check_out "invalid volume_step reported" 'volume_step: invalid value' \
	"$BIN" -n -c "$T/conf-badpct" -t KEY_VOLUMEUP

# errors
check "relative --backlight-dir refused" sh -c "! '$BIN' -n -b bl -t KEY_BRIGHTNESSUP"
printf 'backlight = %s\n' "$T/bl" >"$T/conf-abs"
check_out "absolute backlight refused" 'backlight: invalid value' \
	"$BIN" -n -c "$T/conf-abs" -t KEY_BRIGHTNESSUP
check "unknown key name refused" sh -c "! '$BIN' -n -t KEY_FOO"
check "explicit missing configuration refused" \
	sh -c "! '$BIN' -n -c '$T/absent' -t KEY_BRIGHTNESSUP"
check "--help" "$BIN" --help
check "--version" "$BIN" --version
check "invalid option refused" sh -c "! '$BIN' --bogus"

echo "$ok tests passed, $failed failures"
[ "$failed" -eq 0 ]
