#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "scene.h"
#include "water.h"

namespace rc {

// What the car drives on, from the material bits of a face's base texture (SbTexture::material). The original
// keeps these names for its dust effects (Settings\surf_default|sand|wetsand|dunesand|gravel.crs); of all
// surfaces, only deep sand and water change how a car drives (tires<N>.ini coeffDeepSand, coeffWater).
enum class Surface : uint8_t {
  Default,   // asphalt, concrete, rock, wood, grass, metal, faces without a texture ...
  Sand,      // sand_halfdry
  WetSand,   // sand_wet, rubbish_sand_wet
  DeepSand,  // Sand_trample: "dunesand", loose sand that slows the car down
  Gravel,    // Sand_gravel, path_*, broken_stone
  Water,     // texture "sea": seas, streams, pools and waterfalls; not solid, the car sinks into it (but not into a
             // waterfall, see CollisionMesh::isWater)
  };

Surface     surfaceOf(const SbTexture* texture);
const char* surfaceName(Surface s);

// RCCars.exe's surface numbers for the car's sounds (0x535060): 1 sand, 2 wet sand, 3 trampled sand, 4 grass,
// 5 gravel, 6 the tin / boat textures (material bit 12, played with the car_*_asphalt sounds), 7 wood, 8 stone,
// 9 metal (bit 13, not used on the tracks), 0 none (asphalt bit 10 included). The game adds 10 for water itself.
// A face with several textures takes the lowest number of them (0x534fc0).
namespace SoundSurface { enum : uint8_t { None=0, Sand=1, WetSand=2, Trampled=3, Grass=4, Gravel=5, Tin=6, Wood=7,
                                          Stone=8, Metal=9, Water=10, Count=11 }; }
uint8_t     soundSurfaceOf(uint32_t material);

// Triangle soup of the faces a track's COLL grid refers to, in world space, with the same uniform grid
// for queries. Collision geometry is the render geometry (including faces without a texture, which act as
// invisible walls), so nothing here depends on the renderer.
class CollisionMesh {
  public:
    struct Triangle {
      Vec3     a, b, c;
      Vec3     normal;     // unit, from the winding of the data (see README: faces are wound "backwards")
      uint32_t mesh = 0;   // SbScene::meshes
      uint32_t face = 0;   // SbMesh::faces
      Surface  surface = Surface::Default;
      uint8_t  sound   = SoundSurface::None;   // see soundSurfaceOf
      };

    // which triangles a query sees: the solid track, or only the water surfaces in it
    enum class Layer : uint8_t {
      Solid,
      Water,
      };

    struct Hit {
      float    distance = 0;
      Vec3     point;
      Vec3     normal;     // facing the ray origin
      uint32_t triangle = 0;
      };

    CollisionMesh() = default;
    explicit CollisionMesh(const SbScene& scene);

    bool                         empty()     const { return tris.empty(); }
    const std::vector<Triangle>& triangles() const { return tris; }
    Vec3                         cellSize()  const { return cell; }

    // closest hit along origin + dir*t, 0 <= t <= maxDistance (dir need not be normalized; distance is in
    // units of |dir|). Triangles are two-sided. Only the part of the ray inside the grid is tested, so
    // geometry sticking out of the grid (e.g. sea floor below it at the edge of beach_4) is not found.
    std::optional<Hit>           raycast(const Vec3& from, const Vec3& dir, float maxDistance,
                                         Layer layer = Layer::Solid) const;

    // highest solid surface at or below `from`
    std::optional<float>         groundHeight(const Vec3& from, float maxDepth = 1000.f) const;
    // what the solid surface directly below `from` (within maxDepth) is made of
    Surface                      surfaceBelow(const Vec3& from, float maxDepth) const;
    // the same for the sounds (SoundSurface), the solid ground only
    uint8_t                      soundBelow(const Vec3& from, float maxDepth) const;
    // height of the nearest water surface above `at`, or if there is none, below it; within `range` either way;
    // `mesh` (if given) gets the SbScene::meshes the water belongs to
    std::optional<float>         waterLevel(const Vec3& at, float range, uint32_t* mesh = nullptr) const;
    // height of the highest water surface between `range` above and `range` below `at` (RCCars.exe 0x535130 with
    // fOnlyWater: a ray down from `range` above)
    std::optional<float>         highestWater(const Vec3& at, float range) const;

    // seas with WLOD settings are lowered by their "offset" (formats/water.h)
    void                         setSeas(const Seas& seas);
    // height of the still water at `p`, a point on a water face of `mesh` as the scene has it: lowered by the
    // sea's offset (waves left out); streams and pools stay where the scene has them
    float                        waterSurface(uint32_t mesh, const Vec3& p) const;
    // whether the car finds water in this triangle: RCCars.exe looks for it only in the seas and in the objects
    // named *stream* or *pool* (0x51c240, list at 0x575710), not in the waterfalls (*wtr_fall*), which are only seen
    bool                         isWater(const Triangle& t) const;

    // triangles listed in a cell; the same triangle shows up in every cell it touches
    template<class F>
    void forEachInCell(uint32_t x, uint32_t y, uint32_t z, F&& fn) const {
      const size_t c = z + size_t(dims[2])*(y + size_t(dims[1])*x);
      for(uint32_t i=cellStart[c]; i<cellStart[c+1]; ++i)
        fn(tris[cellTris[i]]);
      }

  private:
    bool                  intersect(const Triangle& t, const Vec3& from, const Vec3& dir, float maxT, Hit& out) const;

    std::vector<Triangle> tris;
    bool                  water = false;
    uint32_t              dims[3] = {};
    Vec3                  origin, cell;
    std::vector<uint32_t> cellStart; // cells+1 offsets into cellTris
    std::vector<uint32_t> cellTris;
    std::vector<float>    seaOffset; // per mesh, NaN: no WLOD settings
    std::vector<bool>     waterfall; // per mesh
  };

}
