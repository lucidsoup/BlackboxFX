#!/usr/bin/env python3
"""Read-only BlackBox probe.

Finds the M-VAVE ANN BlackBox over USB and reports what the host can see
without sending anything to the device beyond the standard descriptor reads
every operating system performs on plug-in:

  * USB vendor/product IDs, strings and firmware (bcdDevice) version
  * every interface: audio streaming, MIDI, HID (the updater uses HID)
  * endpoints and their formats
  * matching MIDI ports and audio devices, if mido / sounddevice are installed

It never claims an interface, never sends MIDI, HID or vendor requests and
never touches the updater. Paste the output (or the --json file) back into
the BlackboxFX issue tracker or chat so the port can be planned.

    pip install pyusb            # required (plus libusb on macOS/Windows)
    pip install mido python-rtmidi sounddevice   # optional extras
    python3 blackbox_probe.py [--json report.json]
"""
import argparse
import json
import sys

# "ann" alone would match words like "channel"
MATCH_WORDS = ("blackbox", "black box", "m-vave", "mvave", "cuvave")

CLASS_NAMES = {
    0x01: "audio",
    0x03: "hid",
    0xFE: "application-specific",
    0xFF: "vendor-specific",
}
AUDIO_SUBCLASS = {1: "control", 2: "streaming", 3: "midi-streaming"}


def safe_string(dev, index):
    if not index:
        return None
    try:
        import usb.util
        return usb.util.get_string(dev, index)
    except Exception as exc:  # permissions or a device that ignores the request
        return f"<unreadable: {exc.__class__.__name__}>"


def bcd(v):
    return f"{v >> 8:x}.{v & 0xFF:02x}"


def describe(dev):
    info = {
        "vid": f"{dev.idVendor:04x}",
        "pid": f"{dev.idProduct:04x}",
        "bcdDevice": bcd(dev.bcdDevice),
        "manufacturer": safe_string(dev, dev.iManufacturer),
        "product": safe_string(dev, dev.iProduct),
        "serial_present": bool(dev.iSerialNumber),  # value withheld on purpose
        "configurations": [],
    }
    for cfg in dev:
        c = {"value": cfg.bConfigurationValue, "max_power_ma": cfg.bMaxPower * 2,
             "interfaces": []}
        for intf in cfg:
            cls = intf.bInterfaceClass
            kind = CLASS_NAMES.get(cls, f"class 0x{cls:02x}")
            if cls == 0x01:
                kind += "/" + AUDIO_SUBCLASS.get(intf.bInterfaceSubClass, "?")
            c["interfaces"].append({
                "number": intf.bInterfaceNumber,
                "alt": intf.bAlternateSetting,
                "kind": kind,
                "name": safe_string(dev, intf.iInterface),
                "endpoints": [{
                    "address": f"0x{ep.bEndpointAddress:02x}",
                    "direction": "IN (device->host)" if ep.bEndpointAddress & 0x80
                                 else "OUT (host->device)",
                    "type": ("control", "isochronous", "bulk", "interrupt")[ep.bmAttributes & 3],
                    "max_packet": ep.wMaxPacketSize,
                } for ep in intf],
            })
        info["configurations"].append(c)
    return info


def looks_like_blackbox(info):
    text = " ".join(str(info.get(k) or "") for k in ("manufacturer", "product")).lower()
    return any(w in text for w in MATCH_WORDS)


def has_audio(info):
    return any(i["kind"].startswith("audio")
               for c in info["configurations"] for i in c["interfaces"])


def extras():
    out = {}
    try:
        import mido
        out["midi_inputs"] = mido.get_input_names()
        out["midi_outputs"] = mido.get_output_names()
    except Exception as exc:
        out["midi"] = f"skipped ({exc.__class__.__name__}: install mido python-rtmidi)"
    try:
        import sounddevice as sd
        out["audio_devices"] = [
            {"name": d["name"], "in": d["max_input_channels"], "out": d["max_output_channels"],
             "rate": d["default_samplerate"]}
            for d in sd.query_devices()
        ]
    except Exception as exc:
        out["audio"] = f"skipped ({exc.__class__.__name__}: install sounddevice)"
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--json", help="also write the report to this file")
    ap.add_argument("--all-audio", action="store_true",
                    help="report every USB audio device, not just BlackBox matches")
    args = ap.parse_args()

    try:
        import usb.core
    except ImportError:
        sys.exit("pyusb is not installed: pip install pyusb")

    try:
        devices = list(usb.core.find(find_all=True))
    except usb.core.NoBackendError:
        sys.exit("no libusb backend found; install libusb (brew install libusb / "
                 "use the libusb-package wheel on Windows)")

    found = []
    for dev in devices:
        try:
            info = describe(dev)
        except Exception as exc:
            print(f"skipping {dev.idVendor:04x}:{dev.idProduct:04x}: {exc}", file=sys.stderr)
            continue
        if looks_like_blackbox(info) or (args.all_audio and has_audio(info)):
            found.append(info)

    if not found:
        print("No BlackBox found. Check the cable, then rerun with --all-audio;")
        print("if strings show as <unreadable>, rerun with sudo (Linux) or as admin.")
    report = {"devices": found, "host": extras()}
    print(json.dumps(report, indent=2))
    if args.json:
        with open(args.json, "w") as f:
            json.dump(report, f, indent=2)
        print(f"saved {args.json}", file=sys.stderr)


if __name__ == "__main__":
    main()
