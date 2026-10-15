#pragma once

#include <vector>

#include "formats/collision.h"
#include "formats/math3d.h"
#include "marks.h"

namespace rc::fx {

// A light or a shadow RCCars.exe lays onto the track (0x44c640 queues it, 0x44c750 finds the faces near it,
// 0x45a220 maps it onto each face, 0x466560 draws them): a texture laid flat on every face within `radius` of
// `pos`. On a face h from `pos` it covers a circle of sqrt(radius^2 - h^2), the texture's centre at the foot of
// `pos` on the face and its edge (u, v 0.5 -+ 0.453) at that circle; the face takes it with the strength
// (1/255)^((h/radius)^2), so a spot fades away from the faces it hovers over.
struct GroundSpot {
  Vec3  pos;
  float radius = 0;
  Vec3  along{1,0,0};      // where the texture's u runs (laid onto each face)
  float rgb[3] = {1,1,1};  // the colour, times the strength
  };

// triangles (three MarkVertex each, rgba the colour times the strength) of the solid faces of `track` the spot
// lies on; water faces are left out (the seas move with their waves)
void layOnto(const CollisionMesh& track, const GroundSpot& spot, std::vector<MarkVertex>& out);

}
