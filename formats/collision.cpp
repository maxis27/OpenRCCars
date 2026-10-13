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

uint8_t rc::soundSurfaceOf(uint32_t material) {
  // RCCars.exe keeps the material bits shifted down by 4 (its bit 0 is our bit 4)
  const uint32_t m = material>>4;
  if(m & 0x001) return SoundSurface::WetSand;
  if(m & 0x002) return SoundSurface::Sand;
  if(m & 0x004) return SoundSurface::Trampled;
  if(m & 0x008) return SoundSurface::Wood;
  if(m & 0x010) return SoundSurface::Grass;
  if(m & 0x020) return SoundSurface::Gravel;
  if(m & 0x080) return SoundSurface::Stone;
  if(m & 0x100) return SoundSurface::Tin;
  if(m & 0x200) return SoundSurface::Metal;
  return SoundSurface::None;
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
      uint8_t sound = SoundSurface::None;
      for(auto& l:f.layers)
        if(const SbTexture* tx = scene.texture(l.texture,m.model)) {
          const uint8_t n = soundSurfaceOf(tx->material);
          if(n!=SoundSurface::None && (sound==SoundSurface::None || n<sound))
            sound = n;
          }
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
        t.sound   = sound;
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

  // 4. the shore of the sea: water cells (0.25 m) on the ground plan next to a cell no water covers, where the
  //    ground is still below that water (the sheet would end in mid-air). Taken from all sea triangles together,
  //    so seams between sea groups, their _LOD copies and holes around rocks sticking out are not a shore.
  if(!water)
    return;
  Vec3 mn = { 1e9f,0, 1e9f}, mx = {-1e9f,0,-1e9f};
  for(auto& t:tris)
    if(t.surface==Surface::Water)
      for(const Vec3* v:{&t.a,&t.b,&t.c}) {
        mn.x = std::min(mn.x,v->x); mn.z = std::min(mn.z,v->z);
        mx.x = std::max(mx.x,v->x); mx.z = std::max(mx.z,v->z);
        }
  const float step = 0.25f;
  const int   w    = int(std::ceil((mx.x-mn.x)/step))+3, h = int(std::ceil((mx.z-mn.z)/step))+3;
  const float x0   = mn.x-step, z0 = mn.z-step;
  constexpr float none = -1e9f;
  std::vector<float> level(size_t(w)*size_t(h),none); // water height per cell, none = dry
  for(auto& t:tris) {
    if(t.surface!=Surface::Water)
      continue;
    const int ix0 = std::max(0,  int(std::floor((std::min({t.a.x,t.b.x,t.c.x})-x0)/step)));
    const int ix1 = std::min(w-1,int(std::ceil ((std::max({t.a.x,t.b.x,t.c.x})-x0)/step)));
    const int iz0 = std::max(0,  int(std::floor((std::min({t.a.z,t.b.z,t.c.z})-z0)/step)));
    const int iz1 = std::min(h-1,int(std::ceil ((std::max({t.a.z,t.b.z,t.c.z})-z0)/step)));
    const float d = (t.b.x-t.a.x)*(t.c.z-t.a.z)-(t.c.x-t.a.x)*(t.b.z-t.a.z);
    if(d==0)
      continue;
    for(int ix=ix0; ix<=ix1; ++ix)
      for(int iz=iz0; iz<=iz1; ++iz) {
        const float px = x0+(float(ix)+0.5f)*step, pz = z0+(float(iz)+0.5f)*step;
        const float u  = ((px-t.a.x)*(t.c.z-t.a.z)-(t.c.x-t.a.x)*(pz-t.a.z))/d;
        const float v  = ((t.b.x-t.a.x)*(pz-t.a.z)-(px-t.a.x)*(t.b.z-t.a.z))/d;
        if(u<0 || v<0 || u+v>1)
          continue;
        float& l = level[size_t(ix)*size_t(h)+size_t(iz)];
        l = std::max(l,t.a.y+u*(t.b.y-t.a.y)+v*(t.c.y-t.a.y));
        }
    }
  for(int ix=1; ix+1<w; ++ix)
    for(int iz=1; iz+1<h; ++iz) {
      const float l = level[size_t(ix)*size_t(h)+size_t(iz)];
      if(l==none)
        continue;
      for(int n=0; n<4; ++n) {
        const int jx = ix+(n==0)-(n==1), jz = iz+(n==2)-(n==3);
        // dry for 0.75 m on: thin cracks between sea groups don't count
        bool dry = true;
        for(int k=1; k<=3 && dry; ++k) {
          const int kx = ix+k*((n==0)-(n==1)), kz = iz+k*((n==2)-(n==3));
          dry = kx<0 || kz<0 || kx>=w || kz>=h || level[size_t(kx)*size_t(h)+size_t(kz)]==none;
          }
        if(!dry)
          continue;
        const float px = x0+(float(jx)+0.5f)*step, pz = z0+(float(jz)+0.5f)*step;
        // the ground there, looked for from a little above the water: rocks overhanging the sea don't count
        const auto g = groundHeight(Vec3{px,l+0.5f,pz},50.f);
        if(!g || *g>=l-0.05f || l-*g>=1.f)
          continue; // the ground meets the water, or open sea at the edge of the map
        const float cx = x0+(float(ix)+0.5f)*step, cz = z0+(float(iz)+0.5f)*step;
        const ShorePoint e = {cx,cz,l-*g};
        shore[(uint64_t(uint32_t(int(std::floor(cx/shoreCell))))<<32) | uint32_t(int(std::floor(cz/shoreCell)))].push_back(e);
        break;
        }
      }
  }

const CollisionMesh::ShorePoint* CollisionMesh::nearestShore(float x, float z, float maxDistance, float& distance) const {
  const ShorePoint* best = nullptr;
  distance = maxDistance;
  const int cx = int(std::floor(x/shoreCell)), cz = int(std::floor(z/shoreCell));
  const int r  = int(std::ceil(maxDistance/shoreCell));
  for(int ix=cx-r; ix<=cx+r; ++ix)
    for(int iz=cz-r; iz<=cz+r; ++iz) {
      auto it = shore.find((uint64_t(uint32_t(ix))<<32) | uint32_t(iz));
      if(it==shore.end())
        continue;
      for(auto& e:it->second) {
        const float d = std::hypot(x-e.x,z-e.z);
        if(d<distance) {
          distance = d;
          best     = &e;
          }
        }
      }
  return best;
  }

float CollisionMesh::shoreHeight(const Vec3& p) const {
  if(shore.empty())
    return p.y;
  float d = 0;
  const ShorePoint* s = nearestShore(p.x,p.z,shoreBand,d);
  if(s==nullptr)
    return p.y;
  const float k     = 1.f-d/shoreBand;
  float       y     = p.y - s->gap*k*k*(3.f-2.f*k);
  const auto  floor = groundHeight(p+Vec3{0,0.05f,0},50.f);
  if(floor)
    y = std::max(y,std::min(p.y,*floor));
  return y;
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

uint8_t CollisionMesh::soundBelow(const Vec3& from, float maxDepth) const {
  if(auto h = raycast(from,Vec3{0,-1,0},maxDepth))
    return tris[h->triangle].sound;
  return SoundSurface::None;
  }

void CollisionMesh::setSeas(const Seas& seas) {
  seaOffset.assign(seas.ofMesh.size(),std::numeric_limits<float>::quiet_NaN());
  for(size_t i=0; i<seas.ofMesh.size(); ++i)
    if(seas.ofMesh[i]>=0)
      seaOffset[i] = seas.waves[size_t(seas.ofMesh[i])].offset;
  }

std::optional<float> CollisionMesh::waterLevel(const Vec3& at, float range) const {
  if(!water)
    return std::nullopt;
  auto level = [&](const Hit& h) {
    const uint32_t mesh = tris[h.triangle].mesh;
    if(mesh<seaOffset.size() && !std::isnan(seaOffset[mesh]))
      return h.point.y + seaOffset[mesh];
    return shoreHeight(h.point);
    };
  // the nearest surface above (under water), else the nearest one below
  if(auto h = raycast(at,Vec3{0,1,0},range,Layer::Water))
    return level(*h);
  if(auto h = raycast(at,Vec3{0,-1,0},range,Layer::Water))
    return level(*h);
  return std::nullopt;
  }
