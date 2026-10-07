# RTX Tahoe Lab

Experimental NVIDIA RTX 3060 driver sources and tools for x86-64 macOS Tahoe.
Published by [NeuralFoundry](https://github.com/NeuralFoundry) under the MIT
license, with existing third-party notices preserved.

**Development is paused. Full Metal acceleration is not complete. This is a
source research release, not an installable or daily-use graphics driver.**
The experiment targeted a GA106 board (PCI `10de:2520`, subsystem `1043:104c`).
Other GPUs, Apple Silicon and other OS/SDK layouts are unverified.

## Progress and limits

The original lab recorded selected GSP/compute work and 16 native triangle
draws whose transaction, readback and cleanup checks passed. A color conversion
discrepancy remains: a constant blue output of 0.5 yielded byte 127 while the
reference comparison expected 128. These narrow results do not prove general
Metal support. Positive standard Metal enumeration for the latest graphics
pair, general shaders, presentation/display acceleration and broad stability
remain incomplete. See [STATUS.md](STATUS.md).

This repository contains **source code and tools only**. Raw GPU/machine dumps,
host execution receipts, private connection helpers and historical deployment
archives are not published. Historical experiment claims are not presented as
independently reproducible hardware evidence within this source-only release.

## Components

| Path | Component |
| --- | --- |
| `changes/metal-native-draw-0.250/source-v1/` | RTXProbe 0.83.1 kernel and native owner250 source |
| `changes/metal-graphics-discovery-0.253/source-v1/` | Child accelerator 0.253.0, application Metal API and tests |
| `changes/metal-graphics-discovery-0.253/root-source-v1/` | Graphics XPC broker |
| `changes/metal-graphics-enumeration-0.254/` | Standard enumeration client, runtime and source dependencies |
| `changes/metal-runtime-compiler-0.56/source-v3/` | Source compiler integration |
| `changes/metal-graphics-compiler-0.233/source-audit-2/standalone-compiler.patch` | Mesa/NAK compiler work |
| `changes/metal-color-quantization-0.255/amd-reference-source-v1/` | Prepared AMD color comparison source; not hardware-validated |
| Root Python files | Protocol decoders, firmware preparation tools and portable CPU tests |

Versioned paths preserve original source include relationships. Some older
build/test scripts depend on private historical fixtures which are omitted;
they are retained as implementation references, not ready-to-run installers.

## CPU validation

Python 3.10+, no external Python packages or GPU required:

```sh
python tools/test_cpu.py
python tools/verify_snapshot.py
```

This subset passed 72 tests during publication. The standalone C++ graphics
broker test passed 307 checks, including 32 negative cases. These are CPU
results, not new hardware execution. See [BUILDING.md](BUILDING.md).

No NVIDIA firmware, VBIOS payload arrays, Apple SDK/toolchain, compiled kexts,
personal machine records or credentials are included. Hardware experiments
can hang/reboot a system; development requires a recoverable test machine and
its owner's authorization. The repository does not load drivers automatically.

See [THIRD_PARTY.md](THIRD_PARTY.md), [LICENSE](LICENSE), and
[CONTRIBUTING.md](CONTRIBUTING.md).

## Türkçe

Projeyi şimdilik askıya aldık. Sürücü kaynaklarını ve araçları herkes
inceleyip geliştirebilsin diye paylaşıyoruz. **Tam Metal hızlandırması veya
günlük kullanıma hazır RTX ekran sürücüsü sunmuyor.** Makine kayıtları,
firmware, SDK ve kişisel erişim bilgileri bu depoda yok. Mevcut dar kapsamlı
deney sonuçlarıyla genel Metal desteği birbirinden ayrı tutulmalıdır.
