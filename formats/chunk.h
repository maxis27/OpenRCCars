#pragma once

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>

namespace rc {

class FormatError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
  };

// Element of CREAT Studio binary containers (.pack, .sb, .crs, .scp):
//   [id:u8][type:u8][end:u32]  where `end` is an absolute file offset.
// Types seen so far:
//   0x30 u32, 0x34/0x74 counted array, 0x38 version, 0x40/0x48 C-string, 0x54 transform,
//   0x70 vec3, 0x02 struct (children, no prefix), 0x06 list (u32 count + children),
//   0x14 raw blob, 0x82/0x92/0x9a node (4 byte prefix + children; 0x92/0x9a prefix is a reversed FourCC).
struct Chunk {
  const uint8_t* file = nullptr;
  size_t         pos  = 0;   // offset of the 6 byte header
  size_t         end  = 0;   // absolute end
  uint8_t        id   = 0;
  uint8_t        type = 0;

  size_t         payloadOffset() const { return pos+6; }
  size_t         size()          const { return end-pos-6; }
  const uint8_t* data()          const { return file+pos+6; }

  bool           isNode()        const { return (type&0x80)!=0; }
  // FourCC of 0x92/0x9a nodes, stored reversed in file ("HSEM" -> "MESH")
  bool           is(const char tag[5]) const {
    if(!isNode() || size()<4)
      return false;
    const uint8_t* d = data();
    return d[3]==uint8_t(tag[0]) && d[2]==uint8_t(tag[1]) && d[1]==uint8_t(tag[2]) && d[0]==uint8_t(tag[3]);
    }
  std::string    fourcc() const {
    if(size()<4)
      return {};
    const uint8_t* d = data();
    return std::string{char(d[3]),char(d[2]),char(d[1]),char(d[0])};
    }

  uint32_t u32(size_t at = 0) const { check(at+4); uint32_t v; std::memcpy(&v,data()+at,4); return v; }
  int32_t  i32(size_t at = 0) const { return int32_t(u32(at)); }
  float    f32(size_t at = 0) const { check(at+4); float v; std::memcpy(&v,data()+at,4); return v; }
  std::string_view str() const {
    auto  d = reinterpret_cast<const char*>(data());
    size_t n = size();
    while(n>0 && d[n-1]=='\0')
      --n;
    return std::string_view(d,n);
    }

  void check(size_t need) const {
    if(need>size())
      throw FormatError("chunk too small");
    }

  class Range children(size_t prefix) const;
  // children with the prefix implied by the type
  class Range children() const;
  };

class Range {
  public:
    Range(const uint8_t* file, size_t begin, size_t end):file(file), begin_(begin), end_(end) {}

    class iterator {
      public:
        iterator(const uint8_t* file, size_t pos, size_t end):file(file), pos(pos), end(end) { load(); }
        const Chunk& operator*()  const { return cur; }
        const Chunk* operator->() const { return &cur; }
        iterator& operator++() { pos = cur.end; load(); return *this; }
        bool operator!=(const iterator& o) const { return pos!=o.pos; }

      private:
        void load() {
          if(pos>=end) {
            pos = end;
            return;
            }
          if(pos+6>end)
            throw FormatError("truncated chunk header");
          cur.file = file;
          cur.pos  = pos;
          cur.id   = file[pos];
          cur.type = file[pos+1];
          uint32_t e;
          std::memcpy(&e,file+pos+2,4);
          if(e<pos+6 || e>end)
            throw FormatError("chunk end out of range");
          cur.end = e;
          }
        const uint8_t* file;
        size_t         pos, end;
        Chunk          cur;
      };

    iterator begin() const { return iterator(file,begin_,end_); }
    iterator end()   const { return iterator(file,end_,end_);   }

  private:
    const uint8_t* file;
    size_t         begin_, end_;
  };

inline Range Chunk::children(size_t prefix) const {
  if(prefix>size())
    throw FormatError("chunk prefix out of range");
  return Range(file,payloadOffset()+prefix,end);
  }

inline Range Chunk::children() const {
  return children(type==0x02 ? 0 : 4);
  }

}
