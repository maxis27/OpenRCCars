#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "math3d.h"

namespace rc {

// .sb - "CREAT Studio Scene Project 6.0".
// Module table followed by a node tree: DESC -> FOLD/MODL -> MESH (nested), GLTX, MARK, COLL, HHID, INST, SPLN, ANIM ...
//
// MESH children:
//   0x03 name, 0x0b transform (u32 9, pos xyz, scale xyz, rotation xyz in degrees),
//   0x11 positions (u32 n + vec3[n]), 0x13 texcoords (u32 n + vec2[n]), 0x14 bbox,
//   0x17 faces: list of 0x18 { prefix n; 0x19 position indices, 0x1a normals, 0x25 material, 0x27 face normal,
//                              0x3a layers: 0x3b { 0x3c texture (GLTX index), 0x3d tag (BAS1/LM_1/ALP1/BAS2), 0x3f uv indices } }
// GLTX children: 0x03 name (= Textures.N/<name>.csi), 0x08 header (u32 5, u32 index ...), 0x32 embedded .csi (lightmaps)
struct SbTexture {
  int32_t              index = -1;
  std::string          name;
  std::vector<uint8_t> embedded; // raw .csi, if stored inside the scene
  };

enum class LayerTag : uint8_t {
  Unknown,
  Base,      // BAS1
  Base2,     // BAS2 - second terrain texture
  Alpha,     // ALP1 - blend mask between Base and Base2
  Lightmap,  // LM_1
  };

struct SbFace {
  struct Layer {
    LayerTag              tag     = LayerTag::Unknown;
    int32_t               texture = -1;
    std::vector<uint32_t> uv;
    };
  std::vector<uint32_t> pos;
  std::vector<Vec3>     normals;
  std::vector<Layer>    layers;

  const Layer* layer(LayerTag t) const {
    for(auto& l:layers)
      if(l.tag==t)
        return &l;
    return nullptr;
    }
  };

struct SbMesh {
  std::string         name;
  int32_t             parent = -1;
  Mat4                local;
  Mat4                world;
  std::vector<Vec3>   positions;
  std::vector<Vec2>   uvs;
  std::vector<SbFace> faces;
  };

struct SbMarker {
  std::string name;
  Mat4        world;
  Vec3        position() const { return world.translation(); }
  };

struct SbScene {
  std::vector<SbTexture> textures;
  std::vector<SbMesh>    meshes;
  std::vector<SbMarker>  markers;

  const SbTexture* texture(int32_t index) const;
  const SbMarker*  marker(std::string_view name) const;

  static SbScene load(const std::vector<uint8_t>& file); // throws FormatError
  };

Mat4 transformFromSb(const float v[9]); // pos, scale, rotation(deg)

}
