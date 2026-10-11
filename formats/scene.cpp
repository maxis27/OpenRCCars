#include "scene.h"

#include "chunk.h"

#include <algorithm>
#include <cstring>

using namespace rc;

namespace {

template<class T>
void readArray(const Chunk& c, std::vector<T>& out) {
  const uint32_t n = c.u32(0);
  c.check(4+size_t(n)*sizeof(T));
  out.resize(n);
  if(n>0)
    std::memcpy(out.data(),c.data()+4,size_t(n)*sizeof(T));
  }

LayerTag parseTag(const Chunk& c) {
  if(c.size()<4)
    return LayerTag::Unknown;
  // stored reversed: "1SAB" == BAS1
  const char* d = reinterpret_cast<const char*>(c.data());
  if(std::memcmp(d,"1SAB",4)==0) return LayerTag::Base;
  if(std::memcmp(d,"2SAB",4)==0) return LayerTag::Base2;
  if(std::memcmp(d,"1PLA",4)==0) return LayerTag::Alpha;
  if(std::memcmp(d,"1_ML",4)==0) return LayerTag::Lightmap;
  return LayerTag::Unknown;
  }

SbCollisionGrid loadCollision(const Chunk& coll) {
  SbCollisionGrid g;
  const Chunk* counts = nullptr;
  const Chunk* meshes = nullptr;
  const Chunk* faces  = nullptr;
  const Chunk* masks  = nullptr;
  std::vector<Chunk> ch;
  for(auto& c:coll.children())
    ch.push_back(c);
  for(auto& c:ch) {
    switch(c.id) {
      case 0x98:
        c.check(16);
        for(int i=0; i<3; ++i)
          g.dims[i] = c.u32(4+size_t(i)*4);
        break;
      case 0x99: g.min = {c.f32(0),c.f32(4),c.f32(8)}; break;
      case 0x9a: g.max = {c.f32(0),c.f32(4),c.f32(8)}; break;
      case 0x9d: counts = &c; break;
      case 0x9c: meshes = &c; break;
      case 0x9f: faces  = &c; break;
      case 0x00: masks  = &c; break;
      }
    }
  if(counts==nullptr || meshes==nullptr || faces==nullptr || masks==nullptr)
    throw FormatError("sb: incomplete COLL");

  const size_t cells = g.cells();
  counts->check(cells*2);
  g.cellEntries.resize(cells);
  std::memcpy(g.cellEntries.data(),counts->data(),cells*2);
  size_t total = 0;
  for(auto n:g.cellEntries)
    total += n;

  std::vector<uint32_t> faceCount;
  readArray(*faces,faceCount);
  if(faceCount.size()!=total)
    throw FormatError("sb: COLL entry count mismatch");
  meshes->check(total*2);

  const uint32_t blob = masks->u32(0);
  masks->check(4+size_t(blob));
  g.masks.assign(masks->data()+4,masks->data()+4+blob);

  g.entries.resize(total);
  size_t at = 0;
  for(size_t i=0; i<total; ++i) {
    auto& e = g.entries[i];
    uint16_t m;
    std::memcpy(&m,meshes->data()+i*2,2);
    e.mesh  = m;
    e.faces = faceCount[i];
    e.mask  = at;
    at += (size_t(e.faces)+7)/8;
    }
  if(at!=g.masks.size())
    throw FormatError("sb: COLL face masks size mismatch");
  return g;
  }

class Loader {
  public:
    explicit Loader(SbScene& s):scene(s) {}

    void node(const Chunk& c, int32_t parent, const Mat4& parentWorld) {
      if(c.is("MESH")) {
        mesh(c,parent,parentWorld);
        }
      else if(c.is("GLTX")) {
        texture(c);
        }
      else if(c.is("MARK")) {
        marker(c,parentWorld);
        }
      else if(c.is("INST")) {
        instance(c,parentWorld);
        }
      else if(c.is("SCRI")) {
        script(c);
        }
      else if(c.is("COLL")) {
        scene.collision = loadCollision(c);
        }
      else if(c.is("MODL")) {
        const int32_t prev = model;
        model = int32_t(scene.models.size());
        scene.models.emplace_back();
        for(auto& ch:c.children()) {
          if(ch.id==0x03 && ch.type!=0x92)
            scene.models[size_t(model)] = std::string(ch.str());
          if(ch.type==0x92)
            node(ch,parent,parentWorld);
          }
        model = prev;
        }
      else if(c.is("DESC") || c.is("FOLD")) {
        for(auto& ch:c.children())
          if(ch.type==0x92)
            node(ch,parent,parentWorld);
        }
      // HHID, EVOL, SPLN, ANIM ... - not needed for rendering yet
      }

  private:
    static Mat4 readTransform(const Chunk& c) {
      float v[9];
      for(int i=0; i<9; ++i)
        v[i] = c.f32(4+size_t(i)*4);
      return transformFromSb(v);
      }

    void mesh(const Chunk& c, int32_t parent, const Mat4& parentWorld) {
      const int32_t self = int32_t(scene.meshes.size());
      scene.meshes.emplace_back();
      {
        SbMesh& m = scene.meshes.back();
        m.parent  = parent;
        m.model   = model;
        m.world   = parentWorld;
      }
      std::vector<Chunk> childNodes;
      for(auto& ch:c.children()) {
        SbMesh& m = scene.meshes[size_t(self)];
        switch(ch.id) {
          case 0x03:
            m.name = std::string(ch.str());
            break;
          case 0x0b:
            m.local = readTransform(ch);
            m.world = parentWorld*m.local;
            break;
          case 0x11:
            readArray(ch,m.positions);
            break;
          case 0x13:
            readArray(ch,m.uvs);
            break;
          case 0x17:
            faces(ch,m);
            break;
          case 0x00:
            if(ch.type==0x92)
              childNodes.push_back(ch);
            break;
          }
        }
      const Mat4 world = scene.meshes[size_t(self)].world;
      for(auto& ch:childNodes)
        node(ch,self,world);
      }

    void faces(const Chunk& list, SbMesh& m) {
      m.faces.reserve(list.u32(0));
      for(auto& f:list.children(4)) {
        if(f.id!=0x18)
          continue;
        SbFace face;
        for(auto& e:f.children(4)) {
          switch(e.id) {
            case 0x19:
              readArray(e,face.pos);
              break;
            case 0x1a:
              readArray(e,face.normals);
              break;
            case 0x3a:
              for(auto& l:e.children(4)) {
                SbFace::Layer layer;
                for(auto& p:l.children(0)) {
                  if(p.id==0x3c) layer.texture = p.i32();
                  if(p.id==0x3d) layer.tag     = parseTag(p);
                  if(p.id==0x3f) readArray(p,layer.uv);
                  }
                face.layers.push_back(std::move(layer));
                }
              break;
            }
          }
        m.faces.push_back(std::move(face));
        }
      }

    void texture(const Chunk& c) {
      SbTexture t;
      t.model = model;
      for(auto& ch:c.children()) {
        switch(ch.id) {
          case 0x03: t.name  = std::string(ch.str()); break;
          case 0x08:
            t.index = ch.i32(4);
            if(ch.u32(0)>=5)
              t.material = ch.u32(20);
            break;
          case 0x32: t.embedded.assign(ch.data(),ch.data()+ch.size()); break;
          }
        }
      scene.textures.push_back(std::move(t));
      }

    void marker(const Chunk& c, const Mat4& parentWorld) {
      SbMarker mk;
      mk.world = parentWorld;
      std::vector<Chunk> nested;
      for(auto& ch:c.children()) {
        if(ch.id==0x03) mk.name  = std::string(ch.str());
        if(ch.id==0x0b) mk.world = parentWorld*readTransform(ch);
        if(ch.id==0x00 && ch.type==0x92) nested.push_back(ch); // e.g. cp_1 -> cp_1_1, cp_1_2 ...
        }
      scene.markers.push_back(std::move(mk));
      // unlike meshes, nested markers store world transforms: cp_1_1, cp_1_2 ... lead from cp_1 to cp_2,
      // and the last sub-checkpoint of a lap ends next to cp_1 (beach_1, urban_1)
      for(auto& ch:nested)
        node(ch,-1,parentWorld);
      }

    void instance(const Chunk& c, const Mat4& parentWorld) {
      SbInstance in;
      in.world = parentWorld;
      for(auto& ch:c.children()) {
        if(ch.id==0x03 && ch.type!=0x92) in.name  = std::string(ch.str());
        if(ch.id==0x0b)                  in.world = parentWorld*readTransform(ch);
        if(ch.id==0x0a && ch.size()>=4)  in.kind   = ch.u32();
        if(ch.id==0x72)                  in.model  = std::string(ch.str());
        if(ch.id==0x74)                  in.source = std::string(ch.str());
        }
      scene.instances.push_back(std::move(in));
      }

    void script(const Chunk& c) {
      SbScript s;
      s.model = model;
      for(auto& ch:c.children()) {
        if(ch.id==0x03 && ch.type!=0x92) s.name = std::string(ch.str());
        // u32 length, then the text
        if(ch.id==0x7e && ch.size()>=4) {
          const size_t n = std::min<size_t>(ch.u32(),ch.size()-4);
          s.text.assign(reinterpret_cast<const char*>(ch.data()+4),n);
          while(!s.text.empty() && s.text.back()=='\0')
            s.text.pop_back();
          }
        }
      scene.scripts.push_back(std::move(s));
      }

    SbScene& scene;
    int32_t  model = -1;
  };

}

Mat4 rc::transformFromSb(const float v[9]) {
  // same convention as the exporter that produced correct models: T * Rz * Ry * Rx * S
  const Vec3 pos   = {v[0],v[1],v[2]};
  const Vec3 scale = {v[3],v[4],v[5]};
  return Mat4::translate(pos) *
         Mat4::rotate(2,v[8]) * Mat4::rotate(1,v[7]) * Mat4::rotate(0,v[6]) *
         Mat4::scale(scale);
  }

SbScene SbScene::load(const std::vector<uint8_t>& file) {
  static const char magic[] = "CREAT Studio Scene Project";
  if(file.size()<0x40 || std::memcmp(file.data()+0x10,magic,sizeof(magic)-1)!=0)
    throw FormatError("sb: not a CREAT Studio scene");

  SbScene scene;
  Loader  ld(scene);
  for(auto& c:Range(file.data(),0,file.size())) {
    if(c.type==0x92 && c.is("DESC"))
      ld.node(c,-1,Mat4::identity());
    }
  for(auto& e:scene.collision.entries)
    if(e.mesh>=scene.meshes.size() || scene.meshes[e.mesh].faces.size()!=e.faces)
      throw FormatError("sb: COLL refers to a missing mesh");
  return scene;
  }

const SbTexture* SbScene::texture(int32_t index, int32_t model) const {
  for(auto& t:textures)
    if(t.index==index && t.model==model)
      return &t;
  return nullptr;
  }

const SbInstance* SbScene::instance(std::string_view name) const {
  for(auto& i:instances)
    if(i.name==name)
      return &i;
  return nullptr;
  }

const SbScript* SbScene::script(int32_t model, std::string_view name) const {
  for(auto& s:scripts)
    if(s.model==model && (name.empty() || s.name==name))
      return &s;
  return nullptr;
  }

int32_t SbScene::findModel(std::string_view name) const {
  for(size_t i=0; i<models.size(); ++i)
    if(models[i]==name)
      return int32_t(i);
  return -1;
  }

const SbMarker* SbScene::marker(std::string_view name) const {
  for(auto& m:markers)
    if(m.name==name)
      return &m;
  return nullptr;
  }
