#!/bin/bash

set -e

VERSION="1.0"
PACKAGE="oddor-gear"

echo "Removing ZSC ODDOR-GEAR DKMS module..."

# Check if running as root, if not use sudo
if [ "$EUID" -ne 0 ]; then
    echo "Running as non-root user, using sudo for elevated privileges..."
    SUDO_CMD="sudo"
else
    SUDO_CMD=""
fi

# Remove from DKMS
$SUDO_CMD dkms remove -m ${PACKAGE} -v ${VERSION} --all

# Remove source
$SUDO_CMD rm -rf /usr/src/${PACKAGE}-${VERSION}

# Remove udev rule
$SUDO_CMD rm -f /etc/udev/rules.d/99-oddor-gear.rules

# Reload udev
$SUDO_CMD udevadm control --reload-rules

# Try to unload module if loaded
$SUDO_CMD modprobe -r oddor_gear 2>/dev/null || true

echo "Uninstall complete!"
