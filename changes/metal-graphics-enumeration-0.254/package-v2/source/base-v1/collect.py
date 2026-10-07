"""Read-only macOS baseline. Writes only to the specified fresh report directory."""
import argparse
import hashlib
import json
import pathlib
import plistlib
import struct
import subprocess
from datetime import datetime, timezone


def pci_ranges(blob):
    """Decode x86 macOS IORegistry PCI address cells (little-endian uint32)."""
    if not isinstance(blob, bytes) or len(blob) % 20:
        raise ValueError("PCI ranges must contain complete five-cell entries")
    ranges = []
    for hi, mid, lo, size_hi, size_lo in struct.iter_unpack("<5I", blob):
        ranges.append({"register": hi & 255, "space": (hi >> 24) & 3,
                       "prefetchable": bool(hi & (1 << 30)),
                       "address": (mid << 32) | lo, "length": (size_hi << 32) | size_lo})
    return ranges


def serializable(obj):
    if isinstance(obj, bytes):
        return {"hex": obj.hex()}
    if isinstance(obj, dict):
        return {str(k): serializable(v) for k, v in obj.items()}
    if isinstance(obj, (list, tuple)):
        return [serializable(v) for v in obj]
    return obj


def walk(nodes):
    for node in nodes:
        yield node
        yield from walk(node.get("IORegistryEntryChildren", []))


def as_int(value):
    return int.from_bytes(value, "little") if isinstance(value, bytes) else value


def command(argv):
    try:
        p = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=45)
        return {"argv": argv, "returncode": p.returncode,
                "stdout": p.stdout.decode("utf-8", errors="replace"),
                "stderr": p.stderr.decode("utf-8", errors="replace")}
    except (subprocess.TimeoutExpired, OSError) as e:
        return {"argv": argv, "error": str(e)}


def config_summary(path):
    raw = path.read_bytes()
    p = plistlib.loads(raw)
    return {"path": str(path), "sha256": hashlib.sha256(raw).hexdigest(),
            "acpi_add": [{k: x.get(k) for k in ("Path", "Enabled", "Comment")}
                         for x in p.get("ACPI", {}).get("Add", [])],
            "kernel_add": [{k: x.get(k) for k in ("BundlePath", "Enabled")}
                           for x in p.get("Kernel", {}).get("Add", [])],
            "booter_quirks": p.get("Booter", {}).get("Quirks", {}),
            "uefi_quirks": p.get("UEFI", {}).get("Quirks", {}),
            "kernel_quirks": p.get("Kernel", {}).get("Quirks", {})}


def collect(out):
    out.mkdir(parents=True, exist_ok=False)
    report = {"utc": datetime.now(timezone.utc).isoformat(), "read_only": True}
    report["commands"] = {name: command(argv) for name, argv in {
        "os": ["sw_vers"], "cpu": ["sysctl", "-n", "machdep.cpu.brand_string"],
        "boot_args": ["nvram", "boot-args"], "compiler": ["clang", "--version"],
        "ditto": ["pgrep", "-x", "ditto"],
        "metal_displays": ["system_profiler", "SPDisplaysDataType"],
        "extensions": ["systemextensionsctl", "list"],
        "sip": ["csrutil", "status"],
        "authenticated_root": ["csrutil", "authenticated-root", "status"],
    }.items()}
    raw = subprocess.check_output(["ioreg", "-a", "-l", "-w0", "-r", "-c", "IOPCIDevice"], timeout=45)
    report["gpus"] = []
    seen = set()
    for node in walk(plistlib.loads(raw)):
        if as_int(node.get("vendor-id")) not in (0x10de, 0x1002):
            continue
        if (as_int(node.get("class-code", 0)) >> 16) != 3:
            continue
        ident = node.get("IORegistryEntryID", node.get("pcidebug"))
        if ident in seen:
            continue
        seen.add(ident)
        filtered = {k: v for k, v in node.items() if k != "IORegistryEntryChildren"}
        filtered["child_drivers"] = [n.get("IOObjectClass", n.get("IORegistryEntryName"))
                                      for n in node.get("IORegistryEntryChildren", [])]
        for key in ("reg", "assigned-addresses"):
            if key in node:
                try:
                    filtered[key + "_decoded"] = pci_ranges(node[key])
                except ValueError as e:
                    filtered[key + "_decode_error"] = str(e)
        report["gpus"].append(serializable(filtered))
    report["configs"] = []
    report["acpi_hashes"] = {}
    for root in ("/Volumes/EFI", "/Volumes/OCRESCUE"):
        config = pathlib.Path(root) / "EFI/OC/config.plist"
        if config.is_file():
            report["configs"].append(config_summary(config))
        acpi = pathlib.Path(root) / "EFI/OC/ACPI"
        for path in acpi.glob("*.aml"):
            report["acpi_hashes"][str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
    (out / "baseline.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({"report": str(out / "baseline.json"), "gpu_count": len(report["gpus"]),
                      "nvidia_ranges": [g.get("assigned-addresses_decoded") for g in report["gpus"]
                                        if g.get("vendor-id") == {"hex": "de100000"}]}, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=pathlib.Path, required=True)
    collect(parser.parse_args().out)
