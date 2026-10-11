#include "csi.h"

#include "chunk.h"

#include <algorithm>
#include <cstring>

using namespace rc;

namespace {

uint32_t rd32(const uint8_t* d, size_t at) {
  uint32_t v;
  std::memcpy(&v,d+at,4);
  return v;
  }

uint8_t extract(uint32_t v, uint8_t lo, uint8_t hi) {
  if(hi<lo)
    return 0;
  const uint32_t bits = uint32_t(hi-lo+1);
  const uint32_t max  = (bits>=32) ? 0xFFFFFFFFu : ((1u<<bits)-1u);
  const uint32_t m    = (v>>lo)&max;
  return uint8_t((uint64_t(m)*255u + max/2)/max);
  }

}

Image rc::decodeCsi(const uint8_t* data, size_t size) {
  if(size<0x34 || std::memcmp(data,"MISC",4)!=0)
    throw FormatError("csi: bad magic");

  const uint32_t hdr  = rd32(data,0x08);
  const uint32_t w    = rd32(data,0x10);
  const uint32_t h    = rd32(data,0x14);
  const uint32_t bpp  = rd32(data,0x18);
  const uint32_t ch   = rd32(data,0x20);
  const uint8_t* fmt  = data+0x24;
  const uint8_t* bits = data+0x2c;

  if(w==0 || h==0 || w>8192 || h>8192 || bpp==0 || bpp>4 || ch==0 || ch>4)
    throw FormatError("csi: unsupported header");
  // Some files in RCCars.pack are short: the original packer dropped the last 10 bytes of e.g. sky_up.csi,
  // and of a few 1x1 textures (car_dirted_hw0, sh_char) even part of the header padding.
  // Tolerate up to one row (at least 16 bytes) of missing data, the rest is filled with zeros.
  const size_t need    = size_t(w)*h*bpp;
  const size_t allowed = std::max<size_t>(size_t(w)*bpp,16);
  if(size_t(hdr)+need>size+allowed)
    throw FormatError("csi: truncated pixel data");
  std::vector<uint8_t> padded;
  const uint8_t* px = data+hdr;
  if(size_t(hdr)+need>size) {
    if(hdr<size)
      padded.assign(data+hdr,data+size);
    padded.resize(need,0);
    px = padded.data();
    }

  Image img;
  img.w = w;
  img.h = h;
  img.rgba.resize(size_t(w)*h*4);

  int idx[4] = {-1,-1,-1,-1}; // R,G,B,A -> channel slot ; 'I' fills RGB
  int intensity = -1;
  for(uint32_t i=0; i<ch; ++i) {
    switch(fmt[i]) {
      case 'R': idx[0]   = int(i); break;
      case 'G': idx[1]   = int(i); break;
      case 'B': idx[2]   = int(i); break;
      case 'A': idx[3]   = int(i); break;
      case 'I': intensity= int(i); break;
      }
    }

  uint8_t*       o  = img.rgba.data();
  size_t         partial = 0, transparent = 0;
  for(size_t i=0, n=size_t(w)*h; i<n; ++i) {
    uint32_t v = 0;
    std::memcpy(&v,px+i*bpp,bpp);
    uint8_t c[4] = {0,0,0,255};
    for(int k=0; k<4; ++k)
      if(idx[k]>=0)
        c[k] = extract(v,bits[idx[k]*2],bits[idx[k]*2+1]);
    if(intensity>=0)
      c[0] = c[1] = c[2] = extract(v,bits[intensity*2],bits[intensity*2+1]);
    if(c[3]<128)
      transparent++;
    if(c[3]>16 && c[3]<240)
      partial++;
    std::memcpy(o+i*4,c,4);
    }
  const size_t total = size_t(w)*h;
  if(partial*50>total)             // more than 2% of soft alpha
    img.alpha = AlphaMode::Translucent;
  else if(transparent>0)
    img.alpha = AlphaMode::Cutout;
  return img;
  }
