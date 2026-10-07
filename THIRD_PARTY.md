# Third-party inputs and notices

The root MIT license covers original project code. It does not relicense
third-party material or grant rights to NVIDIA/Apple firmware and SDKs.

- **Mesa / NAK:** the exported standalone compiler patch is based on Mesa
  26.2.0. Preserve upstream file notices. The upstream licensing overview is
  retained in `LICENSES/Mesa-license.rst`. Obtain the matching source from
  <https://archive.mesa3d.org/>; source provenance is retained with the patch.
- **NVIDIA firmware and VBIOS:** the experiments used NVIDIA 570.144 assets and
  a board-specific ROM. These blobs and generated FWSEC/SEC2 payload arrays are
  omitted, including copies embedded in source snapshots. Acquire your own
  permitted inputs and follow their original terms. See the preparation and
  signature-selection scripts for the exact expected versions and hashes.
- **NVIDIA public reference material:** register/class definitions and protocol
  research were informed by NVIDIA's public documentation and open kernel
  modules: <https://github.com/NVIDIA/open-gpu-doc> and
  <https://github.com/NVIDIA/open-gpu-kernel-modules>. This export does not bundle
  the large reference trees or NVIDIA driver packages.
- **tinygrad:** earlier bootstrap/memory work used
  <https://github.com/tinygrad/tinygrad> at revision
  `33cd373ad35371ccb483c9645d0c0637a04debc2`. Any copied upstream notice remains
  applicable; the vendor checkout is not included. Its MIT license is retained
  in `LICENSES/tinygrad-MIT.txt`.
- **Apple:** install Xcode/SDK/Metal tooling separately from Apple. Apple SDK
  headers, frameworks and compiler binaries are not shipped here. Metal and
  macOS names identify compatibility targets only.

The graphics catalog header contains program bytes compiled from the project's
own shader; it is not a proprietary NVIDIA firmware release. Raw hardware
captures, compiled binaries and host execution metadata are not included.
