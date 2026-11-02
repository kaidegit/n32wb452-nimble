"""Offline checks: config changes reach CRC calculation; invalid input never needs HID."""
import copy
import json
import subprocess
import sys
import tempfile
from pathlib import Path

executable, fixture = sys.argv[1:]
config = json.loads(Path(fixture).read_text())


def check(document, expected_success, expected_output):
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "boot.json"
        path.write_text(json.dumps(document))
        result = subprocess.run([executable, "--config", str(path), "check-config"],
                                capture_output=True, text=True)
    assert (result.returncode == 0) == expected_success, result.stderr
    assert expected_output in result.stdout + result.stderr, result.stdout + result.stderr
    return result.stdout


check(config, True, "PatchArray: 3200 bytes, CRC=0x0277")
changed = copy.deepcopy(config)
patch = next(step for step in changed["steps"] if step.get("name") == "PatchArray")
patch["hex"] = "01" + patch["hex"][2:]
assert "CRC=0x0277" not in check(changed, True, "PatchArray: 3200 bytes")

for invalid_hex in ["0", "gg", "", "00 " * 16385]:
    changed = copy.deepcopy(config)
    changed["steps"][1]["hex"] = invalid_hex
    check(changed, False, "blectl:")

changed = copy.deepcopy(config)
changed["steps"][1]["command"] = -1
check(changed, False, "array command")
changed = copy.deepcopy(config)
changed["steps"].pop(0)
check(changed, False, "preceding start/reset")
changed = copy.deepcopy(config)
changed["steps"].pop()
check(changed, False, "end with hci-enable")
changed = copy.deepcopy(config)
changed["steps"][1]["op"] = "typo"
check(changed, False, "unknown boot operation")
changed = copy.deepcopy(config)
changed["steps"][3]["name"] = "CopyArray"
check(changed, False, "duplicate array name")
changed = copy.deepcopy(config)
changed["steps"] = {}
check(changed, False, "steps must be an array")
print("boot config validation passed")
