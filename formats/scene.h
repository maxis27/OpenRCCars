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
// GLTX children: 0x03 name (= Textures.N/<name>.csi), 0x08 header (u32 5, then index, 0, flags, flags, material),
// 0x32 embedded .csi (lightmaps)
// GLTX indices are local to the enclosing MODL: Car.sb holds several cars, each numbering its textures from 0.
// `material` is a bit set describing what the texture is made of, at most one bit per texture on the tracks:
//   4 wet sand, 5 sand (sand_halfdry), 6 trampled sand (Sand_trample), 7 wood, 8 grass, 9 gravel (Sand_gravel,
//   path_*), 10 asphalt, 11 stone / concrete / rock, 12 metal; 0 for everything else, including water ("sea").
struct SbTexture {
  int32_t              index = -1;
  int32_t              model = -1; // SbScene::models
  uint32_t             material = 0;
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
  int32_t             model  = -1; // SbScene::models
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

// INST: placed object or actor (children are ignored for now). 'Player' in FOLD 'Players' is the start position
// of the player's car, with the car's forward (+Z) rotated to face along the track.
struct SbInstance {
  std::string name;
  Mat4        world;
  };

// EVOL: named volume (trigger), e.g. "water_box", "car_shortcut", "car_reset_volume_*".
//   0x03 name, 0xd0 u32 shape, 0xd4 shape data (floats, world space). All volumes in the tracks are shape 2, a
//   vertical prism: y min, y max, then the corners of its outline (x, z).
struct SbVolume {
  std::string        name;
  uint32_t           shape = 0;
  std::vector<float> data;

  // inside the prism (shape 2); false for other shapes
  bool contains(const Vec3& p) const;
  };

// COLL - top level node of track scenes (MOD_COLLISION). The game collides with the render geometry itself:
// COLL is a uniform grid over the track that lists, for every cell, the faces of SbScene::meshes that touch it.
//   0x98 u32[3] cell counts (x, y, z), 0x99 vec3 grid min, 0x9a vec3 grid max, 0x9e u32 number of entries,
//   0x9d u16 per cell: number of entries in the cell, cell index = z + nz*(y + ny*x), entries stored in cell order,
//   0x9c u16 per entry: mesh index (SbScene::meshes order), 0x9f counted u32 per entry: face count of that mesh,
//   0x00 (type 0x15) u32 size + per entry a bitmask of the mesh's faces, LSB first, ceil(faces/8) bytes.
// Verified on all 10 tracks: every referenced face's bounding box overlaps its cell.
struct SbCollisionGrid {
  struct Entry {
    uint32_t mesh  = 0;
    uint32_t faces = 0;
    size_t   mask  = 0; // offset of the face bitmask in `masks`
    };
  uint32_t              dims[3] = {};
  Vec3                  min, max;
  std::vector<uint16_t> cellEntries; // entry count per cell
  std::vector<Entry>    entries;
  std::vector<uint8_t>  masks;

  bool     empty() const { return entries.empty(); }
  size_t   cells() const { return size_t(dims[0])*dims[1]*dims[2]; }
  size_t   cellIndex(uint32_t x, uint32_t y, uint32_t z) const { return z + size_t(dims[2])*(y + size_t(dims[1])*x); }
  bool     hasFace(const Entry& e, uint32_t face) const { return face<e.faces && (masks[e.mask+face/8]>>(face%8))&1; }
  };

struct SbScene {
  std::vector<std::string> models; // MODL names, e.g. a track ("beach_1") or cars in Car.sb ("Car1", "Car2LOD1" ...)
  std::vector<SbTexture>   textures;
  std::vector<SbMesh>      meshes;
  std::vector<SbMarker>    markers;
  std::vector<SbInstance>  instances;
  std::vector<SbVolume>    volumes;
  SbCollisionGrid          collision; // empty for scenes without COLL (cars, props)

  const SbTexture*  texture(int32_t index, int32_t model) const;
  const SbMarker*   marker(std::string_view name) const;
  const SbInstance* instance(std::string_view name) const;
  const SbVolume*   volume(std::string_view name) const;
  int32_t           findModel(std::string_view name) const; // -1 if not found

  static SbScene load(const std::vector<uint8_t>& file); // throws FormatError
  };

Mat4 transformFromSb(const float v[9]); // pos, scale, rotation(deg)

}
