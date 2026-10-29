#include "levelmesh.h"

#include <Tempest/Log>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <tuple>
#include <unordered_map>

#include "game/resources.h"
#include "shader.h"

using namespace Tempest;

namespace {

// The outline of a sea: the edges of its faces that no other face of it shares (RCCars.exe 0x520d50), and how far
// a point is from it in the xz plane. Our sea mixes LODs of neighbouring groups, so their T-junctions would look
// like outline too: an edge counts only if one of its sides is dry.
class SeaOutline {
  public:
    void add(const rc::Vec3& a, const rc::Vec3& b, const rc::Vec3& c) { tris.push_back({a,b,c}); }

    void build() {
      std::map<std::tuple<int64_t,int64_t,int64_t>,uint32_t> weld;
      auto id = [&](const rc::Vec3& p) {
        auto k = std::make_tuple(std::llround(p.x*100.0),std::llround(p.y*100.0),std::llround(p.z*100.0));
        return weld.emplace(k,uint32_t(weld.size())).first->second;
        };
      std::map<std::pair<uint32_t,uint32_t>,std::pair<int,std::pair<rc::Vec3,rc::Vec3>>> count;
      for(size_t i=0; i<tris.size(); ++i) {
        for(int e=0; e<3; ++e) {
          const rc::Vec3& a = tris[i][e];
          const rc::Vec3& b = tris[i][(e+1)%3];
          uint32_t ia = id(a), ib = id(b);
          auto& c = count[{std::min(ia,ib),std::max(ia,ib)}];
          c.first++;
          c.second = {a,b};
          }
        const auto [mn,mx] = bounds(tris[i]);
        forCells(mn,mx,triCell,[&](uint64_t k){ triGrid[k].push_back(uint32_t(i)); });
        }
      for(auto& [k,c]:count) {
        if(c.first!=1)
          continue;
        const rc::Vec3 a = c.second.first, b = c.second.second;
        const float    dx = b.x-a.x, dz = b.z-a.z, len = std::hypot(dx,dz);
        if(len<1e-4f)
          continue;
        const rc::Vec2 mid = {(a.x+b.x)*0.5f,(a.z+b.z)*0.5f};
        const rc::Vec2 n   = {-dz/len*0.05f, dx/len*0.05f};
        if(covered({mid.x+n.x,mid.y+n.y}) && covered({mid.x-n.x,mid.y-n.y}))
          continue; // water on both sides: a seam between groups
        const uint32_t i = uint32_t(edges.size());
        edges.push_back({rc::Vec2{a.x,a.z},rc::Vec2{b.x,b.z}});
        forCells({std::min(a.x,b.x),0,std::min(a.z,b.z)},{std::max(a.x,b.x),0,std::max(a.z,b.z)},edgeCell,
                 [&](uint64_t k){ edgeGrid[k].push_back(i); });
        }
      }

    // distance to the outline, at most `maxDist`
    float distance(const rc::Vec3& p, float maxDist) const {
      float best = maxDist;
      forCells({p.x-maxDist,0,p.z-maxDist},{p.x+maxDist,0,p.z+maxDist},edgeCell,[&](uint64_t k) {
        auto it = edgeGrid.find(k);
        if(it==edgeGrid.end())
          return;
        for(uint32_t i:it->second) {
          const rc::Vec2 a = edges[i].first, b = edges[i].second;
          const float    ex = b.x-a.x, ez = b.y-a.y;
          const float    t  = std::clamp(((p.x-a.x)*ex+(p.z-a.y)*ez)/(ex*ex+ez*ez),0.f,1.f);
          best = std::min(best,std::hypot(p.x-(a.x+ex*t),p.z-(a.y+ez*t)));
          }
        });
      return best;
      }

    size_t outlineEdges() const { return edges.size(); }

    // the corners of the outline, each once
    std::vector<rc::Vec2> points() const {
      std::set<std::pair<float,float>> seen;
      std::vector<rc::Vec2>            ret;
      for(auto& [a,b]:edges)
        for(const rc::Vec2& p:{a,b})
          if(seen.insert({p.x,p.y}).second)
            ret.push_back(p);
      return ret;
      }

  private:
    static constexpr float triCell = 4.f, edgeCell = 2.f;
    using Tri = std::array<rc::Vec3,3>;

    static std::pair<rc::Vec3,rc::Vec3> bounds(const Tri& t) {
      rc::Vec3 mn = t[0], mx = t[0];
      for(auto& p:t) {
        mn = {std::min(mn.x,p.x),0,std::min(mn.z,p.z)};
        mx = {std::max(mx.x,p.x),0,std::max(mx.z,p.z)};
        }
      return {mn,mx};
      }

    template<class F>
    static void forCells(const rc::Vec3& mn, const rc::Vec3& mx, float cell, F&& fn) {
      const int64_t x0 = int64_t(std::floor(mn.x/cell)), x1 = int64_t(std::floor(mx.x/cell));
      const int64_t z0 = int64_t(std::floor(mn.z/cell)), z1 = int64_t(std::floor(mx.z/cell));
      for(int64_t x=x0; x<=x1; ++x)
        for(int64_t z=z0; z<=z1; ++z)
          fn((uint64_t(uint32_t(int32_t(x)))<<32) | uint32_t(int32_t(z)));
      }

    bool covered(const rc::Vec2& p) const {
      bool hit = false;
      forCells({p.x,0,p.y},{p.x,0,p.y},triCell,[&](uint64_t k) {
        auto it = triGrid.find(k);
        if(it==triGrid.end())
          return;
        for(uint32_t i:it->second) {
          const Tri& t = tris[i];
          auto side = [&](const rc::Vec3& a, const rc::Vec3& b) { return (b.x-a.x)*(p.y-a.z)-(b.z-a.z)*(p.x-a.x); };
          const float d0 = side(t[0],t[1]), d1 = side(t[1],t[2]), d2 = side(t[2],t[0]);
          if((d0>=0 && d1>=0 && d2>=0) || (d0<=0 && d1<=0 && d2<=0))
            hit = true;
          }
        });
      return hit;
      }

    std::vector<Tri>                                        tris;
    std::vector<std::pair<rc::Vec2,rc::Vec2>>               edges;
    std::unordered_map<uint64_t,std::vector<uint32_t>>      triGrid, edgeGrid;
  };

std::optional<rc::WaterWaves> loadWaves(rc::PackFile& pack, std::string_view track, const std::string& object) {
  const std::string name = "Settings\\WLOD_"+std::string(track)+"_"+object+".ini";
  if(pack.find(name)==nullptr)
    return std::nullopt;
  auto d = pack.read(name);
  return rc::WaterWaves::parse(rc::SliderFile::parse(std::string_view(reinterpret_cast<const char*>(d.data()),d.size())));
  }

struct BatchKey {
  int32_t model = -1;
  int32_t base = -1, light = -1, mask = -1, base2 = -1;
  bool    modulate = false; // SbMesh::renderFlags 0x40000000
  int32_t group  = 0;       // AnimatedObjects group
  int32_t detail = 0;       // detail texture 1..6
  bool operator<(const BatchKey& o) const {
    return std::tie(model,base,light,mask,base2,modulate,group,detail) <
           std::tie(o.model,o.base,o.light,o.mask,o.base2,o.modulate,o.group,o.detail);
    }
  };

// Which detail texture (1..6, 0 none) a texture gets. RCCars.exe keeps it at +0x40 of its textures and draws a
// face with the smaller nonzero one of its two base textures (0x480ef0). It is set when the scene's GLTX is read
// (0x48e350): the lowest of bits 23..28 of the GLTX groups (SbTexture::groups) picks detailer (23: stone,
// cobble), detailer2 (24: sand), detailer3 (25: asphalt, concrete), detailer4 (26: gravel, paths), detailer5
// (27: floor) or detailer6 (28: no texture has it, and the game has no detailer6). Bits 21 (wood, metal) and 22
// (sand, always with 24) give none.
int32_t detailLayer(const rc::SbTexture* t) {
  if(t==nullptr)
    return 0;
  for(int32_t bit=23; bit<=28; ++bit)
    if(t->groups & (1u<<bit))
      return bit-22;
  return 0;
  }

float sliderOr(const rc::SliderFile& f, const char* key, float def) {
  return f.find(key) ? float(f.raw(key)) : def;
  }

}

struct LevelMesh::SeaSurface {
  std::vector<rc::WaterWaves>   waves;
  std::vector<SeaOutline>       outlines;
  std::vector<int32_t>          ofMesh;   // per SbScene::meshes, -1 none
  int32_t                       main = -1; // "more1", else the first one
  std::optional<rc::SbVolume>   waterBox;
  std::vector<rc::Vec3>         coast;    // seaCoast()
  };

const rc::WaterWaves* LevelMesh::mainSea() const {
  if(seaSurface==nullptr || seaSurface->main<0)
    return nullptr;
  return &seaSurface->waves[size_t(seaSurface->main)];
  }

std::vector<bool> LevelMesh::hiddenMeshes(const rc::SbScene& scene, int32_t renderQuality) {
  // "_LOD<n>" copies of the same thing are siblings under one parent. Objects keep their full detail mesh
  // without a suffix next to simplified _LOD1/_LOD2 copies; the sea is made only of LODs, where _LOD1 is the
  // coarsest and the highest number the finest, and groups far from the track have only _LOD1/_LOD2. The
  // original picks one per distance (WaterLOD); we draw the finest one of each group it keeps. Which it keeps
  // depends on RenderQual (0x51f160, _wlodScanHier_LOD): all of them on high, all but the finest on medium
  // ("Last LOD is skipped") and only _LOD1 on low, which does not switch between them at all (0x523b00).
  auto lodOf = [](const std::string& name) {
    const size_t at = name.find("_LOD");
    return (at!=std::string::npos && at+4<name.size() && name[at+4]>='1' && name[at+4]<='9') ? name[at+4]-'0' : 0;
    };
  std::map<int32_t,int> finest;
  std::map<int32_t,bool> plain;
  for(auto& m:scene.meshes) {
    if(m.faces.empty())
      continue;
    if(const int l = lodOf(m.name))
      finest[m.parent] = std::max(finest[m.parent],l);
    else
      plain[m.parent] = true;
    }
  auto kept = [renderQuality](int top) {
    if(renderQuality<=0)
      return 1;
    if(renderQuality==1 && top>1)
      return top-1;
    return top;
    };
  std::vector<bool> hidden(scene.meshes.size(),false);
  for(size_t i=0; i<scene.meshes.size(); ++i) {
    auto&     m = scene.meshes[i];
    const int l = lodOf(m.name);
    hidden[i] = l>0 && (plain[m.parent] || l!=kept(finest[m.parent]));
    }
  return hidden;
  }

WaterBlock WaterBlock::from(const rc::WaterWaves& w, const rc::CoastFoam& foam) {
  const float a = w.angle*3.14159265f/180.f;
  WaterBlock b;
  b.wave[0]  = w.amp;      b.wave[1]  = w.period;          b.wave[2]  = w.speed;      b.wave[3]  = w.amp2;
  b.wave2[0] = w.period2;  b.wave2[1] = 1.f/w.length2;    b.wave2[2] = w.center.x;   b.wave2[3] = w.center.z;
  b.dir[0]   = std::cos(a); b.dir[1]  = std::sin(a);       b.dir[2]   = w.offset;     b.dir[3]   = w.magnetOffset;
  b.edge[0]  = w.magnetRadius>0 ? 1.f/w.magnetRadius : 0.f;
  b.edge[1]  = w.alphaRadius;
  b.edge[2]  = w.distAlphaClampMin;
  b.edge[3]  = w.distAlphaClampMax;
  b.color[0] = w.colorMin/255.f; b.color[1] = w.colorMax/255.f; b.color[2] = 1.f; b.color[3] = foam.heightOn;
  b.foam[0]  = foam.alphaMin;    b.foam[1]  = foam.alphaMax;    b.foam[2]  = foam.scrollVel;
  return b;
  }

LevelMesh::Detail LevelMesh::Detail::load(rc::PackFile& pack) {
  Detail d;
  if(pack.find("Settings\\Detail.ini")==nullptr)
    return d;
  auto data = pack.read("Settings\\Detail.ini");
  auto f    = rc::SliderFile::parse(std::string_view(reinterpret_cast<const char*>(data.data()),data.size()));
  for(int i=0; i<6; ++i) {
    const std::string n = i==0 ? "" : std::to_string(i+1);
    d.density[i] = sliderOr(f,("Density"+n).c_str(),d.density[i]);
    d.lodBias[i] = sliderOr(f,("LODBias"+n).c_str(),d.lodBias[i]);
    d.scale[i]   = sliderOr(f,("Scale"+n).c_str(),  d.scale[i]);
    }
  d.distObj = sliderOr(f,"DistObj",d.distObj);
  return d;
  }

LevelMesh::LevelMesh(Device& device, Resources& res, const rc::SbScene& scene, const rc::CollisionMesh* ground,
                     std::string_view track, const std::vector<int32_t>* groups, int32_t renderQuality)
  :pso(device) {
  bbox[0] = { 1e9f, 1e9f, 1e9f};
  bbox[1] = {-1e9f,-1e9f,-1e9f};

  const auto hidden = hiddenMeshes(scene,renderQuality);
  auto isSea = [&](const rc::SbMesh& m, const rc::SbFace& f) {
    auto base = f.layer(rc::LayerTag::Base);
    return base!=nullptr && f.layer(rc::LayerTag::Alpha)==nullptr &&
           rc::surfaceOf(scene.texture(base->texture,m.model))==rc::Surface::Water;
    };

  // 0. seas with waves: the node above the sea faces that has a WLOD_<track>_<node>.ini, e.g. "more1"
  std::vector<rc::WaterWaves> seas;
  std::vector<std::string>    seaNames;
  std::vector<float>          seaEnv;   // strength of the environment map, if the node has one (SbMesh::flags)
  std::map<int32_t,int32_t>   seaNode;  // node -> sea
  std::vector<int32_t>        seaOf(scene.meshes.size(),-1);
  if(!track.empty()) {
    std::map<int32_t,int32_t> node; // mesh -> sea, -1 none
    for(size_t mi=0; mi<scene.meshes.size(); ++mi) {
      auto& m = scene.meshes[mi];
      if(hidden[mi] || std::none_of(m.faces.begin(),m.faces.end(),[&](const rc::SbFace& f){ return isSea(m,f); }))
        continue;
      for(int32_t p=int32_t(mi); p>=0 && seaOf[mi]<0; p=scene.meshes[size_t(p)].parent) {
        auto it = node.find(p);
        if(it==node.end()) {
          int32_t id = -1;
          if(auto w = loadWaves(res.pack(),track,scene.meshes[size_t(p)].name)) {
            id = int32_t(seas.size());
            seas.push_back(*w);
            seaNames.push_back(scene.meshes[size_t(p)].name);
            seaEnv.push_back((scene.meshes[size_t(p)].flags & 0x20000) ? w->alphaEnvMap : 0.f);
            seaNode[p] = id;
            }
          it = node.emplace(p,id).first;
          }
        seaOf[mi] = it->second;
        }
      }
    }
  // the outline of each sea, measured inside the scene's "water_box" only (RCCars.exe 0x5211a0)
  std::vector<SeaOutline> outlines(seas.size());
  for(size_t mi=0; mi<scene.meshes.size(); ++mi) {
    auto& m = scene.meshes[mi];
    if(seaOf[mi]<0)
      continue;
    for(auto& f:m.faces) {
      if(!isSea(m,f) || f.pos.size()<3)
        continue;
      auto at = [&](size_t i) { return m.world.point(m.positions[f.pos[i]<m.positions.size() ? f.pos[i] : 0]); };
      for(size_t i=1; i+1<f.pos.size(); ++i)
        outlines[size_t(seaOf[mi])].add(at(0),at(i),at(i+1));
      }
    }
  for(size_t i=0; i<outlines.size(); ++i) {
    outlines[i].build();
    Log::i("water \"",seaNames[i],"\": ",outlines[i].outlineEdges()," outline edges");
    }
  const rc::SbVolume* waterBox = scene.volume("water_box");
  rc::CoastFoam foam;
  if(!seas.empty() && res.pack().find("Settings\\waterLOD_coast.ini")!=nullptr) {
    auto d = res.pack().read("Settings\\waterLOD_coast.ini");
    foam = rc::CoastFoam::parse(rc::SliderFile::parse(std::string_view(reinterpret_cast<const char*>(d.data()),d.size())));
    }
  // the foam follows the waves of the main sea ("more1"); which one the original links it to is not known
  int32_t foamSea = seas.empty() ? -1 : 0;
  for(size_t i=0; i<seaNames.size(); ++i)
    if(seaNames[i]=="more1")
      foamSea = int32_t(i);

  // the detail textures
  const Detail detail = Detail::load(res.pack());
  for(int i=0; i<6; ++i) {
    // the game has no detailer6: what would get it stays without
    auto t = i==5 ? nullptr : res.texture(i==0 ? std::string("detailer") : "detailer"+std::to_string(i+1));
    detailTex[i] = t!=nullptr ? &t->tex : nullptr;
    WaterBlock b;
    b.detail[0] = detail.scale[i]*0.1f;
    b.detail[1] = detail.density[i]/256.f;
    b.detail[2] = detail.lodBias[i]*0.04f-2.f;
    detailUbo[i] = device.ubo(b);
    }
  detailDist = detail.distObj*0.1f;

  // 1. bucket triangles by texture layers
  struct Bucket {
    std::vector<Vertex>       verts;
    std::vector<Batch::Range> ranges;  // detail buckets: per mesh
    size_t                    mesh = size_t(-1);
    };
  std::map<BatchKey,Bucket> buckets;
  for(size_t mi=0; mi<scene.meshes.size(); ++mi) {
    auto& m = scene.meshes[mi];
    if(m.positions.empty())
      continue;
    if(hidden[mi]) {
      info.skippedMeshes++;
      continue;
      }
    for(auto& f:m.faces) {
      const size_t n = f.pos.size();
      if(n<3)
        continue;
      auto base  = f.layer(rc::LayerTag::Base);
      auto light = f.layer(rc::LayerTag::Lightmap);
      auto mask  = f.layer(rc::LayerTag::Alpha);
      auto base2 = f.layer(rc::LayerTag::Base2);
      if(base==nullptr) {
        info.untextured++;  // helper geometry without material
        continue;
        }
      BatchKey key;
      key.model = m.model;
      key.group = groups!=nullptr && mi<groups->size() ? (*groups)[mi] : 0;
      key.base  = base->texture;
      key.light = light ? light->texture : -1;
      key.modulate = (m.renderFlags & 0x40000000)!=0;
      if(mask) {
        key.mask  = mask->texture;
        key.base2 = base2 ? base2->texture : -1;
        }
      // a sea with waves carries the distance to its outline in uvMask.x (F_Waves); other water (texture "sea")
      // its depth above the solid ground (F_Water)
      const int32_t sea   = isSea(m,f) ? seaOf[mi] : -1;
      const bool    water = sea<0 && ground!=nullptr && isSea(m,f);
      const float   reach = sea>=0 ? std::max(seas[size_t(sea)].magnetRadius,seas[size_t(sea)].alphaRadius)+0.5f : 0.f;
      if(sea>=0) {
        key.mask  = -3; // own batch per sea
        key.base2 = sea;
        }
      if(water)
        key.mask = -2; // own batch
      // the depth of the water at `p` (streams and pools stay flat where the scene has them, as in RCCars.exe)
      auto shapeWater = [&](float* p) {
        const rc::Vec3 at    = {p[0],p[1],p[2]};
        const auto     floor = ground->groundHeight(at+rc::Vec3{0,0.05f,0},50.f); // not from higher up: rocks overhang the sea
        return floor ? p[1]-*floor : 100.f;
        };
      // the depth is interpolated per vertex: split big sea triangles near the shore so that it follows the seabed
      std::function<void(std::vector<Vertex>&,const Vertex&,const Vertex&,const Vertex&,int)> subdivideWater;
      subdivideWater = [&](std::vector<Vertex>& dst, const Vertex& a, const Vertex& b, const Vertex& c, int level) {
        auto len = [](const Vertex& x, const Vertex& y) { return std::hypot(x.pos[0]-y.pos[0],x.pos[2]-y.pos[2]); };
        const float shallowest = std::min({a.uvMask[0],b.uvMask[0],c.uvMask[0]});
        if(level==0 || shallowest>1.f || std::max({len(a,b),len(b,c),len(c,a)})<0.3f) {
          dst.push_back(a); dst.push_back(b); dst.push_back(c);
          return;
          }
        auto mid = [&](const Vertex& x, const Vertex& y) {
          Vertex v;
          auto lerp = [](const float* s, const float* t, float* o, int k) { for(int i=0;i<k;++i) o[i] = (s[i]+t[i])*0.5f; };
          lerp(x.pos,y.pos,v.pos,3);       lerp(x.normal,y.normal,v.normal,3);
          lerp(x.uvBase,y.uvBase,v.uvBase,2); lerp(x.uvLight,y.uvLight,v.uvLight,2);
          lerp(x.uvBase2,y.uvBase2,v.uvBase2,2);
          v.uvMask[1] = (x.uvMask[1]+y.uvMask[1])*0.5f; // the scene's height
          v.pos[1]    = v.uvMask[1];
          v.uvMask[0] = shapeWater(v.pos);
          return v;
          };
        const Vertex ab = mid(a,b), bc = mid(b,c), ca = mid(c,a);
        subdivideWater(dst,a,ab,ca,level-1);
        subdivideWater(dst,ab,b,bc,level-1);
        subdivideWater(dst,ca,bc,c,level-1);
        subdivideWater(dst,ab,bc,ca,level-1);
        };
      if(sea<0 && !water && !key.modulate) {
        const int32_t d1 = detailLayer(scene.texture(key.base,key.model));
        const int32_t d2 = mask && base2 ? detailLayer(scene.texture(key.base2,key.model)) : 0;
        key.detail = (d1>0 && d2>0) ? std::min(d1,d2) : std::max(d1,d2);
        if(key.detail>0 && detailTex[key.detail-1]==nullptr)
          key.detail = 0;
        }
      auto& bucket = buckets[key];
      auto& out    = bucket.verts;
      if(key.detail>0 && bucket.mesh!=mi) {
        bucket.mesh = mi;
        Batch::Range r;
        r.first = out.size();
        r.lo    = { 1e9f, 1e9f, 1e9f};
        r.hi    = {-1e9f,-1e9f,-1e9f};
        bucket.ranges.push_back(r);
        }
      const size_t before = out.size();

      auto uv = [&](const rc::SbFace::Layer* l, size_t corner, float* dst) {
        dst[0] = dst[1] = 0;
        if(l==nullptr || corner>=l->uv.size() || l->uv[corner]>=m.uvs.size())
          return;
        dst[0] = m.uvs[l->uv[corner]].x;
        dst[1] = m.uvs[l->uv[corner]].y;
        };
      auto vert = [&](size_t corner) {
        Vertex v = {};
        const uint32_t pi = f.pos[corner];
        if(pi>=m.positions.size())
          return v;
        const rc::Vec3 p = m.world.point(m.positions[pi]);
        const rc::Vec3 nr = corner<f.normals.size() ? m.world.vector(f.normals[corner]).normalized() : rc::Vec3{0,1,0};
        v.pos[0] = p.x;  v.pos[1] = p.y;  v.pos[2] = p.z;
        v.normal[0] = nr.x; v.normal[1] = nr.y; v.normal[2] = nr.z;
        uv(base, corner,v.uvBase);
        uv(light,corner,v.uvLight);
        uv(mask, corner,v.uvMask);
        uv(base2,corner,v.uvBase2);
        if(water) {
          v.uvMask[1] = v.pos[1];
          v.uvMask[0] = shapeWater(v.pos);
          }
        if(sea>=0) {
          const bool inBox = waterBox==nullptr || waterBox->contains(p);
          v.uvMask[0] = inBox ? outlines[size_t(sea)].distance(p,reach) : reach;
          v.uvMask[1] = 0;
          }
        if(mask!=nullptr && base2==nullptr)
          v.pos[1] += 0.01f; // shore foam lies on the sand, lift it to keep it from z-fighting
        bbox[0] = {std::min(bbox[0].x,p.x),std::min(bbox[0].y,p.y),std::min(bbox[0].z,p.z)};
        bbox[1] = {std::max(bbox[1].x,p.x),std::max(bbox[1].y,p.y),std::max(bbox[1].z,p.z)};
        return v;
        };
      // polygon -> triangle fan
      const Vertex v0 = vert(0);
      for(size_t i=1; i+1<n; ++i) {
        if(water) {
          subdivideWater(out,v0,vert(i),vert(i+1),3);
          continue;
          }
        out.push_back(v0);
        out.push_back(vert(i));
        out.push_back(vert(i+1));
        }
      if(key.detail>0) {
        auto& r = bucket.ranges.back();
        for(size_t i=before; i<out.size(); ++i) {
          const rc::Vec3 p = {out[i].pos[0],out[i].pos[1],out[i].pos[2]};
          r.lo = {std::min(r.lo.x,p.x),std::min(r.lo.y,p.y),std::min(r.lo.z,p.z)};
          r.hi = {std::max(r.hi.x,p.x),std::max(r.hi.y,p.y),std::max(r.hi.z,p.z)};
          }
        r.count = out.size()-r.first;
        }
      }
    }

  // 2. resolve textures, build one vertex buffer
  std::vector<Vertex> all;
  for(auto& [key,bucket]:buckets) {
    auto& verts = bucket.verts;
    Batch b;
    b.first  = all.size();
    b.count  = verts.size();
    b.group  = key.group;
    b.detail = key.detail;
    b.ranges = std::move(bucket.ranges);
    for(auto& r:b.ranges)
      r.first += b.first;
    b.tex[0] = sceneTexture(res,scene,key.base,key.model,&b.mode);
    if(auto t = scene.texture(key.base,key.model); t!=nullptr && rc::PackFile::normalize(t->name).rfind("sky",0)==0) {
      b.sky   = true;
      b.flags = 0;
      }
    b.tex[1] = key.light>=0 ? sceneTexture(res,scene,key.light,key.model,nullptr) : nullptr;
    b.tex[2] = key.mask >=0 ? sceneTexture(res,scene,key.mask, key.model,nullptr) : nullptr;
    if(key.mask==-2) {
      b.flags |= F_Water;
      b.mode   = rc::AlphaMode::Translucent;
      }
    if(key.mask==-3) {
      b.flags |= F_Water | F_Waves;
      b.mode   = rc::AlphaMode::Translucent;
      b.water  = key.base2;
      b.envMap = seaEnv[size_t(key.base2)];
      b.tex[2] = nullptr;
      }
    b.tex[3] = key.base2>=0 && key.mask!=-3 ? sceneTexture(res,scene,key.base2,key.model,nullptr) : nullptr;
    if(b.tex[1]!=nullptr)
      b.flags |= F_Lightmap;
    if(b.tex[2]!=nullptr && b.tex[3]!=nullptr) {
      b.flags |= F_Blend;
      b.mode   = rc::AlphaMode::Opaque; // base alpha is not used for transparency on blended terrain
      }
    else if(b.tex[2]!=nullptr) {
      // a mask without a second texture fades the base out: the foam along the shore (coast_base / coast)
      b.flags |= F_Mask;
      b.mode   = rc::AlphaMode::Translucent;
      b.water  = foamSea;
      }
    if(key.modulate) {
      // darkens or lightens the ground under it: its grey (128) leaves it as it is
      b.flags = F_Modulate;
      b.mode  = rc::AlphaMode::Opaque;
      }
    if(b.mode==rc::AlphaMode::Cutout)
      b.flags |= F_Cutout;
    for(auto& t:b.tex)
      if(t==nullptr)
        t = &res.white();
    all.insert(all.end(),verts.begin(),verts.end());
    batches.push_back(b);
    }

  // opaque first, the strips multiplied onto the ground over it, then the water, then the rest of the translucent
  // batches: the foam is surf on top of the water
  auto order = [](const Batch& b) {
    if(b.mode!=rc::AlphaMode::Translucent)
      return (b.flags & F_Modulate) ? 1 : 0;
    return (b.flags & F_Water) ? 2 : 3;
    };
  std::stable_sort(batches.begin(),batches.end(),[&](const Batch& a, const Batch& b){
    return order(a) < order(b);
    });

  for(auto& w:seas)
    waterUbo.push_back(device.ubo(WaterBlock::from(w,foam)));

  env        = &ScenePipelines::environment(res);
  white      = &res.white();
  seaSurface = std::make_shared<SeaSurface>();
  seaSurface->waves    = std::move(seas);
  seaSurface->outlines = std::move(outlines);
  seaSurface->main     = foamSea;
  // every mesh under a sea's node, the hidden LODs too: the collision has them
  seaSurface->ofMesh.assign(scene.meshes.size(),-1);
  for(size_t mi=0; mi<scene.meshes.size(); ++mi)
    for(int32_t p=int32_t(mi); p>=0; p=scene.meshes[size_t(p)].parent)
      if(auto it = seaNode.find(p); it!=seaNode.end()) {
        seaSurface->ofMesh[mi] = it->second;
        break;
        }
  if(waterBox!=nullptr)
    seaSurface->waterBox = *waterBox;

  // the sea sound follows the coast of the last sea RCCars.exe finds (0x522ed0). It looks for nodes named *more*
  // depth first (0x51eff0), and a node lists its children newest first (0x44ed80), so the last one it finds is the
  // first in the file among siblings: "more1" on beach_1, not the lagoon "more2". The sound sits at the height of
  // that sea's offset (0x523140).
  std::vector<std::vector<int32_t>> children(scene.meshes.size()+1);
  for(size_t mi=0; mi<scene.meshes.size(); ++mi) {
    const int32_t p = scene.meshes[mi].parent;
    children[p>=0 && size_t(p)<scene.meshes.size() ? size_t(p) : scene.meshes.size()].push_back(int32_t(mi));
    }
  int32_t soundSea = -1;
  std::function<void(size_t)> visit = [&](size_t n) {
    for(auto it=children[n].rbegin(); it!=children[n].rend(); ++it) {
      std::string lower = scene.meshes[size_t(*it)].name;
      for(auto& c:lower)
        c = char(std::tolower(static_cast<unsigned char>(c)));
      if(lower.find("more")==std::string::npos) {
        visit(size_t(*it));
        continue;
        }
      auto sea = seaNode.find(*it);
      soundSea = sea!=seaNode.end() ? sea->second : -1;
      }
    };
  visit(scene.meshes.size());
  if(soundSea>=0)
    for(const rc::Vec2& p:seaSurface->outlines[size_t(soundSea)].points())
      seaSurface->coast.push_back({p.x,seaSurface->waves[size_t(soundSea)].offset,p.y});

  vbo            = device.vbo(all);
  info.triangles = all.size()/3;
  info.batches   = batches.size();
  Log::i("level: ",info.triangles," triangles, ",info.batches," batches, ",
         info.skippedMeshes," LOD meshes skipped, ",info.untextured," untextured faces");
  }

ScenePipelines::ScenePipelines(Device& device) {
  auto sh   = AppShader::get("level.vert.sprv");
  auto vert = device.shader(sh.data,sh.len);
  sh        = AppShader::get("level.frag.sprv");
  auto frag = device.shader(sh.data,sh.len);

  // Faces of the original data are wound the other way than Tempest expects for front faces
  // (verified visually), so the "front" side is culled to drop back faces.
  RenderState stOpaque;
  stOpaque.setCullFaceMode(RenderState::CullMode::Front);
  stOpaque.setZTestMode(RenderState::ZTestMode::LEqual);
  opaque = device.pipeline(Topology::Triangles,stOpaque,vert,frag);

  // alpha-tested foliage / fences are usually single quads seen from both sides
  RenderState stCutout = stOpaque;
  stCutout.setCullFaceMode(RenderState::CullMode::NoCull);
  cutout = device.pipeline(Topology::Triangles,stCutout,vert,frag);

  RenderState stBlend = stCutout;
  stBlend.setBlendSource(RenderState::BlendMode::SrcAlpha);
  stBlend.setBlendDest  (RenderState::BlendMode::OneMinusSrcAlpha);
  stBlend.setZWriteEnabled(false);
  blend = device.pipeline(Topology::Triangles,stBlend,vert,frag);

  // SbMesh::renderFlags 0x40000000: RCCars.exe gives these meshes the same state as the tyre tracks (0x451310 mode
  // 3, 0x121), twice the source times the destination
  RenderState stModulate = stOpaque;
  stModulate.setBlendSource(RenderState::BlendMode::DstColor);
  stModulate.setBlendDest  (RenderState::BlendMode::SrcColor);
  stModulate.setZWriteEnabled(false);
  modulate = device.pipeline(Topology::Triangles,stModulate,vert,frag);

  noWater = device.ubo(WaterBlock());

  if(device.properties().tesselationShader) {
    sh        = AppShader::get("water.tesc.sprv");
    auto tesc = device.shader(sh.data,sh.len);
    sh        = AppShader::get("water.tese.sprv");
    auto tese = device.shader(sh.data,sh.len);
    water = device.pipeline(Topology::Triangles,stBlend,vert,tesc,tese,frag);
    }
  }

const Texture2d& ScenePipelines::environment(Resources& res) {
  auto t = res.texture("cubemap_racer");
  return t!=nullptr ? t->tex : res.white();
  }

const RenderPipeline& ScenePipelines::get(rc::AlphaMode mode) const {
  return (mode==rc::AlphaMode::Translucent) ? blend :
         (mode==rc::AlphaMode::Cutout)      ? cutout : opaque;
  }

const Texture2d* LevelMesh::sceneTexture(Resources& res, const rc::SbScene& scene, int32_t index, int32_t model, rc::AlphaMode* mode) {
  if(mode!=nullptr)
    *mode = rc::AlphaMode::Opaque;
  auto t = scene.texture(index,model);
  if(t==nullptr)
    return nullptr;

  if(!t->embedded.empty()) {
    for(auto& [idx,slot]:embeddedIndex)
      if(idx==index)
        return &embedded[slot];
    try {
      auto tex = res.decode(t->embedded);
      embedded.push_back(std::move(tex.tex));
      embeddedIndex.emplace_back(index,embedded.size()-1);
      return &embedded.back();
      }
    catch(const std::exception& e) {
      Log::e("embedded texture \"",t->name,"\": ",e.what());
      return nullptr;
      }
    }

  auto r = res.texture(t->name);
  if(r==nullptr)
    return nullptr;
  if(mode!=nullptr)
    *mode = r->alpha;
  return &r->tex;
  }

void LevelMesh::draw(Encoder<CommandBuffer>& enc, const Push& frame, Pass pass, bool drawSky) const {
  static const std::vector<rc::Mat4> none;
  draw(enc,frame,pass,rc::Mat4(),none,drawSky);
  }

void LevelMesh::draw(Encoder<CommandBuffer>& enc, const Push& frame, Pass pass,
                     const rc::Mat4& viewProj, const std::vector<rc::Mat4>& poses, bool drawSky) const {
  Push push = frame;
  const rc::Vec3 eye = {frame.cameraPos[0],frame.cameraPos[1],frame.cameraPos[2]};
  // RCCars.exe 0x480e50: the distance from the camera to the mesh's box
  auto near = [&](const Batch::Range& r) {
    const rc::Vec3 d = {std::max({r.lo.x-eye.x,0.f,eye.x-r.hi.x}),std::max({r.lo.y-eye.y,0.f,eye.y-r.hi.y}),
                        std::max({r.lo.z-eye.z,0.f,eye.z-r.hi.z})};
    return d.length()<=detailDist;
    };
  const RenderPipeline* current = nullptr;
  for(auto& b:batches) {
    if(b.sky && !drawSky)
      continue;
    // the shore's foam off (VIDEO_WaterCoast, 0x51c974: not made at all)
    if((b.flags & F_Mask) && !features.coast)
      continue;
    if((b.mode==rc::AlphaMode::Translucent)!=(pass==Pass::Translucent))
      continue;
    const bool tess = (b.flags & F_Water) && !pso.water.isEmpty();
    auto&      p    = tess ? pso.water : (b.flags & F_Modulate) ? pso.modulate : pso.get(b.mode);
    if(current!=&p) {
      enc.setPipeline(p);
      current = &p;
      }
    if(b.group>0 && size_t(b.group)<poses.size())
      (viewProj*poses[size_t(b.group)]).toColumnMajor(push.viewProj);
    else
      std::copy(std::begin(frame.viewProj),std::end(frame.viewProj),push.viewProj);
    push.envMap = features.envMap ? b.envMap : 0.f;
    enc.setBinding(0,*b.tex[0]);
    // without lightmaps (VIDEO_LightMaps off; on low RenderQual 0x4df940 also takes the lightmap___rds layers
    // off the meshes, GLTX groups bit 12) the textures are drawn as they are: a white lightmap at scale 1
    const bool lit = (b.flags & F_Lightmap) && !features.lightMaps;
    push.sunDir[3] = lit ? 1.f : frame.sunDir[3];
    enc.setBinding(1,lit ? *white : *b.tex[1]);
    enc.setBinding(2,*b.tex[2]);
    enc.setBinding(3,*b.tex[3]);
    auto drawRange = [&](size_t first, size_t count, bool detail) {
      push.flags = b.flags | (tess ? uint32_t(F_Tess) : 0u) | (detail ? uint32_t(F_Detail) : 0u);
      enc.setPushData(push);
      enc.setBinding(4,detail ? detailUbo[b.detail-1] : b.water>=0 ? waterUbo[size_t(b.water)] : pso.noWater);
      enc.setBinding(5,detail ? *detailTex[b.detail-1] : *env);
      enc.draw(vbo,first,count);
      };
    if(b.detail<=0 || b.ranges.empty() || !features.detail) {
      drawRange(b.first,b.count,false);
      continue;
      }
    // the meshes near the camera with the detail texture, the others without; neighbours alike in one draw
    size_t first = b.ranges[0].first, count = 0;
    bool   on    = near(b.ranges[0]);
    for(auto& r:b.ranges) {
      const bool n = near(r);
      if(n!=on && count>0) {
        drawRange(first,count,on);
        first = r.first;
        count = 0;
        }
      on     = n;
      count += r.count;
      }
    if(count>0)
      drawRange(first,count,on);
    }
  }

const std::vector<rc::Vec3>& LevelMesh::seaCoast() const {
  static const std::vector<rc::Vec3> none;
  return seaSurface ? seaSurface->coast : none;
  }

float LevelMesh::waveLift(uint32_t mesh, const rc::Vec3& p, float time) const {
  if(seaSurface==nullptr || mesh>=seaSurface->ofMesh.size() || seaSurface->ofMesh[mesh]<0)
    return 0;
  // the distance to the outline as the vertices of the sea get it (uvMask.x above)
  const size_t          sea   = size_t(seaSurface->ofMesh[mesh]);
  const rc::WaterWaves& w     = seaSurface->waves[sea];
  const float           reach = std::max(w.magnetRadius,w.alphaRadius)+0.5f;
  const bool            inBox = !seaSurface->waterBox || seaSurface->waterBox->contains(p);
  const float           edge  = inBox ? seaSurface->outlines[sea].distance(p,reach) : reach;
  return w.lift(p.x,p.z,time,edge);
  }
