#!/usr/bin/env python3
"""Export the SDK boot arrays once. blectl reads the resulting JSON at runtime."""
import argparse
import json
import re
from pathlib import Path


def crc16(data: bytes) -> int:
    """CRC-16: poly 0x8005, init 0, MSB first, no reflection or xorout."""
    crc = 0
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ (0x8005 if crc & 0x8000 else 0)) & 0xFFFF
    return crc


def export(source: Path, destination: Path) -> None:
    text = source.read_text()
    steps = [{"op": "start"}]
    for name, command in [("Copy", 0xBB), ("Ramcode", 0xBB),
                          ("Patch", 0xCC), ("Nvds", 0xDD)]:
        match = re.search(rf"{name}Array\[[^]]+\]\s*=\s*\{{(.*?)\}}", text, re.S)
        if not match:
            raise ValueError(f"{name}Array missing from {source}")
        body = re.sub(r"//[^\n]*|/\*.*?\*/", "", match[1], flags=re.S)
        data = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]+)", body))
        length = int(re.search(rf"#define\s+{name}ArrayLength\s+(0x\w+|\d+)", text)[1], 0)
        expected = int(re.search(rf"#define\s+{name}CrcValue\s+(0x\w+|\d+)", text)[1], 0)
        # C permits omitted array initializers, which are zero-filled.
        if len(data) > length:
            raise ValueError(f"too many bytes in {name}Array")
        data += bytes(length - len(data))
        actual = crc16(data)
        if actual != expected:
            raise ValueError(f"{name}: CRC 0x{actual:04x}, SDK 0x{expected:04x}")
        print(f"{name}Array: {len(data)} bytes, CRC=0x{actual:04x}")
        steps.append({"op": "array", "name": name + "Array", "command": command,
                      "hex": " ".join(f"{byte:02x}" for byte in data)})
        if name == "Copy":
            steps.append({"op": "bypass"})
    steps.extend([{"op": "bypass"}, {"op": "hci-enable"}])
    destination.write_text(json.dumps({"version": 1, "steps": steps}, indent=2) + "\n")


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=root / "third_party/sdk/n32wb452_ble_driver/platform/inc/Ramcode.h")
    parser.add_argument("--output", type=Path, default=Path(__file__).with_name("bridge_boot.json"))
    args = parser.parse_args()
    export(args.source, args.output)
