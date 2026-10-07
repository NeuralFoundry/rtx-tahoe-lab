#pragma once
#include "OwnedPageTree167.hpp"

namespace RTXTreeBacking168 {
enum class Phase : uint32_t { Idle, Preparing, Failed, HostReady, Exposed, Retained, Released };
enum class Step : uint32_t { None, Shape, Image, Scratch, Backing, Encode, Write, Publish, Physical, Readback, Expose, Cleanup };
struct Info {
    Phase phase=Phase::Idle; Step step=Step::None;
    uint32_t error=0,cleanupError=0,mappings=0,tables=0,written=0;
    uint64_t bytes=0,root=0;
    bool image=false,scratch=false,backingStarted=false,cleanupAttempted=false,cleanupSucceeded=false;
};
// Builds an unpublished root. This owns TABLE backing, not mapped DATA. The
// native coordinator must hold all data and the PCI provider throughout prepare,
// and pin them before expose() and before any root RPC. No GPU call occurs here.
class Lifetime {
    Info m;
    bool fail(Step step,uint32_t e=1) {m.phase=Phase::Failed;m.step=step;m.error=e?e:1;m.root=0;return false;}
    Lifetime(const Lifetime&)=delete;Lifetime&operator=(const Lifetime&)=delete;
public:
    Lifetime()=default;
    const Info&info()const{return m;}
    template<class B>bool prepare(B &b,const RTXPageTree167::Mapping *rows,uint32_t count,bool disableAts) {
        if(m.phase!=Phase::Idle||m.cleanupAttempted)return false;
        m.phase=Phase::Preparing;m.mappings=count;
        auto shape=RTXPageTree167::measure(rows,count);
        if(shape.error!=RTXPageTree167::Error::None)return fail(Step::Shape,uint32_t(shape.error));
        m.tables=shape.tables;m.bytes=uint64_t(m.tables)*RTXPageTree167::Page;
        if(!b.allocateImage(size_t(m.bytes)))return fail(Step::Image);m.image=true;
        if(!b.allocateScratch(m.tables))return fail(Step::Scratch);m.scratch=true;
        // Set before prepare: a failed IOKit preparation may still hold resources.
        m.backingStarted=true;if(!b.prepareBacking(m.bytes))return fail(Step::Backing);
        auto tree=RTXPageTree167::build(rows,count,b.pages(),m.tables,b.scratch(),b.image(),size_t(m.bytes),disableAts);
        if(tree.error!=RTXPageTree167::Error::None)return fail(Step::Encode,uint32_t(tree.error));
        for(uint32_t i=0;i<m.tables;++i) {
            if(!b.writePage(uint64_t(i)*RTXPageTree167::Page,b.image()+size_t(i)*RTXPageTree167::Page))return fail(Step::Write);
            ++m.written;
        }
        if(!b.publishBacking())return fail(Step::Publish);
        if(!b.physicalPagesStable(m.tables))return fail(Step::Physical);
        if(!b.readbackEquals(b.image(),size_t(m.bytes)))return fail(Step::Readback);
        // Staging is private CPU memory and never referenced by the GPU.
        b.freeScratch(m.tables);m.scratch=false;b.freeImage(size_t(m.bytes));m.image=false;
        m.root=tree.root;m.phase=Phase::HostReady;m.step=Step::None;return true;
    }
    template<class B>bool expose(B &b) {
        if(m.phase!=Phase::HostReady||m.cleanupAttempted)return false;
        if(!b.exposeBacking()){fail(Step::Expose);m.phase=Phase::Retained;return false;}
        m.phase=Phase::Exposed;return true;
    }
    template<class B>bool cleanup(B &b) {
        if(m.phase==Phase::Exposed||m.phase==Phase::Retained)return false;
        if(m.cleanupAttempted)return m.cleanupSucceeded;
        m.cleanupAttempted=true;m.root=0;
        if(m.backingStarted) {
            if(!b.cleanupBacking()){m.phase=Phase::Failed;m.step=Step::Cleanup;m.cleanupError=1;return false;}
            m.backingStarted=false;
        }
        if(m.scratch){b.freeScratch(m.tables);m.scratch=false;}
        if(m.image){b.freeImage(size_t(m.bytes));m.image=false;}
        m.root=0;m.phase=Phase::Released;m.cleanupSucceeded=true;return true;
    }
};
}
