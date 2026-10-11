#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "scene.h"

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
  Water,     // texture "sea": seas, streams, pools and waterfalls; not solid, the car sinks into it
  };

Surface     surfaceOf(const SbTexture* texture);
const char* surfaceName(Surface s);

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
    // height of the nearest water surface above `at`, or if there is none, below it; within `range` either way
    // (shaped by shoreHeight)
    std::optional<float>         waterLevel(const Vec3& at, float range) const;

    // The sea is a flat sheet that ends in mid-air where the beach is lower than the water (on beach_1 the
    // surface is at 0 and the sand along the shore at -0.31). The original pulls the water down to the sand
    // there (WaterLOD magnet/clampY sliders, code not readable); we bend it down over the last `shoreBand`
    // metres to the height of the ground past the shore, never below the ground under it. `p` is a point on
    // the water as the scene has it, the result the height of the water surface there.
    float                        shoreHeight(const Vec3& p) const;
    static constexpr float       shoreBand = 1.f;

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

    // the shore of the sea (points, as zero-length edges), bucketed in shoreCell x shoreCell squares
    struct ShorePoint { float x, z, gap; }; // gap: how far the water is above the ground just past the shore
    // the nearest shore point within maxDistance and the distance to it
    const ShorePoint*     nearestShore(float x, float z, float maxDistance, float& distance) const;
    static constexpr float shoreCell = 2.f;
    std::unordered_map<uint64_t,std::vector<ShorePoint>> shore;
  };

}
