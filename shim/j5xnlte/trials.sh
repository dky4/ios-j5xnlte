#!/bin/sh
# j5xnlte device trials: the fastboot sequence with the scratch-DRAM marker
# words read back. Run from anywhere on the machine with the device in lk2nd
# fastboot; the script tells you what to do at every step.
#
#   ./shim/j5xnlte/trials.sh a        Trial A: real payload, "XNU1" marker
#   ./shim/j5xnlte/trials.sh b        Trial B: display probe (DIAG=2) fb facts
#   ./shim/j5xnlte/trials.sh c        Trial C: PSCI probe (DIAG=3) return value
#   ./shim/j5xnlte/trials.sh persist  DRAM persistence across lk2nd reboot
#
# Marker word: 0x92000000 (lk2nd's scratch DRAM, 32 MB into SCRATCH_ADDR
# 0x90000000), readable with the unmodified installed lk2nd via
# 'fastboot oem debug readl'. The earlier IMEM word 0x0860065c did NOT
# persist across resets (boot ROM/SBL re-initializes IMEM); DRAM does.
set -eu
cd "$(dirname "$0")/../.."

IMG=shim/j5xnlte/out/j5xnlte-boot.img
PAYLOAD=/tmp/opencode/payload-xnu-j5xnlte.bin
MARK=0x92000000

die() { echo "ERROR: $*" >&2; exit 1; }
need_fastboot() {
	[ -n "$(fastboot devices 2>/dev/null)" ] || die "no device in fastboot"
}
readl() {
	fastboot oem debug readl "$1" 2>&1 | awk '/\(bootloader\)/{print $2}'
}
wait_gone() {	# fastboot disappears within ~2 s of booting the payload
	for _ in $(seq 1 10); do
		[ -z "$(fastboot devices 2>/dev/null)" ] && return 0
		sleep 1
	done
	return 1
}
# $1: what the phone does after the boot; $2: what you do to get back
poll_back() {
	echo
	echo ">>> $1"
	echo ">>> $2"
	for _ in $(seq 1 150); do
		[ -n "$(fastboot devices 2>/dev/null)" ] && { echo "FASTBOOT BACK"; return 0; }
		sleep 2
	done
	die "fastboot never came back (5 min)"
}
restore_full_loader() {
	echo "Re-packing the active trial-A image (full loader + payload)..."
	sh shim/j5xnlte/build.sh "$PAYLOAD" >/dev/null
}

cmd="${1:-a}"
case "$cmd" in
a)
	need_fastboot
	echo "=== Trial A: real payload (full loader + marker-fix kernel) ==="
	echo "Booting; the phone will HANG at XNU (fastboot disappears in ~2 s)..."
	fastboot boot "$IMG"
	if wait_gone; then
		echo "fastboot gone: the shim jumped (a loader failure stage would keep it alive 5+ s)."
	else
		echo "WARNING: fastboot still alive - the loader may have failed; check the stage read below."
	fi
	poll_back "NOW force-reboot: hold POWER ~10 s, release, then hold VOLUMEDOWN until fastboot is back." \
		"If the phone boots ANDROID instead, the trial is lost: just re-run './shim/j5xnlte/trials.sh a'."
	V=$(readl "$MARK")
	case "$V" in
	0x584e5531) echo "SUCCESS: \"XNU1\" - XNU reached its marker write site in start.s." ;;
	0x4a355800) echo "Loader stage 0: the shim ran, XNU wedged BEFORE its own marker." ;;
	0x4a355801|0x4a355802|0x4a355803|0x4a355804)
		echo "Loader FAILED at stage ${V#0x4a35580} (see shim/j5xnlte/README.md)." ;;
	0x00000000) echo "Word is 0: DRAM was cleared by the reset, or the write never landed." ;;
	*) echo "Unexpected: $V" ;;
	esac
	;;
b)
	need_fastboot
	echo "=== Trial B: display probe (DIAG=2, lk2nd.pass-simplefb fb facts) ==="
	DIAG=2 sh shim/j5xnlte/build.sh "$PAYLOAD" >/dev/null
	echo "Booting; the phone turns OFF ~1 s after the boot if the probe ran..."
	fastboot boot "$IMG"
	poll_back "Press POWER and HOLD VOLUMEDOWN until fastboot is back (force-reboot with POWER ~10 s if it hung)." \
		"If the phone boots ANDROID instead, the trial is lost: re-run './shim/j5xnlte/trials.sh b'."
	echo "fb facts (00 base, 04 size, 08 width, 0c height, 10 stride, 14 format):"
	for off in 0x92000000 0x92000004 0x92000008 0x9200000c 0x92000010 0x92000014; do
		echo "  $off = $(readl "$off")"
	done
	echo "0x4a355801 at 0x92000000 = the fb node is missing (lk2nd detected no splash or the cmdline arg did not flow)."
	restore_full_loader
	;;
c)
	need_fastboot
	echo "=== Trial C: PSCI CPU_ON via smc #0 alone (DIAG=3) ==="
	DIAG=3 sh shim/j5xnlte/build.sh >/dev/null
	echo "Booting; the phone turns OFF ~1 s after the boot if the smc returned..."
	fastboot boot "$IMG"
	poll_back "Press POWER and HOLD VOLUMEDOWN until fastboot is back (force-reboot with POWER ~10 s if it hung)." \
		"If the phone boots ANDROID instead, the trial is lost: re-run './shim/j5xnlte/trials.sh c'."
	V=$(readl "$MARK")
	case "$V" in
	0x00000000) echo "PSCI CPU_ON returned 0 (success): PSCI works via smc from the TZ-milestone EL1 state." ;;
	0x4a355800) echo "0x4a355800 with a HANG = the smc itself hung; PSCI CPU_ON unusable from this state." ;;
	*) echo "PSCI error code $V (negative: -1 NOT_SUPPORTED, -2 DENIED, -3 INVALID...)" ;;
	esac
	restore_full_loader
	;;
persist)
	need_fastboot
	echo "=== scratch-DRAM persistence across lk2nd's own reboot ==="
	fastboot oem debug writel "$MARK" 0x4a355800
	echo "Read back now: $(readl "$MARK") (expect 0x4a355800)"
	echo ">>> 'fastboot reboot' runs next: lk2nd hard-resets. HOLD VOLUMEDOWN as it reboots."
	fastboot reboot
	poll_back "HOLD VOLUMEDOWN until fastboot is back (if it booted ANDROID, the word read below is still valid)." \
		""
	V=$(readl "$MARK")
	case "$V" in
	0x4a355800) echo "DRAM SURVIVES lk2nd's hard reset - trial A's 0 reads mean the shim write never landed." ;;
	0x00000000) echo "DRAM CLEARED by lk2nd's hard reset - markers need yet another home." ;;
	*) echo "Unexpected: $V" ;;
	esac
	;;
*)
	echo "usage: $0 a|b|c|persist"
	;;
esac
