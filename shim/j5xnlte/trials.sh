#!/bin/sh
# j5xnlte device trials. Run from anywhere on the machine with the device in
# lk2nd fastboot; the script tells you what to do at every step.
#
#   ./shim/j5xnlte/trials.sh a        Trial A: live entry observable (white
#                                     band on the AMOLED = XNU entered)
#   ./shim/j5xnlte/trials.sh b        Trial B: display probe (DIAG=2), fb
#                                     node + splash refresh, as a band
#   ./shim/j5xnlte/trials.sh c        Trial C: PSCI probe (DIAG=3), return
#                                     value as the poweroff delay
#
# Memory is NOT readable after a reset: IMEM 0x0860065c reads 0 after any
# warm cycle and scratch DRAM 0x92000000 reads 0xffffffff after lk2nd's own
# reboot — the boot ROM/SBL wipes both on every reset (device-proven
# 2026-10-23). Every observable here is live: what the AMOLED shows or how
# long the phone takes to power off. `fastboot oem debug readl` still works
# in-session (while fastboot is up), which the probes use for their
# in-session facts at 0x92000000.
set -eu
cd "$(dirname "$0")/../.."

IMG=shim/j5xnlte/out/j5xnlte-boot.img
PAYLOAD=/tmp/opencode/payload-xnu-j5xnlte.bin

die() { echo "ERROR: $*" >&2; exit 1; }
need_fastboot() {
	[ -n "$(fastboot devices 2>/dev/null)" ] || die "no device in fastboot"
}
wait_gone() {	# fastboot disappears within ~2 s of booting the payload
	for _ in $(seq 1 10); do
		[ -z "$(fastboot devices 2>/dev/null)" ] && return 0
		sleep 1
	done
	return 1
}
# $1: what the phone does after the boot; $2: what you do to get back.
# Empty args print nothing (keeps the one-liner prompts out of trial A).
poll_back() {
	echo
	[ -n "${1:-}" ] && echo ">>> $1"
	[ -n "${2:-}" ] && echo ">>> $2"
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
	echo "=== Trial A: live entry observable (white band on the AMOLED) ==="
	echo "The loader stashes a fb mailbox at scratch DRAM 0x92000000 in the"
	echo "same boot; XNU draws a white 64x32 band at the fb top-left when it"
	echo "reaches its entry. Memory is NOT readable after a reset (IMEM and"
	echo "scratch DRAM are both wiped by the boot ROM/SBL) - the band on the"
	echo "screen is the observable; no readl."
	echo "Booting; fastboot disappears in ~2 s..."
	fastboot boot "$IMG"
	wait_gone && echo "fastboot gone: the shim jumped (a loader failure stage would keep it alive 5+ s)." || \
		echo "WARNING: fastboot still alive - the loader may have failed."
	echo
	echo ">>> NOW LOOK AT THE AMOLED:"
	echo ">>>   WHITE BAND at the top-left = XNU reached its entry (SUCCESS)."
	echo ">>>   Splash unchanged = XNU wedged before the draw (or the MDP"
	echo ">>>   stopped refreshing through the jump)."
	echo ">>> Force-reboot (hold POWER ~10 s, release, hold VOLUMEDOWN) to get"
	echo ">>> fastboot back AFTER checking the screen."
	poll_back
	;;
b)
	need_fastboot
	echo "=== Trial B: display probe (DIAG=2, lk2nd.pass-simplefb fb node) ==="
	DIAG=2 sh shim/j5xnlte/build.sh "$PAYLOAD" >/dev/null
	echo "Booting; if the fb node parses, the probe draws a WHITE 64x32 BAND"
	echo "at the splash top-left, holds it ~10 s, then the phone turns OFF."
	fastboot boot "$IMG"
	echo
	echo ">>> WATCH THE AMOLED for ~10 s after the boot:"
	echo ">>>   WHITE BAND at the top-left = the fb node exists (base/stride"
	echo ">>>   parsed) AND the MDP keeps refreshing from DRAM - the same"
	echo ">>>   assumption trial A relies on (SUCCESS)."
	echo ">>>   No band, phone powered off = node missing (no splash detected"
	echo ">>>   or the cmdline arg did not flow) or a non-32bpp format."
	poll_back "Phone OFF: press POWER, HOLD VOLUMEDOWN until fastboot is back."
	restore_full_loader
	;;
c)
	need_fastboot
	echo "=== Trial C: PSCI CPU_ON via smc #0 alone (DIAG=3) ==="
	DIAG=3 sh shim/j5xnlte/build.sh >/dev/null
	echo "Booting; the poweroff delay IS the return value (readback is dead):"
	echo "  ~2 s off  = PSCI CPU_ON returned 0 (success)"
	echo "  ~10 s off = a PSCI error code (-1 NOT_SUPPORTED, -2 DENIED, ...)"
	echo "  hang      = the smc itself hung"
	fastboot boot "$IMG"
	poll_back "Phone OFF: press POWER, HOLD VOLUMEDOWN until fastboot is back. Count the seconds between the boot and the poweroff." \
		"If the phone boots ANDROID instead, the trial is lost: re-run './shim/j5xnlte/trials.sh c'."
	restore_full_loader
	;;
*)
	echo "usage: $0 a|b|c"
	;;
esac
