"""Wait for IOService::start to finish before decoding or unloading the probe."""
import argparse
from pathlib import Path
import plistlib
import subprocess
import time


def wait_complete(read, expected_version, timeout=45, now=time.monotonic, sleep=time.sleep):
    deadline = now() + timeout
    while True:
        raw = read()
        nodes = [n for n in plistlib.loads(raw) if n.get("IOObjectClass") == "RTXProbe" or n.get("ProbeVersion")]
        if len(nodes) > 1:
            raise ValueError("Multiple RTXProbe services")
        if nodes:
            node = nodes[0]
            if node.get("ProbeVersion") not in (None, expected_version):
                raise ValueError("Loaded probe version differs from requested binary")
            if node.get("ProbeVersion") == expected_version and node.get("ProbeComplete") is True:
                return raw
        if now() >= deadline:
            raise TimeoutError("RTXProbe has not completed within 45 seconds; inspect its loaded state")
        sleep(0.25)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--expected-version", required=True)
    args = parser.parse_args()
    try:
        raw = wait_complete(lambda: subprocess.check_output(["ioreg", "-a", "-l", "-w0", "-r", "-c", "RTXProbe"], timeout=5),
                            args.expected_version)
        args.output.write_bytes(raw)
    except (ValueError, OSError, TimeoutError, subprocess.SubprocessError) as error:
        parser.exit(3, f"Capture incomplete: {error}\n")
