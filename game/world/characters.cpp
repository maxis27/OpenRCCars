#include "characters.h"

#include <algorithm>

#include "game/resources.h"
#include "physics/cargeometry.h"

using namespace Tempest;

CharacterView::CharacterView(Device& device)
  :device(device), pso(device) {
  }

CharacterView::~CharacterView() = default;

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
    for(auto& f:mesh.faces) {
      const auto* base = f.layer(rc::LayerTag::Base);
      if(f.pos.size()<3 || base==nullptr)
        continue;
      const auto* tex = sb.texture(base->texture,sk.model());
      auto&       out = byTexture[tex ? tex->name : std::string()];
      auto corner = [&](size_t c) {
        Model::Corner v;
        const uint32_t pi = f.pos[c];
        rc::Vec3 p = pi<mesh.positions.size() ? mesh.positions[pi] : rc::Vec3{};
        rc::Vec3 n = c<f.normals.size() ? f.normals[c] : rc::Vec3{0,1,0};
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

void CharacterView::draw(Encoder<CommandBuffer>& enc, uint8_t frame, Resources& res, const rc::Characters& chars,
                         const LevelMesh::Push& push) {
  verts.clear();
  draws.clear();
  const rc::Vec3 camera = {push.cameraPos[0],push.cameraPos[1],push.cameraPos[2]};
  for(auto* a:chars.actors()) {
    if(!a->visible || a->skeleton==nullptr)
      continue;
    // not worth posing far away in the fog
    if((a->world.translation()-camera).length()>push.cameraPos[3]+20.f)
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
      std::string name = b.texture;
      for(auto& [from,to]:a->textures)
        if(name==from)
          name = to;
      Draw d;
      d.first = verts.size();
      d.count = b.count;
      if(auto t = res.texture(name)) {
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
  vbo[frame%2] = device.vbo(BufferHeap::Upload,verts);
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
    enc.setPushData(pp);
    enc.setBinding(0,*d.tex);
    enc.setBinding(1,*d.tex);
    enc.setBinding(2,*d.tex);
    enc.setBinding(3,*d.tex);
    enc.setBinding(4,pso.noWater);
    enc.draw(vbo[frame%2],d.first,d.count);
    }
  }
