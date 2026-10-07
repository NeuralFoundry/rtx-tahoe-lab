"""Independently validate RTXProbe 0.10 SEC2 signature-fuse evidence."""
import argparse
import hashlib
import json
from pathlib import Path
import plistlib

EFFECTS = {"MMIOReadOnly": True, "FirmwareExecuted": False, "DMATransferExecuted": False,
           "ResetExecuted": False, "BusMasterEnabled": False}


def decode_booter_probe(node):
    if not isinstance(node, dict):
        raise ValueError("Expected a probe object")
    node = dict(node)
    if node.get("ProbeVersion") != "0.10.0" or node.get("Mode") != "gsp-booter-fuse-readonly":
        raise ValueError("Expected the dedicated 0.10 read-only booter probe")
    if node.get("ProbeComplete") is not True:
        raise ValueError("Probe is incomplete")
    for key, value in EFFECTS.items():
        if node.get(key) is not value:
            raise ValueError("Unexpected effect flag: " + key)
    for key in ("ProbePassed", "TargetBDF", "BARTypesValid", "MemoryEnableAttempted", "RestoreVerified"):
        if type(node.get(key)) is not bool:
            raise ValueError("Invalid boolean: " + key)
    numeric = ("TargetIdentity", "TargetSubsystem", "PMCSR", "LinkStatus", "BAR0", "BAR1",
               "BAR0Descriptor", "BAR1Descriptor", "BAR0Length", "BAR1Length", "Boot0First",
               "Boot0Second", "Boot0Reads", "CommandBefore", "CommandDuring", "CommandAfter",
               "BooterFuseOffset", "BooterFuseFirst", "BooterFuseSecond", "BooterFuseReads",
               "BooterSignatureIndex")
    for key in numeric:
        value = node.get(key)
        wide = key.startswith("BAR")
        limit = 1 << (64 if wide else 32)
        if type(value) is not int or not (0 if wide else -(1 << 31)) <= value < limit:
            raise ValueError("Invalid integer: " + key)
        # IORegistry serializes 32-bit OSNumber values with their signed form.
        node[key] = value if wide else value & 0xffffffff
    for key in ("PMCSR", "LinkStatus", "CommandBefore", "CommandDuring", "CommandAfter"):
        if node[key] > 0xffff:
            raise ValueError("Value exceeds PCI 16-bit register: " + key)
    for key in ("Boot0Reads", "BooterFuseReads"):
        if node[key] > 2:
            raise ValueError("Register read count exceeds bound")
    for key in ("Boot0Status", "BooterFuseStatus"):
        if not isinstance(node.get(key), str):
            raise ValueError("Invalid status: " + key)
    first, second, reads = (node["BooterFuse" + key] for key in ("First", "Second", "Reads"))
    stable = reads == 2 and first == second and first != 0xffffffff and first >> 16 not in (0xbad0, 0xbadf)
    index = (1 - first) if stable and first in (0, 1) else None
    link = node["LinkStatus"]
    checks = {
        "target": node["TargetIdentity"] == 0x252010de and node["TargetSubsystem"] == 0x104c1043 and node["TargetBDF"],
        "power_and_link": node["PMCSR"] != 0xffff and node["PMCSR"] & 3 == 0 and
                          bool(link & 15) and bool((link >> 4) & 63) and not link & 0x800,
        "bars": node["BARTypesValid"] and 0 < node["BAR0"] <= 0xff000000 and
                node["BAR0"] % 4096 == 0 and node["BAR0"] == node["BAR0Descriptor"] and
                node["BAR0Length"] == 16 << 20 and 0 < node["BAR1"] < 1 << 40 and
                node["BAR1"] % (64 << 20) == 0 and node["BAR1"] == node["BAR1Descriptor"] and
                node["BAR1Length"] == 64 << 20,
        "pci_restored": node["CommandBefore"] == node["CommandAfter"] == 0 and
                        node["CommandDuring"] == 2 and node["MemoryEnableAttempted"] and node["RestoreVerified"],
        "boot0": node["Boot0Reads"] == 2 and node["Boot0First"] == node["Boot0Second"] == 0xb76000a1,
        "fuse_offset": node["BooterFuseOffset"] == 0x824148,
        "fuse_stable": stable,
        "signature_supported": index is not None,
        "signature_index": node["BooterSignatureIndex"] == (index if index is not None else 0xffffffff),
        "fuse_status": node.get("BooterFuseStatus") == "gsp-booter-signature-candidate-selected",
        "boot0_status": node.get("Boot0Status") == "GA106-BOOT0-read",
    }
    passed = all(checks.values())
    if node["ProbePassed"] and not passed:
        raise ValueError("Unsupported native success: " + ", ".join(k for k, v in checks.items() if not v))
    if passed and not node["ProbePassed"]:
        raise ValueError("Native failure contradicts complete successful evidence")
    return {"probe_version": "0.10.0", "passed": passed, "checks": checks,
            "fuse": {"offset": 0x824148, "first": first, "second": second, "reads": reads,
                     "signature_index": index, "status": node.get("BooterFuseStatus")},
            "pci": {"before": node["CommandBefore"], "during": node["CommandDuring"],
                    "after": node["CommandAfter"], "restored": checks["pci_restored"]},
            "signature_gpu_acceptance_tested": False, "firmware_executed": False,
            "dma_transfer_executed": False, "reset_executed": False, "gsp_running": False}


def decode_capture(path):
    data = Path(path).read_bytes()
    nodes = plistlib.loads(data)
    if not isinstance(nodes, list) or len(nodes) != 1 or not isinstance(nodes[0], dict):
        raise ValueError("Expected exactly one captured probe")
    result = decode_booter_probe(nodes[0])
    result["snapshot_sha256"] = hashlib.sha256(data).hexdigest()
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("snapshot", type=Path)
    args = parser.parse_args()
    try:
        result = decode_capture(args.snapshot)
    except (OSError, ValueError, plistlib.InvalidFileException) as error:
        parser.exit(2, "Booter preflight decode failed: " + str(error) + "\n")
    print(json.dumps(result, indent=2))
    if not result["passed"]:
        raise SystemExit(3)
