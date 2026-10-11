#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "scene.h"

namespace rc {

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
    std::optional<Hit>           raycast(const Vec3& from, const Vec3& dir, float maxDistance) const;

    // highest collision surface at or below `from`
    std::optional<float>         groundHeight(const Vec3& from, float maxDepth = 1000.f) const;

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
    uint32_t              dims[3] = {};
    Vec3                  origin, cell;
    std::vector<uint32_t> cellStart; // cells+1 offsets into cellTris
    std::vector<uint32_t> cellTris;
  };

}
