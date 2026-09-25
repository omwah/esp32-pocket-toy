# USB HID Keyboard Tester

A simple full-screen USB keyboard tester for the Hosyond ES3C28P. It shows every
new key press alongside an impatient hippo image.

## Connect the keyboard

The keyboard must connect to the ESP32-S3 **native USB/OTG data port**, not to a
computer. A USB-C OTG adapter or hub is normally required. The board must also
be able to provide 5 V VBUS to the keyboard; use a powered OTG hub if the board
or adapter does not source VBUS. A charge-only cable will not work.

This firmware claims the ESP32-S3 USB OTG controller in host mode. It therefore
does not expose a USB CDC device on that same controller while running. The
USB-Serial/JTAG interface remains usable for flashing on hardware where it is
wired separately.

The tester supports boot-protocol USB keyboards (the usual BIOS-compatible
keyboard interface). It displays printable US-layout keys, modifiers, arrows,
function keys, navigation keys, and the raw HID usage for unknown keys.

## Build

From the repository root:

```sh
micromamba run -n platformio pio run -d projects/usb-hid-tester
```

Upload with:

```sh
micromamba run -n platformio pio run -d projects/usb-hid-tester -t upload
```

The USB HID host class implementation under `lib/ESP32_USB_Host_HID` is derived
from Espressif's HID host example and vendored from
[esp32beans/ESP32_USB_Host_HID](https://github.com/esp32beans/ESP32_USB_Host_HID).
Its Apache-2.0 license is included alongside the source.
