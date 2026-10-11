#include "collision.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <tuple>

using namespace rc;

Surface rc::surfaceOf(const SbTexture* texture) {
  if(texture==nullptr)
    return Surface::Default;
  // the game finds its water by this name too (RCCars.exe: "sea", "sea_alpha" next to the water code)
  if(texture->name=="sea")
    return Surface::Water;
  const uint32_t m = texture->material;
  if(m & (1u<<4)) return Surface::WetSand;
  if(m & (1u<<5)) return Surface::Sand;
  if(m & (1u<<6)) return Surface::DeepSand;
  if(m & (1u<<9)) return Surface::Gravel;
  return Surface::Default;
  }

const char* rc::surfaceName(Surface s) {
  switch(s) {
    case Surface::Default:  return "default";
    case Surface::Sand:     return "sand";
    case Surface::WetSand:  return "wet sand";
    case Surface::DeepSand: return "deep sand";
    case Surface::Gravel:   return "gravel";
    case Surface::Water:    return "water";
    }
  return "?";
  }

CollisionMesh::CollisionMesh(const SbScene& scene) {
  const auto& grid = scene.collision;
  if(grid.empty())
    return;

  // 1. faces referenced by any cell
  std::vector<std::vector<bool>> used(scene.meshes.size());
  for(auto& e:grid.entries) {
    auto& u = used[e.mesh];
    u.resize(e.faces);
    for(uint32_t f=0; f<e.faces; ++f)
      if(grid.hasFace(e,f))
        u[f] = true;
    }

  // 2. triangulate them (fans, like the renderer); faceTris[mesh][face] = {first, count}
  std::vector<std::vector<std::pair<uint32_t,uint32_t>>> faceTris(scene.meshes.size());
  for(size_t mi=0; mi<scene.meshes.size(); ++mi) {
    if(used[mi].empty())
      continue;
    const SbMesh& m = scene.meshes[mi];
    faceTris[mi].resize(m.faces.size());
    for(size_t fi=0; fi<m.faces.size(); ++fi) {
      if(!used[mi][fi])
        continue;
      const SbFace& f = m.faces[fi];
      const auto*   base = f.layer(LayerTag::Base);
      const Surface surf = surfaceOf(base ? scene.texture(base->texture,m.model) : nullptr);
      water = water || surf==Surface::Water;
      auto at = [&](size_t i) {
        return f.pos[i]<m.positions.size() ? m.world.point(m.positions[f.pos[i]]) : Vec3{};
        };
      const uint32_t first = uint32_t(tris.size());
      for(size_t i=1; i+1<f.pos.size(); ++i) {
        Triangle t;
        t.a = at(0);
        t.b = at(i);
        t.c = at(i+1);
        const Vec3  n = Vec3::cross(t.b-t.a,t.c-t.a);
        const float l = n.length();
        if(l<1e-10f)
          continue; // degenerate
        t.normal = n*(1.f/l);
        t.mesh   = uint32_t(mi);
        t.face   = uint32_t(fi);
        t.surface = surf;
        tris.push_back(t);
        }
      faceTris[mi][fi] = {first,uint32_t(tris.size())-first};
      }
    }

  // 3. the same grid, listing triangles instead of faces
  for(int i=0; i<3; ++i)
    dims[i] = std::max(grid.dims[i],1u);
  origin = grid.min;
  cell   = {(grid.max.x-grid.min.x)/float(dims[0]),
            (grid.max.y-grid.min.y)/float(dims[1]),
            (grid.max.z-grid.min.z)/float(dims[2])};

  const size_t cells = grid.cells();
  cellStart.resize(cells+1);
  size_t entry = 0;
  for(size_t c=0; c<cells; ++c) {
    cellStart[c] = uint32_t(cellTris.size());
    for(uint16_t i=0; i<grid.cellEntries[c]; ++i, ++entry) {
      auto& e = grid.entries[entry];
      for(uint32_t f=0; f<e.faces; ++f) {
        if(!grid.hasFace(e,f))
          continue;
        auto [first,count] = faceTris[e.mesh][f];
        for(uint32_t t=0; t<count; ++t)
          cellTris.push_back(first+t);
        }
      }
    }
  cellStart[cells] = uint32_t(cellTris.size());

  // 4. shore edges of the sea
  if(!water)
    return;
  auto key = [](const Vec3& p) {
    return std::make_tuple(std::lround(p.x*100),std::lround(p.y*100),std::lround(p.z*100));
    };
  using Key = decltype(key(Vec3{}));
  std::map<std::tuple<uint32_t,Key,Key>,int> uses;
  auto edgeKey = [&](uint32_t mesh, const Vec3& a, const Vec3& b) {
    const auto ka = key(a), kb = key(b);
    return ka<kb ? std::make_tuple(mesh,ka,kb) : std::make_tuple(mesh,kb,ka);
    };
  for(auto& t:tris)
    if(t.surface==Surface::Water) {
      uses[edgeKey(t.mesh,t.a,t.b)]++;
      uses[edgeKey(t.mesh,t.b,t.c)]++;
      uses[edgeKey(t.mesh,t.c,t.a)]++;
      }
  for(auto& t:tris) {
    if(t.surface!=Surface::Water)
      continue;
    const Vec3* v[3] = {&t.a,&t.b,&t.c};
    for(int i=0; i<3; ++i) {
      const Vec3& a = *v[i];
      const Vec3& b = *v[(i+1)%3];
      if(uses[edgeKey(t.mesh,a,b)]!=1)
        continue;
      const Vec3 mid = (a+b)*0.5f;
      const auto floor = groundHeight(mid+Vec3{0,0.05f,0},50.f);
      if(!floor || mid.y-*floor>=1.f)
        continue; // open sea: a seam between sea groups, or the edge of the grid
      // only where the ground just past the edge is still below the water: the water would end in mid-air.
      // Around rocks and up a beach that rises out of the sea it meets the ground by itself.
      const Vec3& o   = *v[(i+2)%3];
      Vec3        out = {b.z-a.z,0,a.x-b.x};
      if(Vec3::dot(out,o-mid)>0)
        out = out*-1.f;
      const float len = out.length();
      if(len<1e-6f)
        continue;
      const auto past = groundHeight(mid+out*(0.3f/len)+Vec3{0,2.f,0},50.f);
      if(!past || *past>=mid.y-0.05f)
        continue;
      const Edge e = {a.x,a.z,b.x,b.z};
      const int x0 = int(std::floor(std::min(a.x,b.x)/shoreCell)), x1 = int(std::floor(std::max(a.x,b.x)/shoreCell));
      const int z0 = int(std::floor(std::min(a.z,b.z)/shoreCell)), z1 = int(std::floor(std::max(a.z,b.z)/shoreCell));
      for(int x=x0; x<=x1; ++x)
        for(int z=z0; z<=z1; ++z)
          shore[(uint64_t(uint32_t(x))<<32) | uint32_t(z)].push_back(e);
      }
    }
  }

float CollisionMesh::shoreDistance(float x, float z, float maxDistance) const {
  float best = maxDistance;
  const int cx = int(std::floor(x/shoreCell)), cz = int(std::floor(z/shoreCell));
  const int r  = int(std::ceil(maxDistance/shoreCell));
  for(int ix=cx-r; ix<=cx+r; ++ix)
    for(int iz=cz-r; iz<=cz+r; ++iz) {
      auto it = shore.find((uint64_t(uint32_t(ix))<<32) | uint32_t(iz));
      if(it==shore.end())
        continue;
      for(auto& e:it->second) {
        const float ex = e.bx-e.ax, ez = e.bz-e.az, len2 = ex*ex+ez*ez;
        const float t  = len2>0 ? std::clamp(((x-e.ax)*ex+(z-e.az)*ez)/len2,0.f,1.f) : 0.f;
        best = std::min(best,std::hypot(x-(e.ax+ex*t),z-(e.az+ez*t)));
        }
      }
  return best;
  }

float CollisionMesh::shoreHeight(const Vec3& p) const {
  if(shore.empty())
    return p.y;
  const auto floor = groundHeight(p+Vec3{0,0.05f,0},50.f);
  if(!floor || p.y<=*floor)
    return p.y;
  const float depth = p.y-*floor;
  return *floor + std::min(depth,shoreSlope*shoreDistance(p.x,p.z,depth/shoreSlope));
  }

bool CollisionMesh::intersect(const Triangle& t, const Vec3& from, const Vec3& dir, float maxT, Hit& out) const {
  // Moller-Trumbore, two-sided
  const Vec3  e1  = t.b-t.a;
  const Vec3  e2  = t.c-t.a;
  const Vec3  p   = Vec3::cross(dir,e2);
  const float det = Vec3::dot(e1,p);
  if(std::abs(det)<1e-12f)
    return false;
  const float inv = 1.f/det;
  const Vec3  s   = from-t.a;
  const float u   = Vec3::dot(s,p)*inv;
  if(u<0 || u>1)
    return false;
  const Vec3  q   = Vec3::cross(s,e1);
  const float v   = Vec3::dot(dir,q)*inv;
  if(v<0 || u+v>1)
    return false;
  const float d   = Vec3::dot(e2,q)*inv;
  if(d<0 || d>maxT)
    return false;
  out.distance = d;
  out.point    = from + dir*d;
  out.normal   = Vec3::dot(t.normal,dir)>0 ? t.normal*-1.f : t.normal;
  return true;
  }

std::optional<CollisionMesh::Hit> CollisionMesh::raycast(const Vec3& from, const Vec3& dir, float maxDistance,
                                                          Layer layer) const {
  if(tris.empty() || (layer==Layer::Water && !water))
    return std::nullopt;

  const float o[3]  = {from.x,from.y,from.z};
  const float d[3]  = {dir.x,dir.y,dir.z};
  const float lo[3] = {origin.x,origin.y,origin.z};
  const float cs[3] = {cell.x,cell.y,cell.z};

  // clip the ray to the grid box
  float t0 = 0, t1 = maxDistance;
  for(int i=0; i<3; ++i) {
    const float hi = lo[i]+cs[i]*float(dims[i]);
    if(std::abs(d[i])<1e-12f) {
      if(o[i]<lo[i] || o[i]>hi)
        return std::nullopt;
      continue;
      }
    float a = (lo[i]-o[i])/d[i];
    float b = (hi   -o[i])/d[i];
    if(a>b)
      std::swap(a,b);
    t0 = std::max(t0,a);
    t1 = std::min(t1,b);
    }
  if(t0>t1)
    return std::nullopt;

  // 3D DDA over the cells the ray crosses (Amanatides & Woo)
  constexpr float inf = std::numeric_limits<float>::infinity();
  int   c[3], step[3], last[3];
  float tMax[3], tDelta[3];
  for(int i=0; i<3; ++i) {
    const float p = o[i]+d[i]*t0;
    c[i]    = std::clamp(int(std::floor((p-lo[i])/cs[i])),0,int(dims[i])-1);
    last[i] = int(dims[i]);
    if(d[i]>0) {
      step[i]   = 1;
      tMax[i]   = (lo[i]+float(c[i]+1)*cs[i]-o[i])/d[i];
      tDelta[i] = cs[i]/d[i];
      }
    else if(d[i]<0) {
      step[i]   = -1;
      last[i]   = -1;
      tMax[i]   = (lo[i]+float(c[i])*cs[i]-o[i])/d[i];
      tDelta[i] = -cs[i]/d[i];
      }
    else {
      step[i]   = 0;
      tMax[i]   = inf;
      tDelta[i] = inf;
      }
    }

  std::optional<Hit> best;
  Hit                h;
  while(true) {
    const size_t idx = size_t(c[2]) + size_t(dims[2])*(size_t(c[1]) + size_t(dims[1])*size_t(c[0]));
    for(uint32_t i=cellStart[idx]; i<cellStart[idx+1]; ++i) {
      const Triangle& tri = tris[cellTris[i]];
      if((tri.surface==Surface::Water)!=(layer==Layer::Water))
        continue;
      const float limit = best ? best->distance : t1;
      if(intersect(tri,from,dir,limit,h)) {
        h.triangle = cellTris[i];
        best       = h;
        }
      }
    const int   axis  = (tMax[0]<tMax[1]) ? (tMax[0]<tMax[2] ? 0 : 2) : (tMax[1]<tMax[2] ? 1 : 2);
    const float tExit = tMax[axis];
    // a hit inside this cell can't be beaten by triangles further along the ray
    if(best && best->distance<=tExit)
      break;
    if(tExit>t1)
      break;
    c[axis] += step[axis];
    if(c[axis]==last[axis])
      break;
    tMax[axis] += tDelta[axis];
    }
  return best;
  }

std::optional<float> CollisionMesh::groundHeight(const Vec3& from, float maxDepth) const {
  if(auto h = raycast(from,Vec3{0,-1,0},maxDepth))
    return h->point.y;
  return std::nullopt;
  }

Surface CollisionMesh::surfaceBelow(const Vec3& from, float maxDepth) const {
  if(auto h = raycast(from,Vec3{0,-1,0},maxDepth))
    return tris[h->triangle].surface;
  return Surface::Default;
  }

std::optional<float> CollisionMesh::waterLevel(const Vec3& at, float range) const {
  if(!water)
    return std::nullopt;
  // the nearest surface above (under water), else the nearest one below
  if(auto h = raycast(at,Vec3{0,1,0},range,Layer::Water))
    return shoreHeight(h->point);
  if(auto h = raycast(at,Vec3{0,-1,0},range,Layer::Water))
    return shoreHeight(h->point);
  return std::nullopt;
  }
