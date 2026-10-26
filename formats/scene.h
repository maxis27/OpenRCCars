#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "math3d.h"

namespace rc {

// .sb - "CREAT Studio Scene Project 6.0".
// Module table followed by a node tree: DESC -> FOLD/MODL -> MESH (nested), GLTX, MARK, COLL, HHID, INST, SCRI, SPLN, ANIM ...
//
// MESH children:
//   0x03 name, 0x08 header (u32 count, node number, 0, a flag word, `renderFlags`, `flags`),
//   0x0b transform (u32 9, pos xyz, scale xyz, rotation xyz in degrees),
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
  // the GLTX chunk's word at 16: material groups, bits 21..27 pick the detail texture (LevelMesh, Detail.ini)
  uint32_t             groups = 0;
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

// Animated models (people.sb, AIChars.sb) are skeletons of MESH nodes ("bones": ROOT, CENTRE, LLEG1 ...),
// some of them carrying geometry. Every node has
//   0x08 header (u32 5, then the node's number in its MODL: `node`), 0x0b its transform in the file's pose,
//   0x2a skin of a skinned mesh: list of 0x2b { 0x2c counted u32 node numbers, 0x2d counted float weights in % }
//        per vertex, in vertex order; the vertices are bound in the file's pose (0x0b of every node),
//   ANIM nodes, one per animation sequence (ASEQ, see SbSequence) by the number in their header: 0x10 rest pose
//        (u32 9, pos xyz, rotation xyz in degrees, scale xyz) and SPLN channels, each 0x57 FourCC "ROTX" ... "TRNX"
//        ... "SCLX" ..., 0x5d list of 0x5b keys: u32 type, time (s), value (degrees for rotations); type 0 adds an
//        in and out tangent (Hermite), type 1 is linear. Keys come every frame (30 per second), starting at the
//        sequence's first frame, so linear interpolation is enough. Channels without keys keep the rest pose.
struct SbChannel {
  enum Target : uint8_t { PosX, PosY, PosZ, RotX, RotY, RotZ, SclX, SclY, SclZ, Count };
  Target             target = PosX;
  std::vector<float> times;
  std::vector<float> values;

  float value(float t) const;
  };

struct SbAnim {
  bool                   valid   = false;                 // the node has an ANIM for this sequence
  float                  rest[9] = {0,0,0, 0,0,0, 1,1,1}; // pos, rotation (deg), scale
  std::vector<SbChannel> channels;
  };

struct SbSkinVertex {
  uint8_t  count = 0;
  uint16_t node[4]   = {};
  float    weight[4] = {};  // 0..1, sum 1
  };

struct SbMesh {
  std::string         name;
  int32_t             parent = -1;
  int32_t             model  = -1; // SbScene::models
  int32_t             node   = -1; // number of the node in its MODL (header), referred to by skins
  uint32_t            flags  = 0;  // 0x08 word 5; 0x20000 = drawn with the environment map (RCCars.exe 0x48e4b0):
                                   // the seas' nodes ("more1") and the cars' ENVIR_CAR_BODY_body
  uint32_t            renderFlags = 0; // 0x08 word 4, how RCCars.exe draws the mesh (0x48e4b0, 0x451310):
                                       // 0x1000 blended by its texture's alpha, 0x2000 the seas,
                                       // 0x40000000 multiplied onto what is under it, twice the source times
                                       // the destination (the tread strips "wt_wetsand" on every track)
  Mat4                local;
  Mat4                world;
  Mat4                bindLocal;           // 0x38: the pose the skins were bound in (pos, rotation, scale)
  bool                hasBind = false;
  float               trs[9] = {0,0,0, 1,1,1, 0,0,0}; // 0x0b: pos, scale, rotation (deg)
  std::vector<Vec3>   positions;
  std::vector<Vec2>   uvs;
  std::vector<SbFace> faces;
  std::vector<SbSkinVertex> skin;  // per position, empty for rigid meshes
  std::vector<SbAnim>       anims; // by sequence number (SbSequence::index); empty if the node isn't animated
  };

// ASEQ: animation sequence of a model, e.g. people.sb "Man": Walk, Pendal (kick), Stand, Brosok (throw), Hurt.
//   0x03 name, 0x08 header (u32 5, then the sequence number used by ANIM), 0x50 frames per second (30),
//   0x51 frame count, 0x52 float (1; 0.638 for Man's Walk: unknown).
struct SbSequence {
  std::string name;
  int32_t     model  = -1;
  int32_t     index  = -1;
  uint32_t    fps    = 30;
  uint32_t    frames = 0;
  float       speed  = 1;

  float       duration() const { return fps>0 ? float(frames)/float(fps) : 0.f; }
  };

// MARK: named point. 0x04 holds properties set in the level editor as text pairs (list of 0x05 { 0x06 key,
// 0x07 value }); checkpoints have `width`, `height` and `expand` (see rc::Checkpoints).
struct SbMarker {
  std::string name;
  Mat4        world;
  std::vector<std::pair<std::string,std::string>> props;

  Vec3        position() const { return world.translation(); }
  // property as a number, `def` if missing or not a number
  float       number(std::string_view key, float def) const;
  };

// FOLD: a folder of nodes, its name (0x03) and properties (0x04), e.g. "Checkpoints" with the default width,
// height and expand of the route corridor
struct SbFolder {
  std::string name;
  std::vector<std::pair<std::string,std::string>> props;

  float       number(std::string_view key, float def) const;
  };

// EVOL: named volume (trigger), e.g. "water_box", "car_shortcut", "car_reset_volume_*".
//   0x03 name, 0xd0 u32 shape, 0xd4 shape data (floats, world space), SCRI child: its script (car_reset_volume_*:
//   Section Keys "Params" FilterTime(int) = <ms> End). All volumes in the tracks are shape 2, a vertical prism:
//   y min, y max, then the corners of its outline (x, z).
struct SbVolume {
  std::string        name;
  uint32_t           shape = 0;
  std::vector<float> data;
  std::string        script;

  // inside the prism (shape 2); false for other shapes
  bool contains(const Vec3& p) const;
  };

// INST: placed object or actor. Characters carry their own markers (seagull "flightDir", crab "seahole1"/
// "landhole1", spider "hole1".. ), volumes ("visibilityVolume", "inhabitVolume", dog "attackVolume", guard
// "guardVolume") and a script (SbScript, e.g. Section Keys "Path" name = "beach_1_man": Profiles/beach_1_man.dat)
// as children; they are stored in `markers`, `volumes` and `script` here, not in the scene's lists.
//   0x03 name, 0x0a kind (3, or 2 for props laid on their side: purpose unknown, the transform has the rotation),
//   0x0b transform, 0x72 model (MODL name in another scene), 0x74 that scene ("RoadCar" = Roadcar.sb, empty:
//   the game knows where the model lives: Car.sb, stone.sb, people.sb, AIChars.sb).
// 'Player' in FOLD 'Players' is the start position of the player's car (model "Car1"), with the car's
// forward (+Z) rotated to face along the track.
struct SbInstance {
  std::string name;
  std::string model;
  std::string source;
  uint32_t    kind = 0;
  Mat4        world;
  std::vector<SbMarker> markers;
  std::vector<SbVolume> volumes;
  std::string script;

  const SbMarker* marker(std::string_view name) const;
  const SbVolume* volume(std::string_view name) const;
  };

// SCRI: text in the "Section Keys" format (see KeySections), e.g. the physics of a prop in stone.sb:
//   Section Keys "Geom"   type(int) = 2 // cylinder   shiftCenterMassOY(float) = 0   End
//   Section Keys "Params" mass(float) = 0.1  coeffHookNorm(float) = 0.3  coeffFrict(float) = 1.0  End
// Scripts of instances (people, the track's DemoEntry) are game logic in another language and kept as text too.
struct SbScript {
  std::string name;
  int32_t     model = -1; // SbScene::models, -1 outside a MODL
  std::string text;
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
  std::vector<SbScript>    scripts;   // SCRI directly under MODL/FOLD/DESC
  std::vector<SbVolume>    volumes;
  std::vector<SbSequence>  sequences;
  std::vector<SbFolder>    folders;
  SbCollisionGrid          collision; // empty for scenes without COLL (cars, props)

  const SbTexture*  texture(int32_t index, int32_t model) const;
  const SbMarker*   marker(std::string_view name) const;
  const SbFolder*   folder(std::string_view name) const;
  const SbInstance* instance(std::string_view name) const;
  const SbScript*   script(int32_t model, std::string_view name = {}) const; // first script of the model if no name
  const SbVolume*   volume(std::string_view name) const;
  int32_t           findModel(std::string_view name) const; // -1 if not found

  static SbScene load(const std::vector<uint8_t>& file); // throws FormatError
  };

Mat4 transformFromSb(const float v[9]); // pos, scale, rotation(deg)

}
