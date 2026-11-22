#!/bin/bash
set -e

source "$(dirname "$0")/version.sh"
MODULE="oddor_gear"
PACKAGE="oddor-gear"

echo "Installing ZSC ODDOR-GEAR DKMS module..."

# Check if running as root, if not use sudo
if [ "$EUID" -ne 0 ]; then
    echo "Running as non-root user, using sudo for elevated privileges..."
    SUDO_CMD="sudo"
else
    SUDO_CMD=""
fi

# Check for required tools
for cmd in dkms make gcc; do
    command -v "$cmd" &>/dev/null || {
        echo "ERROR: '$cmd' not found. Install it first (e.g. sudo apt install $cmd)."
        exit 1
    }
done

# Show detected USB devices to help user confirm VID/PID
echo ""
echo "Detected USB devices (confirm your shifter is listed as 4785:7353):"
lsusb | grep "4785:7353" && echo "  -> Shifter found!" || echo "  -> Shifter NOT detected. Check USB connection or update VID/PID in oddor_gear.c"
echo ""

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

# Add to DKMS
echo "Adding module to DKMS..."
$SUDO_CMD dkms add -m ${PACKAGE} -v ${VERSION}

# Build module with verbose output
echo "Building module..."
$SUDO_CMD dkms build -m ${PACKAGE} -v ${VERSION} -k "$(uname -r)" --verbose

# Install module
echo "Installing module..."
$SUDO_CMD dkms install -m ${PACKAGE} -v ${VERSION} -k "$(uname -r)"

# Load module immediately (unload any previously loaded version first,
# otherwise modprobe is a no-op and the old code keeps running)
echo "Loading module..."
$SUDO_CMD modprobe -r ${MODULE} 2>/dev/null || true
$SUDO_CMD modprobe ${MODULE} || echo "WARNING: could not load ${MODULE} now; it will load on next boot/replug."

# Create udev rule for permissions.
# The game talks to the kernel input device (/dev/input/event*, js*), not to
# raw USB, so only that node is opened up - and only to the user logged in at
# the seat (uaccess), not world-writable. The file must sort before
# 73-seat-late.rules, which is where uaccess tags are applied.
echo "Creating udev rule..."
$SUDO_CMD rm -f /etc/udev/rules.d/99-oddor-gear.rules   # old, world-writable rule
$SUDO_CMD bash -c 'cat > /etc/udev/rules.d/70-oddor-gear.rules << EOF
# ZSC ODDOR-GEAR Shifter: let the logged-in user read the input device
SUBSYSTEM=="input", ATTRS{idVendor}=="4785", ATTRS{idProduct}=="7353", ENV{ID_INPUT_JOYSTICK}="1", TAG+="uaccess"
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
echo "If it stops working after a kernel update, run: sudo dkms autoinstall"
