#include "groundspots.h"

#include <algorithm>
#include <cmath>

using namespace rc;
using namespace rc::fx;

namespace {

constexpr float EdgeUv  = 0.453125f;      // 0x45a616: u, v at the spot's circle, from the centre
constexpr float Fade    = 1.f/255.f;      // 0x45a6b8: strength at a face `radius` away
constexpr float Lift    = 0.01f;          // m: ours, laid this much off the face against z-fighting

}

void rc::fx::layOnto(const CollisionMesh& track, const GroundSpot& spot, std::vector<MarkVertex>& out) {
  const float r = spot.radius;
  if(r<=0)
    return;
  const Vec3 ext = {r,r,r};
  const auto& tris = track.triangles();
  for(uint32_t i:track.trianglesIn(spot.pos-ext,spot.pos+ext)) {
    const auto& t = tris[i];
    if(t.surface==Surface::Water)
      continue;
    Vec3        n = t.normal;
    float       h = Vec3::dot(spot.pos-t.a,n);
    if(h<0) {
      n = n*-1.f;
      h = -h;
      }
    if(h>=r)
      continue;
    // laid off the face on its open side: up for floors (a shadow's spot can be a little under the road, where the
    // wheel sinks into it), else the spot's side
    const Vec3  off = std::abs(n.y)>0.3f && n.y<0 ? n*-Lift : n*Lift;
    const float rp = std::sqrt(r*r-h*h);
    if(rp<1e-4f)
      continue;
    // the face's own axes: u along `along` laid onto it (x or z if that is upright to it), v across
    Vec3 u = spot.along-n*Vec3::dot(spot.along,n);
    if(u.length()<1e-3f)
      u = Vec3{0,0,1}-n*n.z;
    u = u.normalized();
    const Vec3  v    = Vec3::cross(n,u);
    const Vec3  foot = spot.pos-n*h;
    const float k    = EdgeUv/rp;
    MarkVertex  m[3];
    const Vec3* p[3] = {&t.a,&t.b,&t.c};
    float lo[2] = {1e9f,1e9f}, hi[2] = {-1e9f,-1e9f};
    for(int c=0; c<3; ++c) {
      const Vec3 d = *p[c]-foot;
      m[c].pos   = *p[c]+off;
      m[c].uv[0] = 0.5f+Vec3::dot(d,u)*k;
      m[c].uv[1] = 0.5f+Vec3::dot(d,v)*k;
      for(int a=0; a<2; ++a) {
        lo[a] = std::min(lo[a],m[c].uv[a]);
        hi[a] = std::max(hi[a],m[c].uv[a]);
        }
      }
    // wholly outside the texture: nothing to show
    if(hi[0]<0 || lo[0]>1 || hi[1]<0 || lo[1]>1)
      continue;
    const float q        = h/r;
    const float strength = std::pow(Fade,q*q);
    for(auto& c:m) {
      for(int a=0; a<3; ++a)
        c.rgba[a] = spot.rgb[a]*strength;
      c.rgba[3] = 1;
      out.push_back(c);
      }
    }
  }
