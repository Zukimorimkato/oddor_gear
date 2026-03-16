#!/bin/bash
set -e

source "$(dirname "$0")/version.sh"
PACKAGE="oddor-gear"
MODULE="oddor_gear"

echo "Removing ZSC ODDOR-GEAR DKMS module..."

# Check if running as root, if not use sudo
if [ "$EUID" -ne 0 ]; then
    echo "Running as non-root user, using sudo for elevated privileges..."
    SUDO_CMD="sudo"
else
    SUDO_CMD=""
fi

# Unload module first before DKMS tears it down
echo "Unloading module..."
$SUDO_CMD modprobe -r $MODULE 2>/dev/null || true

# Remove from DKMS
$SUDO_CMD dkms remove -m ${PACKAGE} -v ${VERSION} --all

# Remove source
$SUDO_CMD rm -rf /usr/src/${PACKAGE}-${VERSION}

# Remove udev rule
$SUDO_CMD rm -f /etc/udev/rules.d/99-oddor-gear.rules

# Reload udev
$SUDO_CMD udevadm control --reload-rules
$SUDO_CMD udevadm trigger

echo "Uninstall complete!"
