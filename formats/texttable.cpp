#include "texttable.h"

#include <cstring>

#include "chunk.h"

using namespace rc;

namespace {

uint32_t dword(const uint8_t* p) {
  uint32_t v;
  std::memcpy(&v,p,4);
  return v;
  }

}

TextTable TextTable::parse(const uint8_t* data, size_t size) {
  TextTable ret;
  uint32_t  key = 0, step = 0;
  bool      longIds = false;
  auto decode = [&](const uint8_t* p, size_t n) {
    std::string s(n,'\0');
    for(size_t i=0; i<n; ++i) {
      s[i] = char(p[i]^uint8_t(key));
      key += step;
      }
    while(!s.empty() && s.back()=='\0')
      s.pop_back();
    return s;
    };
  size_t at = 0;
  while(at+8<=size) {
    const uint32_t tag = dword(data+at);
    const uint32_t len = dword(data+at+4);
    if(len<8 || len>size-at)
      throw FormatError("tbl: a chunk runs past the end");
    const uint8_t* body = data+at+8;
    const size_t   n    = len-8;
    if(std::memcmp(&tag,"HSRC",4)==0 && n>=4)
      longIds = (dword(body)&0xffff0000u)!=0x10000u;
    else if(std::memcmp(&tag,"CSRC",4)==0 && n>=8) {
      key  = dword(body);
      step = dword(body+4);
      }
    else if(std::memcmp(&tag,"LSRC",4)==0)
      ret.lang = decode(body,n);
    else if(std::memcmp(&tag,"DSRC",4)==0 && n>=4) {
      const uint32_t count = dword(body);
      const size_t   head  = longIds ? 10 : 6;
      size_t         p     = 4;
      for(uint32_t i=0; i<count; ++i) {
        if(p+head>n)
          throw FormatError("tbl: a text runs past the end");
        const uint32_t id = dword(body+p);
        uint16_t       l;
        std::memcpy(&l,body+p+head-2,2);
        p += head;
        if(p+l>n)
          throw FormatError("tbl: a text runs past the end");
        ret.texts[id] = decode(body+p,l);
        p += l;
        }
      }
    at += len;
    }
  return ret;
  }

std::string_view TextTable::get(uint32_t id) const {
  auto it = texts.find(id);
  return it==texts.end() ? std::string_view() : std::string_view(it->second);
  }
