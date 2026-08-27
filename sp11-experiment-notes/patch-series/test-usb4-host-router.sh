#!/bin/sh
set -eu

# Run after booting the v4 LCD-PHY test kernel. Keep the generic NHI reset
# disabled while the Qualcomm router firmware path is being brought up.

sudo modprobe -r qcom_usb4_hr 2>/dev/null || true
sudo modprobe -r thunderbolt 2>/dev/null || true
sudo modprobe -r qcom_usb4_hr_overlay 2>/dev/null || true

sudo modprobe qcom_usb4_hr_overlay
# The overlay may cause udev to auto-load qcom_usb4_hr with activate=0.
# Remove that instance before loading it explicitly with activate=1.
sudo modprobe -r qcom_usb4_hr 2>/dev/null || true
sudo modprobe thunderbolt host_reset=0
sudo modprobe qcom_usb4_hr activate=1

echo
echo '=== kernel identity ==='
uname -a
echo
echo '=== USB4 / Thunderbolt diagnostics ==='
sudo dmesg -T | grep -Ei 'usb4|thunderbolt|nhi|qmp|phy|firmware|ring|timeout|overflow' || true
echo
echo '=== Thunderbolt topology ==='
if command -v boltctl >/dev/null 2>&1; then
	boltctl list || true
fi
find /sys/bus/thunderbolt/devices -maxdepth 2 -type f -name authorized \
	-print -exec sh -c 'printf "%s: " "$1"; cat "$1"' sh {} \; 2>/dev/null || true
