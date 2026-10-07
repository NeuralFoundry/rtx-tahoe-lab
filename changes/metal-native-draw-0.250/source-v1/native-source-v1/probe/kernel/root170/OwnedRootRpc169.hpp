#pragma once
#include "OwnedTreeBacking168.hpp"
#include "ExternalVAS.hpp"

// Same 570.144 RPC54/48-byte payload as the working external VAS path, using
// coherent sysmem root instead of the old fixed VRAM address. Serialization and
// reply validation ONLY: queue ownership, actual send/ack, and channel ordering
// must be supplied by the native GSP coordinator. Data must also be pinned.
namespace RTXRootRpc169 {
constexpr uint32_t CoherentAllChannels=9;
inline bool retained(const RTXTreeBacking168::Info&s){
    return s.phase==RTXTreeBacking168::Phase::Exposed&&s.root>=4096&&s.root%4096==0&&
        s.root<=RTXBacking166::AddressLimit-4096&&s.tables>=5&&s.tables<=RTXPageTree167::MaxTables&&
        s.bytes==uint64_t(s.tables)*4096&&s.written==s.tables&&s.mappings&&s.mappings<=RTXPageTree167::MaxMappings&&
        s.backingStarted&&!s.image&&!s.scratch&&!s.error&&!s.cleanupError&&!s.cleanupAttempted&&!s.cleanupSucceeded&&
        s.step==RTXTreeBacking168::Step::None;
}
inline bool request(const RTXTreeBacking168::Info&s,uint32_t sequence,uint8_t*out,size_t bytes){
    if(!retained(s)||!sequence||sequence==UINT32_MAX||bytes!=4096||!RTXPageTree167::validRange(out,bytes)||
       RTXPageTree167::overlap(&s,sizeof(s),out,bytes))return false;
    if(!ExternalVAS::request(4,out,bytes))return false;
    ExternalVAS::put32(out+36,sequence);ExternalVAS::put64(out+96,s.root);
    ExternalVAS::put32(out+108,CoherentAllChannels);ExternalVAS::put32(out+32,0);
    ExternalVAS::put32(out+32,ExternalVAS::checksum(out,128));return true;
}
inline bool reply(const RTXTreeBacking168::Info&s,uint32_t sequence,const uint8_t*raw,size_t bytes){
    if(!retained(s)||!sequence||sequence==UINT32_MAX)return false;ExternalVAS::Reply result;
    return ExternalVAS::reply(4,sequence,raw,bytes,result)&&result.accepted;
}
}
