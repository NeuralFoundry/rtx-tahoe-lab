# Build and test notes

This source-only export was prepared on 2026-10-07. No fresh macOS build or
hardware execution was possible for publication; do not interpret source
availability as a verified installable release.

## Portable tests

```sh
python tools/test_cpu.py
c++ -std=c++17 -O1 -Wall -Wextra \
  changes/metal-graphics-discovery-0.253/source-v1/test-graphics-broker251.cpp \
  -o /tmp/rtx-broker-test
/tmp/rtx-broker-test
```

The Python subset covers boot layout, memory planning, startup protocol and RPC
encoding. The C++ broker uses a CPU model. Neither opens a GPU or loads a kext.
Other historical tests may require local fixtures that are not published.

## macOS components

The latest parent source is in
`changes/metal-native-draw-0.250/source-v1/native-source-v1/probe/kernel`.
The kernel entry is `driver/GSPLibraryUploadProbe.cpp` (RTXProbe 0.83.1).
The child/application sources are under the 0.253 source directory; its small
`probe/` subtree is an interface dependency, not the recommended parent release.

Install Xcode command-line tools and the appropriate x86-64 macOS SDK with
Kernel.framework headers separately. The parent requires generated
`driver/generated/fwsec-payload.hpp` and `sec2-payload.hpp`, intentionally
omitted because they embed third-party firmware. The root preparation scripts
document the expected 570.144 assets and board-specific signature/fuse profile.
Acquire your own permitted firmware/VBIOS inputs and inspect `--help` before
generating them. Do not substitute another board's binary profile.

For a build-only parent compilation, see `python tools/build_probe.py --help`.
The helper does not load or sign the result and was not executed on macOS for
this publication. It preserves the original kernel compile/link approach.

Historical `build250.py`, `build253.py` and `build-root251.py` document source
lists, link flags and test structure. Their old manifests, binary fixtures and
host receipts are excluded. To reproduce those workflows, create a new local
input manifest and fixtures and adapt host assumptions (including UID, SDK
version and private Metal object layouts). Do not bypass their checks while
claiming the original validation result. Kernel loading and firmware execution
are separate, hardware-specific work.

## Compiler patch

The NAK experiment used Mesa 26.2.0 from <https://archive.mesa3d.org/>. The
recorded archive SHA-256 is
`efd4bb08cdb7c365a812cd4e6c9202ab55b2f22cdcd13c7d6c4f9647b799a4ef`.
Apply the retained standalone compiler patch to that matching source tree.
The large vendor tree, compiler binaries and caches are not included. Its
historical Windows build environment needs reconstruction; no fresh compiler
toolchain rebuild is claimed here.
