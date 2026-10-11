#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rc {

// .csi texture: 160 byte "MISC" header followed by raw pixels of the top level and its mip chain.
//  0x08 header size   0x10 width   0x14 height   0x18 bytes per pixel   0x1c mip count
//  0x20 channel count 0x24 channel names ("RGB", "ARGB", "AI")   0x2c [lo,hi] bit range per channel
enum class AlphaMode : uint8_t {
  Opaque,      // alpha is 255 everywhere
  Cutout,      // mostly binary alpha - alpha test
  Translucent, // significant amount of partial alpha - blending
  };

struct Image {
  uint32_t             w = 0;
  uint32_t             h = 0;
  AlphaMode            alpha = AlphaMode::Opaque;
  std::vector<uint8_t> rgba;   // top mip level, RGBA8, first row first
  };

Image decodeCsi(const uint8_t* data, size_t size); // throws FormatError

}
