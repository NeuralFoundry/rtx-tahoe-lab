#pragma once
#include "MacGSPDma.hpp"
#include "MacGSPIdentity.hpp"
#include "SEC2StageProtocol.hpp"
#include "generated/sec2-payload.hpp"

// Borrows one already-owned PCI provider and the current prepared mappings.
// No allocation, free, close, CPU-start, boot-vector, BROM or mailbox write API.
class MacSEC2Stage {
  IOPCIDevice *pci;
  IOService *owner;
  MacGSPDma &dma;
  static constexpr unsigned MapCount=8;
  const unsigned offsets[MapCount]={0x840000,0x841000,0,0x110000,0x111000,0x118000,0x1fa000,0x824000};
  IOMemoryMap *maps[MapCount]={};
  bool hasRiscv=false;
  bool owned() const { return pci && pci->isOpen(owner); }
  unsigned raw(unsigned offset) const {
    if (!owned() || !(pci->configRead16(4)&2) || (offset&3)) return 0xffffffffU;
    for (unsigned i=0;i<MapCount;++i) if (maps[i] && (offset&~4095U)==offsets[i]) {
      __sync_synchronize();
      const unsigned value=*reinterpret_cast<volatile const UInt32 *>(maps[i]->getVirtualAddress()+(offset&4095));
      __sync_synchronize(); return value;
    }
    return 0xffffffffU;
  }
public:
  unsigned pciRevision=0xffffffffU;
  unsigned bar3Types=0xffffffffU;
  unsigned long long bar3=0,bar3Descriptor=0,bar3Length=0;
  MacSEC2Stage(IOPCIDevice *device,IOService *client,MacGSPDma &memory)
    :pci(device),owner(client),dma(memory) {}
  bool map() {
    if (!owned() || command()!=0 || !dma.ready()) return false;
    IODeviceMemory *memory=pci->getDeviceMemoryWithRegister(0x10);
    if (!memory || memory->getLength()!=(16ULL<<20)) return false;
    const unsigned long long bar0=pci->configRead32(0x10)&0xfffffff0U;
    if (!bar0 || bar0>0xff000000ULL || memory->getPhysicalAddress()!=bar0) return false;
    for (unsigned i=0;i<MapCount;++i) {
      maps[i]=memory->createMappingInTask(kernel_task,0,
        kIOMapAnywhere|kIOMapInhibitCache|kIOMapUnique|(i>=2?kIOMapReadOnly:0),offsets[i],4096);
      if (!maps[i] || !maps[i]->getVirtualAddress() || maps[i]->getLength()!=4096 ||
          maps[i]->getPhysicalAddress()!=bar0+offsets[i]) { unmap(); return false; }
    }
    pciRevision=pci->configRead8(8);
    const unsigned low=pci->configRead32(0x1c);
    bar3Types=low&15;
    if ((low&7)==4) bar3=(static_cast<unsigned long long>(pci->configRead32(0x20))<<32)|(low&0xfffffff0U);
    if (IODeviceMemory *mem=pci->getDeviceMemoryWithRegister(0x1c)) {
      bar3Descriptor=mem->getPhysicalAddress(); bar3Length=mem->getLength();
    }
    return true;
  }
  void unmap() {
    for (unsigned i=0;i<MapCount;++i) if (maps[i]) { maps[i]->release(); maps[i]=nullptr; }
  }
  unsigned command() const { return pci->configRead16(4); }
  void setMemory(bool enabled) { if (owned()) pci->setMemoryEnable(enabled); }
  void setMaster(bool enabled) { if (owned()) pci->setBusMasterEnable(enabled); }
  void delayUs(unsigned us) { if (us<=100) IODelay(us); }
  unsigned deviceStatus() {
    UInt8 offset=0;
    if (!pci->findPCICapability(0x10,&offset) || offset<0x40 || offset>0xe8) return 0xffff;
    return pci->configRead16(offset+0x0a);
  }
  bool imageMatchesBoard() {
    static_assert(SEC2Payload::ImageBytes==SEC2Stage::ImageBytes &&
      SEC2Payload::AllocationBytes==SEC2Stage::AllocationBytes &&
      SEC2Payload::CodeOffset==SEC2Stage::ImemOffset && SEC2Payload::CodeBytes==SEC2Stage::ImemBytes &&
      SEC2Payload::DataOffset==SEC2Stage::DmemOffset && SEC2Payload::DataBytes==SEC2Stage::DmemBytes,
      "Unsupported embedded SEC2 geometry");
    return owned() && command()==0 && pci->configRead32(0)==0x252010de &&
      pci->configRead32(0x2c)==0x104c1043 && pci->getBusNumber()==1 &&
      pci->getDeviceNumber()==0 && pci->getFunctionNumber()==0 && verifyImage();
  }
  bool verifyImage() {
    if (!owned() || !dma.ready()) return false;
    unsigned char block[256];
    for (unsigned offset=0;offset<SEC2Stage::AllocationBytes;offset+=sizeof(block)) {
      if (!dma.copyOut(GSPDmaProtocol::BooterLoad,offset,block,sizeof(block))) return false;
      for (unsigned i=0;i<sizeof(block);++i) {
        const unsigned at=offset+i;
        const unsigned char expected=at<SEC2Payload::ImageBytes?SEC2Payload::Image[at]:0;
        if (block[i]!=expected) return false;
      }
    }
    return true;
  }
  bool synchronizeImage() { return owned() && verifyImage() && dma.synchronizeAll() && verifyImage(); }
  unsigned imageWord(unsigned offset) {
    if ((offset&3) || offset>SEC2Payload::ImageBytes-4) return 0xffffffffU;
    const auto *p=SEC2Payload::Image+offset;
    return unsigned(p[0])|(unsigned(p[1])<<8)|(unsigned(p[2])<<16)|(unsigned(p[3])<<24);
  }
  bool imageAddress(unsigned offset,SEC2Stage::U64 &address) {
    if (!owned() || !dma.ready() || !dma.pages() || offset>=SEC2Stage::ImageBytes) return false;
    unsigned base=0;
    for (unsigned i=0;i<GSPDmaProtocol::BooterLoad;++i) base+=GSPDmaProtocol::Pages[i];
    const auto page=dma.pages()[base+offset/4096];
    if (!page || page%4096 || page>(1ULL<<40)-4096) return false;
    address=page+(offset&4095);
    return address<(1ULL<<40);
  }
  bool environmentReady() {
    if (!owned() || command()!=2 || !verifyImage()) return false;
    MacGSPIdentity identity(pci,owner);
    auto facts=identity.facts();
    if (facts.command!=2) return false;
    // Boot0::preflight additionally validates power/link/BAR facts; its idle
    // command guard was checked before this transaction enabled decode.
    facts.command=0;
    if (Boot0::preflight(facts)) return false;
    for (unsigned sample=0;sample<2;++sample) {
      if (raw(0)!=0xb76000a1U || raw(0x824148)!=SEC2Payload::Fuse ||
          raw(0x1183a4)!=6144 || raw(0x1fa828)!=0) return false;
      const unsigned cpu=raw(0x110100),riscv=raw(0x111388),engine=raw(0x1103c0),bcr=raw(0x111668);
      if (!GSPBooterPreflight::readable(cpu) || !GSPBooterPreflight::readable(riscv) ||
          !GSPBooterPreflight::readable(engine) || !GSPBooterPreflight::readable(bcr) ||
          (cpu&2) || (riscv&0x80) || (engine&1) || (bcr&0x10)) return false;
    }
    return command()==2;
  }
  unsigned read(SEC2Stage::Reg reg) {
    if (unsigned(reg)>=SEC2Stage::Count) return 0xffffffffU;
    if ((reg==SEC2Stage::BCR || reg==SEC2Stage::RISCVCPU) && !hasRiscv) return 0xffffffffU;
    const unsigned value=raw(SEC2Stage::Base+SEC2Stage::Offsets[reg]);
    if (reg==SEC2Stage::HWCFG2 && GSPBooterPreflight::readable(value)) hasRiscv=(value&0x400)!=0;
    return value;
  }
  void write(SEC2Stage::Reg reg,unsigned value) {
    using namespace SEC2Stage;
    if (!owned() || !(command()&2) || unsigned(reg)>=Count) return;
    switch (reg) {
      case Engine: case TRANSCFG: case FBIFCTL: case DMACTL:
      case DMABASE: case DMAOFFSET: case FBOFFSET: case DMEMD: break;
      case DMABASE1: if (value!=0) return; break; // 40-bit source limit.
      case DMEMC: if ((value&3) || value>=DmemBytes) return; break;
      case DMACMD: if ((value!=0x614 && value!=0x600) || command()!=6) return; break;
      case BCR: if (!hasRiscv || value!=0) return; break;
      default:return; // CPU start, BROM, mailbox, boot-vector are absent.
    }
    const unsigned offset=Offsets[reg],page=offset/4096;
    if (page>1 || !maps[page]) return;
    __sync_synchronize();
    *reinterpret_cast<volatile UInt32 *>(maps[page]->getVirtualAddress()+(offset&4095))=value;
    __sync_synchronize();
  }
};
