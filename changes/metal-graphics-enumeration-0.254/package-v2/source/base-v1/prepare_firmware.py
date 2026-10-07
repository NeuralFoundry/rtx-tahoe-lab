"""Fetch pinned firmware into the lab and inspect bytes; never access a GPU."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import tempfile
import urllib.request

TINYGRAD_COMMIT = "5ae6526d4787fa498f7876982a72b517b93b4888"
FIRMWARE_COMMIT = "0a6871b19abf5d6e024b5d208b101ae53e7fa0de"
BASE = f"https://gitlab.com/kernel-firmware/linux-firmware/-/raw/{FIRMWARE_COMMIT}"
FILES = {
    "gsp-570.144.bin": "a8c3ebeed280323aedb51c061f321e73379cce7a9ae643a33dd03915df027f7f",
    "booter_load-570.144.bin": "4497e3eff7e95c774b8a569d17b27c08c9650158d10b229d2be81cdcad9a085b",
    "bootloader-570.144.bin": "82428f532240727e95bb3083fbaaba9b2cc7b937314323f2d546ce7245f27fad",
}
MAX_FILE = 128 << 20


def checked_slice(data, offset, size):
    if offset < 0 or size < 0 or offset > len(data) or size > len(data) - offset:
        raise ValueError("Firmware range is outside the file")
    return data[offset:offset + size]


def inspect_elf(data):
    h = struct.unpack("<16sHHIQQQIHHHHHH", checked_slice(data, 0, 64))
    if h[0][:7] != b"\x7fELF\x02\x01\x01" or h[3] != 1 or h[8] != 64:
        raise ValueError("Expected ELF64 little-endian firmware")
    shoff, entsize, count, strings_index = h[6], h[11], h[12], h[13]
    if entsize != 64 or not 1 <= count <= 4096 or not 0 < strings_index < count:
        raise ValueError("Unsupported or invalid ELF section table")
    table = checked_slice(data, shoff, count * entsize)
    sections = list(struct.iter_unpack("<IIQQQQIIQQ", table))
    names = sections[strings_index]
    if names[1] != 3:
        raise ValueError("Section names are not a string table")
    strings = checked_slice(data, names[4], names[5])
    out = {}
    for s in sections:
        if s[0] >= len(strings):
            raise ValueError("ELF name outside string table")
        end = strings.find(b"\x00", s[0])
        if end < 0:
            raise ValueError("Unterminated ELF section name")
        name = strings[s[0]:end].decode("ascii")
        payload = b"" if s[1] == 8 else checked_slice(data, s[4], s[5])
        if name in (".fwimage", ".fwsignature_ga10x"):
            if name in out or s[1] != 1 or not payload:
                raise ValueError("Duplicate, empty or invalid required firmware section")
            out[name] = {"offset": s[4], "size": s[5], "sha256": hashlib.sha256(payload).hexdigest()}
    if set(out) != {".fwimage", ".fwsignature_ga10x"}:
        raise ValueError("GA10x image/signature section missing")
    return {"format": "ELF64-LE", "machine": h[2], "section_count": count, "required_sections": out,
            "signature_cryptographically_verified": False}


def inspect_bin(data):
    magic, version, size, header, offset, length = struct.unpack("<6I", checked_slice(data, 0, 24))
    # The pinned booter retains a nominal 0xf000 bin_size in a 0xef78-byte file.
    # Never use that nominal size to allow reads beyond the actual bytes.
    if (magic != 0x10de or version != 1 or not len(data) <= size <= MAX_FILE
            or not 24 <= header < len(data) or not length or offset < 24):
        raise ValueError("Invalid NVIDIA binary envelope")
    payload = checked_slice(data, offset, length)
    return {"format": "NVIDIA-bin-envelope", "magic": hex(magic), "version": version,
            "declared_size": size, "actual_size": len(data), "header_offset": header, "payload_offset": offset,
            "payload_size": length, "payload_sha256": hashlib.sha256(payload).hexdigest(),
            "nested_descriptors_validated": False}


def obtain(directory, name, download):
    path = directory / name
    if path.is_symlink():
        raise ValueError("Refusing firmware symlink")
    url = f"{BASE}/nvidia/ga102/gsp/{name}"
    if path.exists():
        if path.stat().st_size > MAX_FILE:
            raise ValueError("Firmware exceeds size limit")
        data = path.read_bytes()
    elif download:
        request = urllib.request.Request(url, headers={"User-Agent": "RTX-Tahoe-Lab/0.3"})
        with urllib.request.urlopen(request, timeout=45) as response:
            data = response.read(MAX_FILE + 1)
        if len(data) > MAX_FILE:
            raise ValueError("Firmware exceeds size limit")
    else:
        raise FileNotFoundError(f"Missing {path}; use --download to fetch the pinned asset")
    digest = hashlib.sha256(data).hexdigest()
    if digest != FILES[name]:
        raise ValueError(f"SHA-256 mismatch for {name}; refusing asset")
    if not path.exists():
        with tempfile.NamedTemporaryFile(dir=directory, prefix=".firmware-", delete=False) as tmp:
            temporary = Path(tmp.name)
            tmp.write(data)
            tmp.flush()
            os.fsync(tmp.fileno())
        try:
            os.replace(temporary, path)
        finally:
            temporary.unlink(missing_ok=True)
    details = inspect_elf(data) if name.startswith("gsp-") else inspect_bin(data)
    return {"file": name, "url": url, "sha256": digest, "bytes": len(data), "inspection": details}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--download", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    directory = root / "firmware" / "570.144"
    directory.mkdir(parents=True, exist_ok=True)
    report = {"tinygrad_commit": TINYGRAD_COMMIT, "linux_firmware_commit": FIRMWARE_COMMIT,
              "target_chip": "GA106", "firmware_family": "GA10x (ga102 assets)", "assets": [],
              "loaded_into_gpu": False, "gpu_signature_acceptance_tested": False,
              "remaining_prerequisites": ["board-specific VBIOS/FWSEC extraction and validation",
                  "device-bound macOS DMA mapping and teardown", "GPU memory/page-table transport",
                  "bounded firmware bootstrap, RPC and shutdown"]}
    try:
        for name in FILES:
            row = obtain(directory, name, args.download)
            report["assets"].append(row)
            print(f"Verified {name}: {row['bytes']} bytes, SHA-256 {row['sha256']}", flush=True)
    except (OSError, ValueError, struct.error) as error:
        parser.exit(2, f"Firmware preparation failed: {error}\n")
    report["all_pinned_assets_verified"] = True
    results = root / "results"
    results.mkdir(exist_ok=True)
    (results / "firmware-manifest.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("Manifest: results/firmware-manifest.json; no hardware access performed", flush=True)


if __name__ == "__main__":
    main()
