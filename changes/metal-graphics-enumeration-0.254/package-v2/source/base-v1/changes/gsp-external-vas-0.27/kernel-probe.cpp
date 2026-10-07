#include "ExternalVAS.hpp"
extern "C" bool external_vas_request(unsigned step,unsigned char *out,size_t capacity){return ExternalVAS::request(step,out,capacity);}
extern "C" bool external_vas_consume(ExternalVAS::Journal *journal,const unsigned char *raw,size_t bytes){
 return journal&&ExternalVAS::consume(*journal,raw,bytes);
}
