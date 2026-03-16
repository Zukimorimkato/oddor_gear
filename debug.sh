#!/bin/bash
# Debug script for ODDOR-GEAR shifter

echo "=== ODDOR-GEAR Debug Information ==="
echo ""

# Check if module is loaded
echo "1. Module status:"
sudo lsmod | grep oddor_gear || echo "   Module not loaded"

echo ""
echo "2. USB device detection:"
lsusb | grep "4785:7353" && echo "   -> Shifter detected!" || echo "   -> Device not found. Check USB connection or VID/PID in oddor_gear.c"

echo ""
echo "3. Kernel messages related to shifter:"
sudo dmesg | grep -i "oddor\|4785:7353" | tail -20

echo ""
echo "4. Input devices:"
ODDOR_DEV=$(ls /dev/input/by-id/ 2>/dev/null | grep -i "oddor" | head -1)
if [ -n "$ODDOR_DEV" ]; then
    echo "   Found: $ODDOR_DEV"
    echo "   Full path: /dev/input/by-id/$ODDOR_DEV"
else
    ls -la /dev/input/by-id/ 2>/dev/null | grep -i "usb" || echo "   No input devices found"
fi

echo ""
echo "5. Device details:"
sudo cat /proc/bus/input/devices | grep -A8 -i "oddor" || echo "   No ODDOR entry in /proc/bus/input/devices"

echo ""
echo "6. Testing with evtest:"
if command -v evtest &>/dev/null; then
    if [ -n "$ODDOR_DEV" ]; then
        echo "   Run: sudo evtest /dev/input/by-id/$ODDOR_DEV"
    else
        echo "   Run: sudo evtest  (then select the ODDOR-GEAR device)"
    fi
else
    echo "   evtest not installed. Install with: sudo apt install evtest"
fi

echo ""
echo "7. Testing with jstest:"
if command -v jstest &>/dev/null; then
    JS_DEV=$(ls /dev/input/js* 2>/dev/null | head -1)
    [ -n "$JS_DEV" ] && echo "   Run: jstest $JS_DEV" || echo "   No joystick device nodes found under /dev/input/js*"
else
    echo "   jstest not installed. Install with: sudo apt install joystick"
fi

echo ""
echo "8. DKMS status:"
sudo dkms status | grep oddor-gear || echo "   DKMS module not found"
