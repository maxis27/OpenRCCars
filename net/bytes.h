#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace rc::net {

// Little-endian packing of the messages. A Reader that runs past the end stays failed and gives zeros, so a short or
// broken packet is noticed once, at ok(), instead of at every field.
class Writer {
  public:
    Writer& u8 (uint8_t v)  { out.push_back(v); return *this; }
    Writer& u16(uint16_t v) { return u8(uint8_t(v)).u8(uint8_t(v>>8)); }
    Writer& u32(uint32_t v) { return u16(uint16_t(v)).u16(uint16_t(v>>16)); }
    Writer& i32(int32_t v)  { return u32(uint32_t(v)); }
    Writer& f32(float v)    { uint32_t u; std::memcpy(&u,&v,4); return u32(u); }
    // [u8 length][bytes], cut at 255
    Writer& str(std::string_view s) {
      const size_t n = std::min<size_t>(s.size(),255);
      u8(uint8_t(n));
      out.insert(out.end(),s.begin(),s.begin()+n);
      return *this;
      }
    Writer& bytes(const std::vector<uint8_t>& b) { out.insert(out.end(),b.begin(),b.end()); return *this; }

    const std::vector<uint8_t>& data() const { return out; }
    std::vector<uint8_t>        take()       { return std::move(out); }

  private:
    std::vector<uint8_t> out;
  };

class Reader {
  public:
    Reader(const uint8_t* data, size_t size):at(data),end(data+size) {}
    explicit Reader(const std::vector<uint8_t>& d):Reader(d.data(),d.size()) {}

    uint8_t  u8()  { if(!need(1)) return 0; return *at++; }
    uint16_t u16() { const uint16_t a = u8(); return uint16_t(a | (u8()<<8)); }
    uint32_t u32() { const uint32_t a = u16(); return a | (uint32_t(u16())<<16); }
    int32_t  i32() { return int32_t(u32()); }
    float    f32() { const uint32_t u = u32(); float v; std::memcpy(&v,&u,4); return v; }
    std::string str() {
      const size_t n = u8();
      if(!need(n))
        return {};
      std::string s(reinterpret_cast<const char*>(at),n);
      at += n;
      return s;
      }
    // what is left
    std::vector<uint8_t> rest() { std::vector<uint8_t> r(at,end); at = end; return r; }

    bool   ok()   const { return good; }
    size_t left() const { return size_t(end-at); }

  private:
    bool need(size_t n) {
      if(good && size_t(end-at)>=n)
        return true;
      good = false;
      at   = end;
      return false;
      }

    const uint8_t* at;
    const uint8_t* end;
    bool           good = true;
  };

}
