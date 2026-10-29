#include "characters.h"

#include <algorithm>

#include "game/resources.h"
#include "physics/cargeometry.h"

using namespace Tempest;

CharacterView::CharacterView(Device& device)
  :device(device), pso(device) {
  }

CharacterView::~CharacterView() = default;

// The meshes carry only face normals (0x1a repeats 0x27 at every corner), which shades the low-poly bodies flat.
// Corners get the average normal of the faces around their position that bend less than 60 degrees from their own
// face, so bodies are smooth and sharp edges (a gun, a crab's shell) stay sharp.
static std::vector<std::vector<rc::Vec3>> smoothNormals(const rc::SbMesh& mesh) {
  auto faceNormal = [&](const rc::SbFace& f) {
    rc::Vec3 n = {0,0,0};
    for(size_t i=1; i+1<f.pos.size(); ++i) {
      if(std::max({f.pos[0],f.pos[i],f.pos[i+1]})>=mesh.positions.size())
        continue;
      const rc::Vec3 a = mesh.positions[f.pos[0]], b = mesh.positions[f.pos[i]], c = mesh.positions[f.pos[i+1]];
      n = n+rc::Vec3::cross(b-a,c-a);   // area-weighted
      }
    // the winding is not always consistent: follow the file's normal
    const rc::Vec3 file = f.normals.empty() ? n : f.normals[0];
    return rc::Vec3::dot(n,file)<0 ? n*-1.f : n;
    };
  std::vector<rc::Vec3> faceN(mesh.faces.size());
  std::vector<std::vector<size_t>> around(mesh.positions.size());
  for(size_t i=0; i<mesh.faces.size(); ++i) {
    faceN[i] = faceNormal(mesh.faces[i]);
    for(auto p:mesh.faces[i].pos)
      if(p<around.size())
        around[p].push_back(i);
    }
  const float crease = 0.5f;    // cos 60
  std::vector<std::vector<rc::Vec3>> out(mesh.faces.size());
  for(size_t i=0; i<mesh.faces.size(); ++i) {
    const rc::SbFace& f   = mesh.faces[i];
    const rc::Vec3    own = faceN[i].normalized();
    out[i].resize(f.pos.size());
    for(size_t c=0; c<f.pos.size(); ++c) {
      rc::Vec3 n = {0,0,0};
      if(f.pos[c]<around.size())
        for(auto j:around[f.pos[c]])
          if(rc::Vec3::dot(faceN[j].normalized(),own)>=crease)
            n = n+faceN[j];
      const float len = n.length();
      out[i][c] = len>1e-8f ? n*(1.f/len) : (c<f.normals.size() ? f.normals[c] : rc::Vec3{0,1,0});
      }
    }
  return out;
  }

const CharacterView::Model& CharacterView::model(const rc::Skeleton& sk) {
  auto& m = models[&sk];
  if(m!=nullptr)
    return *m;
  m = std::make_unique<Model>();
  const rc::SbScene& sb = sk.scene();

  std::map<std::string,std::vector<Model::Corner>> byTexture;
  for(auto& bone:sk.bones()) {
    const rc::SbMesh& mesh = sb.meshes[bone.mesh];
    if(mesh.positions.empty() || !rc::carPart(sb,bone.mesh))
      continue;
    const bool     skinned  = mesh.skin.size()==mesh.positions.size();
    const int32_t  self     = sk.boneOfMesh(bone.mesh);
    const bool     mirrored = mesh.world.determinant3()<0;
    const auto     smooth   = smoothNormals(mesh);
    size_t         fi       = 0;
    for(; fi<mesh.faces.size(); ++fi) {
      const rc::SbFace& f    = mesh.faces[fi];
      const auto*       base = f.layer(rc::LayerTag::Base);
      if(f.pos.size()<3 || base==nullptr)
        continue;
      const auto* tex = sb.texture(base->texture,sk.model());
      auto&       out = byTexture[tex ? tex->name : std::string()];
      auto corner = [&](size_t c) {
        Model::Corner v;
        const uint32_t pi = f.pos[c];
        rc::Vec3 p = pi<mesh.positions.size() ? mesh.positions[pi] : rc::Vec3{};
        rc::Vec3 n = smooth[fi][c];
        v.bone = self;
        if(skinned) {
          // bound in the file's pose of the model: the skin's bones move it from there
          auto& sv = mesh.skin[pi];
          for(uint8_t i=0; i<sv.count; ++i) {
            const int32_t b = sk.boneOfNode(sv.node[i]);
            if(b<0)
              continue;
            v.bones  [v.count] = uint16_t(b);
            v.weights[v.count] = sv.weight[i];
            ++v.count;
            }
          if(v.count>0) {
            p = sk.bones()[size_t(self)].bind.point(p);
            n = sk.bones()[size_t(self)].bind.vector(n).normalized();
            }
          }
        v.pos[0] = p.x; v.pos[1] = p.y; v.pos[2] = p.z;
        v.normal[0] = n.x; v.normal[1] = n.y; v.normal[2] = n.z;
        if(c<base->uv.size() && base->uv[c]<mesh.uvs.size()) {
          v.uv[0] = mesh.uvs[base->uv[c]].x;
          v.uv[1] = mesh.uvs[base->uv[c]].y;
          }
        else {
          v.uv[0] = v.uv[1] = 0;
          }
        return v;
        };
      const Model::Corner v0 = corner(0);
      for(size_t i=1; i+1<f.pos.size(); ++i) {
        out.push_back(v0);
        out.push_back(corner(mirrored ? i+1 : i));
        out.push_back(corner(mirrored ? i   : i+1));
        }
      }
    }
  for(auto& [tex,list]:byTexture) {
    Model::Batch b;
    b.texture = tex;
    b.first   = m->corners.size();
    b.count   = list.size();
    m->corners.insert(m->corners.end(),list.begin(),list.end());
    m->batches.push_back(b);
    }
  return *m;
  }

std::string CharacterView::textureOf(const rc::Characters::Actor& a, const std::string& own) {
  std::string name = own;
  for(auto& [from,to]:a.textures)
    if(name==from)
      name = to;
  return name;
  }

void CharacterView::preload(Resources& res, const rc::Characters& chars) {
  models.clear();   // those of the previous track's skeletons, which are gone
  for(auto* a:chars.actors()) {
    if(a->skeleton==nullptr)
      continue;
    for(auto& b:model(*a->skeleton).batches)
      res.texture(textureOf(*a,b.texture));
    }
  }

static constexpr float PoseDistance = 270.f; // metres from the camera

void CharacterView::draw(Encoder<CommandBuffer>& enc, uint8_t frame, Resources& res, const rc::Characters& chars,
                         const LevelMesh::Push& push, const std::vector<float>& shades) {
  verts.clear();
  draws.clear();
  const rc::Vec3 camera = {push.cameraPos[0],push.cameraPos[1],push.cameraPos[2]};
  for(size_t ai=0; ai<chars.actors().size(); ++ai) {
    const auto* a = chars.actors()[ai];
    if(!a->visible || a->skeleton==nullptr)
      continue;
    // not worth posing far away
    if((a->world.translation()-camera).length()>PoseDistance)
      continue;
    const rc::Skeleton& sk = *a->skeleton;
    const Model&        md = model(sk);
    sk.pose(a->seq,a->time,pose);
    skin.resize(pose.size());
    for(size_t i=0; i<pose.size(); ++i)
      skin[i] = a->world*pose[i]*sk.bones()[i].bindInverse;
    for(size_t i=0; i<pose.size(); ++i)
      pose[i] = a->world*pose[i];

    for(auto& b:md.batches) {
      Draw d;
      d.first = verts.size();
      d.count = b.count;
      d.shade = ai<shades.size() ? shades[ai] : -1.f;
      if(auto t = res.texture(textureOf(*a,b.texture))) {
        d.tex  = &t->tex;
        d.mode = t->alpha;
        }
      else {
        d.tex = &res.white();
        }
      for(size_t i=b.first; i<b.first+b.count; ++i) {
        const Model::Corner& c = md.corners[i];
        rc::Mat4 m;
        if(c.count>0) {
          for(int y=0; y<3; ++y)
            for(int x=0; x<4; ++x) {
              float v = 0;
              for(uint8_t k=0; k<c.count; ++k)
                v += skin[c.bones[k]].m[y][x]*c.weights[k];
              m.m[y][x] = v;
              }
          }
        else if(c.bone>=0) {
          m = pose[size_t(c.bone)];
          }
        const rc::Vec3 p = m.point ({c.pos[0],c.pos[1],c.pos[2]});
        const rc::Vec3 n = m.vector({c.normal[0],c.normal[1],c.normal[2]}).normalized();
        LevelMesh::Vertex v = {};
        v.pos[0] = p.x;    v.pos[1] = p.y;    v.pos[2] = p.z;
        v.normal[0] = n.x; v.normal[1] = n.y; v.normal[2] = n.z;
        v.uvBase[0] = c.uv[0]; v.uvBase[1] = c.uv[1];
        verts.push_back(v);
        }
      draws.push_back(d);
      }
    }
  if(verts.empty())
    return;
  vbo[frame%4] = device.vbo(BufferHeap::Upload,verts);
  std::stable_sort(draws.begin(),draws.end(),[](const Draw& a, const Draw& b){
    return (a.mode==rc::AlphaMode::Translucent) < (b.mode==rc::AlphaMode::Translucent);
    });

  const RenderPipeline* current = nullptr;
  LevelMesh::Push pp = push;
  for(auto& d:draws) {
    auto& p = pso.get(d.mode);
    if(current!=&p) {
      enc.setPipeline(p);
      current = &p;
      }
    pp.flags = (d.mode==rc::AlphaMode::Cutout) ? uint32_t(LevelMesh::F_Cutout) : 0u;
    if(d.shade>=0) {
      pp.flags    |= LevelMesh::F_CarLight;
      pp.carPos[3] = d.shade;
      pp.sunDir[3] = 0;
      }
    else {
      pp.carPos[3] = push.carPos[3];
      pp.sunDir[3] = push.sunDir[3];
      }
    enc.setPushData(pp);
    enc.setBinding(0,*d.tex);
    enc.setBinding(1,*d.tex);
    enc.setBinding(2,*d.tex);
    enc.setBinding(3,*d.tex);
    enc.setBinding(4,pso.noWater);
    enc.setBinding(5,res.white());
    enc.draw(vbo[frame%4],d.first,d.count);
    }
  }
