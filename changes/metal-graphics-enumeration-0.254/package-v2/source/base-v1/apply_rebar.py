"""Apply/restore the verified single OpenCore BAR quirk change on the development EFI."""
import argparse
import hashlib
import json
import os
import pathlib
import plistlib
import subprocess
import tempfile
from datetime import datetime, timezone

ROOT = pathlib.Path(__file__).resolve().parent
CHANGE = ROOT / "changes/rebar-20260906"
TARGET = pathlib.Path("/Volumes/EFI/EFI/OC/config.plist")
PARTITION = "A72EACA9-F9DF-4CF3-8B44-8BAC6BA1A7F3"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def replace_atomically(data):
    descriptor, name = tempfile.mkstemp(prefix=".rtx-config-", dir=TARGET.parent)
    tmp = pathlib.Path(name)
    try:
        with os.fdopen(descriptor, "wb") as file:
            file.write(data)
            file.flush()
            os.fsync(file.fileno())
        os.replace(tmp, TARGET)
    finally:
        if tmp.exists():
            tmp.unlink()


def run(action):
    if os.geteuid() != 0:
        raise RuntimeError("Run with sudo; the target is the verified development EFI")
    info = plistlib.loads(subprocess.check_output(["diskutil", "info", "-plist", "/Volumes/EFI"]))
    if info.get("DiskUUID", "").upper() != PARTITION or not info.get("Internal"):
        raise RuntimeError("EFI partition identity does not match the internal development disk")
    if info.get("DeviceIdentifier") != "disk0s1" or info.get("MountPoint") != "/Volumes/EFI":
        raise RuntimeError("Unexpected EFI device or mount point")
    for path in (TARGET, *TARGET.parents):
        if path.is_symlink():
            raise RuntimeError("Refusing a symbolic-link target")
    manifest = json.loads((CHANGE / "manifest.json").read_text())
    original = (CHANGE / "config.original.plist").read_bytes()
    candidate = (CHANGE / "config.candidate.plist").read_bytes()
    if manifest["target"] != str(TARGET) or manifest["partition_uuid"] != PARTITION:
        raise RuntimeError("Unexpected change manifest target")
    if sha(original) != manifest["original_sha256"] or sha(candidate) != manifest["candidate_sha256"]:
        raise RuntimeError("Original/candidate integrity check failed")
    before, after = plistlib.loads(original), plistlib.loads(candidate)
    if before["Booter"]["Quirks"]["ResizeAppleGpuBars"] != -1 or after["Booter"]["Quirks"]["ResizeAppleGpuBars"] != 0:
        raise RuntimeError("Unexpected quirk values")
    after["Booter"]["Quirks"]["ResizeAppleGpuBars"] = -1
    if before != after:
        raise RuntimeError("Candidate changes more than ResizeAppleGpuBars")
    validator = ROOT / "vendor/opencore-1.0.7/ocvalidate"
    subprocess.run([str(validator), str(CHANGE / "config.candidate.plist")], check=True)
    reference = (ROOT / "vendor/opencore-1.0.7/OpenCore.reference.efi").read_bytes()
    if sha((TARGET.parent / "OpenCore.efi").read_bytes()) != sha(reference):
        raise RuntimeError("Installed OpenCore no longer matches validated version 1.0.7")
    current = TARGET.read_bytes()
    expected, desired = (original, candidate) if action == "apply" else (candidate, original)
    if current == desired:
        print("Requested configuration is already installed and verified")
        return
    if current != expected:
        raise RuntimeError("Active config has changed; refusing to overwrite it")
    backup = TARGET.with_name("config.before-rtx-rebar-20260906.plist")
    if backup.exists():
        if backup.read_bytes() != original:
            raise RuntimeError("Existing EFI backup does not match original")
    else:
        with backup.open("xb") as file:
            file.write(original)
            file.flush()
            os.fsync(file.fileno())
    if backup.read_bytes() != original:
        raise RuntimeError("EFI backup verification failed")
    replace_atomically(desired)
    if TARGET.read_bytes() != desired:
        replace_atomically(current)
        raise RuntimeError("Readback mismatch; restored previous config")
    result = {"utc": datetime.now(timezone.utc).isoformat(), "action": action,
              "target": str(TARGET), "backup": str(backup), "sha256": sha(desired),
              "ResizeAppleGpuBars": plistlib.loads(desired)["Booter"]["Quirks"]["ResizeAppleGpuBars"],
              "restart_performed": False}
    (CHANGE / (action + "-result.json")).write_text(json.dumps(result, indent=2))
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("apply", "restore"))
    run(parser.parse_args().action)
