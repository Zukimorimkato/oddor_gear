#!/bin/bash

set -e

VERSION="1.0"
MODULE="oddor_gear"
PACKAGE="oddor-gear"

echo "Installing ZSC ODDOR-GEAR DKMS module..."

# Check if all needed tools are installed
for cmd in dkms make gcc; do
    command -v "$cmd" &>/dev/null || { echo "ERROR: $cmd not found. Install it first."; exit 1; }
done

# Check if running as root, if not use sudo
if [ "$EUID" -ne 0 ]; then
    echo "Running as non-root user, using sudo for elevated privileges..."
    SUDO_CMD="sudo"
else
    SUDO_CMD=""
fi

# Check if already installed and remove if so
if $SUDO_CMD dkms status | grep -q "$PACKAGE/$VERSION"; then
    echo "Found existing installation, removing first..."
    $SUDO_CMD dkms remove -m $PACKAGE -v $VERSION --all
    $SUDO_CMD rm -rf /usr/src/${PACKAGE}-${VERSION}
fi

# Create module directory
$SUDO_CMD mkdir -p /usr/src/${PACKAGE}-${VERSION}

# Copy source files
$SUDO_CMD cp dkms.conf Makefile oddor_gear.c /usr/src/${PACKAGE}-${VERSION}/

# Make sure Makefile is executable
$SUDO_CMD chmod +x /usr/src/${PACKAGE}-${VERSION}/Makefile

# Add to DKMS
echo "Adding module to DKMS..."
$SUDO_CMD dkms add -m ${PACKAGE} -v ${VERSION}

# Build module with verbose output
echo "Building module..."
$SUDO_CMD dkms build -m ${PACKAGE} -v ${VERSION} --verbose

# Install module
echo "Installing module..."
$SUDO_CMD dkms install -m ${PACKAGE} -v ${VERSION}

# Load module immediately
echo "Loading module..."
$SUDO_CMD modprobe ${MODULE} 2>/dev/null || true

# Create udev rule for permissions
echo "Creating udev rule..."
$SUDO_CMD bash -c 'cat > /etc/udev/rules.d/99-oddor-gear.rules << EOF
# ZSC ODDOR-GEAR Shifter
SUBSYSTEM=="usb", ATTR{idVendor}=="4785", ATTR{idProduct}=="7353", MODE="0666", GROUP="plugdev"
EOF'

# Reload udev rules
$SUDO_CMD udevadm control --reload-rules
$SUDO_CMD udevadm trigger

echo ""
echo "=== Installation Summary ==="
echo "Module installed: $(ls -la /lib/modules/$(uname -r)/extra/ 2>/dev/null | grep oddor || echo 'Check manually')"
echo "DKMS status:"
$SUDO_CMD dkms status | grep oddor || echo "Not found in DKMS"
echo ""
echo "Installation complete!"
echo "The shifter should now work with games that accept joystick input for gears."
