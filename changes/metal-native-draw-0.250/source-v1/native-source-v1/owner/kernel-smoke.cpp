#include "LibraryUpload.hpp"
#include "LibraryUploadABI.hpp"
extern "C" unsigned rtx_upload_begin(RtxLibraryUpload036::State *s,const RtxLibraryUpload036::Scope *scope,const unsigned char *data,unsigned n){return unsigned(s->begin(*scope,data,n));}
extern "C" unsigned rtx_upload_append(RtxLibraryUpload036::State *s,const RtxLibraryUpload036::Scope *scope,unsigned long long offset,const unsigned char *data,unsigned n){return unsigned(s->append(*scope,offset,data,n));}
extern "C" unsigned rtx_upload_seal(RtxLibraryUpload036::State *s,const RtxLibraryUpload036::Scope *scope){return unsigned(s->seal(*scope));}
extern "C" unsigned rtx_upload_consume(RtxLibraryUpload036::State *s,const RtxLibraryUpload036::Scope *scope){return unsigned(s->consume(*scope));}
extern "C" bool rtx_upload_info(RtxLibraryUpload036::State *s,unsigned char *out,unsigned n){return s->info(out,n);}
extern "C" unsigned rtx_upload_dispatch(RtxLibraryUpload036::State *s,const RtxLibraryUpload036::Scope *scope,unsigned method,const RtxLibraryUploadABI036::Call *call){return unsigned(RtxLibraryUploadABI036::dispatch(*s,*scope,method,*call));}
