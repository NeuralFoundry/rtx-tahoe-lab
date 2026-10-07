#pragma once
#include "MacFalcon.hpp"
#include "FWSECStageProtocol.hpp"
#include "generated/fwsec-payload.hpp"

class MacFWSECStage : public MacFalcon {
  const unsigned char *rom;
  const unsigned fuseValue, signatureIndex;
public:
  MacFWSECStage(IOPCIDevice *pci, IOService *owner, const void *capturedRom, unsigned fuse, unsigned signature)
    : MacFalcon(pci, owner, true), rom(static_cast<const unsigned char *>(capturedRom)),
      fuseValue(fuse), signatureIndex(signature) {}
  bool imageMatchesBoard() {
    using namespace FWSECPayload;
    static_assert(ImageSize == FWSECStage::ImageBytes && ImemSize == FWSECStage::ImemBytes &&
      DmemSize == FWSECStage::DmemBytes && ImemBase == 0 && ImemVirtualBase == 0 && DmemBase == 0,
      "Stage protocol only supports the captured GA106 FWSEC geometry");
    static_assert(DescriptorOffset + DescriptorSize <= Preparation::RomSize &&
      ImageRomOffset + ImageSize <= Preparation::RomSize, "Embedded firmware exceeds captured ROM");
    if (!rom || fuseValue != FuseValue || signatureIndex != SignatureIndex) return false;
    for (unsigned i = 0; i < DescriptorSize; ++i) if (rom[DescriptorOffset + i] != Descriptor[i]) return false;
    for (unsigned i = 0; i < ImageSize; ++i) if (rom[ImageRomOffset + i] != OriginalImage[i]) return false;
    return true;
  }
  unsigned imageWord(unsigned offset) {
    if ((offset & 3) || offset > FWSECPayload::ImageSize - 4) return 0xffffffffU;
    const auto *p = FWSECPayload::Image + offset;
    return unsigned(p[0]) | (unsigned(p[1]) << 8) | (unsigned(p[2]) << 16) | (unsigned(p[3]) << 24);
  }
  bool publishFWSEC(unsigned offset) {
    return publishFWSECImage(FWSECPayload::Image, FWSECPayload::ImageSize, offset);
  }
  bool verifyFWSEC() { return verifyFWSECImage(); }
  void forgetCapturedRom() { rom = nullptr; }
};
