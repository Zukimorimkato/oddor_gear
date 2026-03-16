# oddor_gear

Linux kernel driver for the [ZSC ODDOR H-pattern shifter](https://www.aliexpress.com/item/1005007543763824.html). Translates the shifter's USB HID data into standard joystick button events, making it work in racing simulators like BeamNG.drive under Linux.

---

## Hardware

The driver targets USB device `4785:7353`. If your shifter has a different VID:PID (check with `lsusb`), update the IDs in `oddor_gear.c` before installing:

```c
#define ODDOR_VENDOR_ID   0x4785
#define ODDOR_PRODUCT_ID  0x7353
```

---

## Prerequisites

Install the required packages before proceeding:

**Debian/Ubuntu:**
```bash
sudo apt install linux-headers-$(uname -r) gcc make dkms
```

**Fedora/RHEL:**
```bash
sudo dnf install kernel-devel gcc make dkms
```

**Arch Linux:**
```bash
sudo pacman -S linux-headers gcc make dkms
```

---

## Installation

```bash
git clone https://github.com/Zukimorimkato/oddor_gear.git
cd oddor_gear
chmod +x *.sh
./install.sh
```

The script will:
1. Check that all dependencies are present
2. Verify the USB device is connected
3. Copy sources to `/usr/src/` and register with DKMS
4. Build and load the kernel module
5. Install a udev rule so the device is accessible without root

After installation, **reboot** or run `sudo modprobe oddor_gear` to load the module immediately.

> **Note:** DKMS will automatically recompile the module after kernel updates.

---

## Verifying the Device

After loading the module, confirm the driver is bound:

```bash
lsmod | grep oddor_gear
dmesg | grep -i oddor
```

You should see a new input device appear under `/dev/input/`.

---

## Testing

**Low-level event test (recommended):**
```bash
sudo evtest
```
Select the `ODDOR` device from the list and shift through all gears — you should see button press/release events.

**Joystick test:**
```bash
jstest /dev/input/js0
```
Replace `js0` with the correct device if needed.

---

## Gear Mapping

| Physical position | Linux event       | Button index |
|-------------------|-------------------|--------------|
| Gear 1            | BTN_TRIGGER_HAPPY1 | 0 |
| Gear 2            | BTN_TRIGGER_HAPPY2 | 1 |
| Gear 3            | BTN_TRIGGER_HAPPY3 | 2 |
| Gear 4            | BTN_TRIGGER_HAPPY4 | 3 |
| Gear 5            | BTN_TRIGGER_HAPPY5 | 4 |
| Gear 6            | BTN_TRIGGER_HAPPY6 | 5 |
| Gear 7            | BTN_TRIGGER_HAPPY7 | 6 |
| Reverse           | BTN_TRIGGER_HAPPY8 | 7 |
| Neutral           | *(no event)*       | — |

---

## Debugging

Run the included diagnostic script for a full status report:

```bash
./debug.sh
```

It checks module load status, USB device presence, kernel logs, and input device paths.

For raw kernel logs:
```bash
dmesg | grep -i oddor | tail -20
```

---

## Uninstallation

```bash
./uninstall.sh
```

This removes the DKMS module, source files from `/usr/src/`, and the udev rule.

---

## Contributing

Pull requests are welcome. If you fix a bug or improve compatibility, open a PR — testing and merging will happen on a best-effort basis.

---

## License

GPL v3 — see [LICENSE](LICENSE).
