#!/bin/bash
# Debug script for ODDOR-GEAR shifter

echo "=== ODDOR-GEAR Debug Information ==="
echo ""

# Check if module is loaded
echo "1. Module status:"
sudo lsmod | grep oddor_gear || echo "Module not loaded"

echo ""
echo "2. USB device detection:"
lsusb | grep "4785:7353" || echo "Device not found in lsusb"

echo ""
echo "3. Kernel messages related to shifter:"
sudo dmesg | grep -i "oddor\|4785:7353" | tail -20

echo ""
echo "4. Input devices:"
ls -la /dev/input/by-id/ 2>/dev/null | grep -i "oddor\|usb" || echo "No input devices found"

echo ""
echo "5. Testing with evtest:"
echo "   Run 'sudo evtest' and select the ODDOR-GEAR device"
echo "   Move the shifter to see input events"
echo ""
echo "6. Testing gear detection:"
echo "   Run 'sudo cat /proc/bus/input/devices' to see detailed info"

echo ""
echo "7. DKMS status:"
sudo dkms status | grep oddor-gear || echo "DKMS module not found"
