#include "antennaview.h"

#include <Tempest/Log>

#include <algorithm>
#include <cmath>

#include "game/resources.h"
#include "physics/antenna.h"

using namespace Tempest;

AntennaView::AntennaView(Device& device, Resources& res, const rc::SbScene& sb)
  :device(device), res(res), pso(device) {
  for(size_t car=0; car<models.size(); ++car) {
    Model&        md = models[car];
    const int32_t id = sb.findModel("Car"+std::to_string(car+1)+"Antenna");
    if(id<0)
      continue;
    md.skeleton = std::make_unique<rc::Skeleton>(sb,id);
    const rc::Skeleton& sk = *md.skeleton;
    // the joints, in chain order
    std::vector<int32_t> joints;
    for(int k=1; ; ++k) {
      const int32_t b = sk.bone("jnt3_"+std::to_string(k));
      if(b<0)
        break;
      joints.push_back(b);
      md.bind.push_back(sk.bones()[size_t(b)].bind);
      md.bindInverse.push_back(sk.bones()[size_t(b)].bindInverse);
      // 0x4ff2d0 keeps each joint's own (parent space) matrix as modelled, 0x4ff0c0 takes its turn about y from
      // it: asin of minus the z of its x axis (0x4074c0)
      const rc::Mat4& local = sb.meshes[sk.bones()[size_t(b)].mesh].local;
      md.bend.push_back(std::asin(std::clamp(-local.m[2][0],-1.f,1.f)));
      }
    const int32_t carId = sb.findModel("Car"+std::to_string(car+1));
    if(joints.empty() || carId<0 || !rc::Antenna::mountOf(sb,carId,md.mount)) {
      md.skeleton.reset();
      continue;
      }
    for(auto& bone:sk.bones()) {
      const rc::SbMesh& mesh = sb.meshes[bone.mesh];
      if(mesh.positions.empty() || mesh.skin.size()!=mesh.positions.size())
        continue;
      const bool mirrored = mesh.world.determinant3()<0;
      for(auto& f:mesh.faces) {
        const auto* base = f.layer(rc::LayerTag::Base);
        if(f.pos.size()<3 || base==nullptr)
          continue;
        if(md.tex==nullptr)
          if(auto* t = sb.texture(base->texture,id))
            if(auto* r = res.texture(t->name)) {
              md.tex  = &r->tex;
              md.mode = r->alpha;
              }
        auto corner = [&](size_t c) {
          Corner v;
          const uint32_t pi = f.pos[c];
          if(pi<mesh.positions.size()) {
            v.pos = bone.bind.point(mesh.positions[pi]);
            auto& sv = mesh.skin[pi];
            for(uint8_t i=0; i<sv.count && v.count<4; ++i) {
              const int32_t b  = sk.boneOfNode(sv.node[i]);
              auto          it = std::find(joints.begin(),joints.end(),b);
              if(it==joints.end())
                continue;
              v.joint [v.count] = uint8_t(it-joints.begin());
              v.weight[v.count] = sv.weight[i];
              ++v.count;
              }
            }
          const rc::Vec3 n = c<f.normals.size() ? f.normals[c] : rc::Vec3{0,1,0};
          v.normal = bone.bind.vector(n).normalized();
          if(c<base->uv.size() && base->uv[c]<mesh.uvs.size()) {
            v.uv[0] = mesh.uvs[base->uv[c]].x;
            v.uv[1] = mesh.uvs[base->uv[c]].y;
            }
          return v;
          };
        const Corner v0 = corner(0);
        for(size_t i=1; i+1<f.pos.size(); ++i) {
          md.corners.push_back(v0);
          md.corners.push_back(corner(mirrored ? i+1 : i));
          md.corners.push_back(corner(mirrored ? i   : i+1));
          }
        }
      }
    if(md.tex==nullptr)
      md.tex = &res.white();
    Log::d("antenna of Car",car+1,": ",md.corners.size()/3," triangles, ",joints.size()," joints");
    }
  }

AntennaView::~AntennaView() = default;

bool AntennaView::has(size_t car) const {
  return car<models.size() && models[car].skeleton!=nullptr && !models[car].corners.empty();
  }

rc::Vec3 AntennaView::mount(size_t car) const {
  return has(car) ? models[car].mount : rc::Vec3{};
  }

rc::Mat4 AntennaView::rest(size_t car) const {
  // 0x4ff2d0 (0x40cb30): a quarter turn about z for every car, x up the car, y along its -x
  rc::Mat4 r;
  r.m[0][0] = 0; r.m[0][1] = -1;
  r.m[1][0] = 1; r.m[1][1] = 0;
  return r;
  }

std::vector<rc::Mat4> AntennaView::restJoints(size_t car, const rc::Mat4& carPose) const {
  std::vector<rc::Mat4> r;
  if(!has(car))
    return r;
  for(auto& b:models[car].bind)
    r.push_back(carPose*rc::Mat4::translate(models[car].mount)*b);
  return r;
  }

void AntennaView::clear() {
  verts.clear();
  draws.clear();
  }

void AntennaView::add(size_t car, const std::vector<rc::Mat4>& joints, const ModelMesh::Light& look) {
  if(!has(car))
    return;
  const Model& md = models[car];
  // 0x4ff0c0: each joint where the chain puts it, then turned about its own y by its bend as modelled; the joints
  // after it follow, so the bends add up along the antenna (Car2's, modelled bent back, stays bent back)
  std::vector<rc::Mat4> placed(joints.size());
  for(size_t j=0; j<joints.size(); ++j) {
    rc::Mat4 turn;
    if(j<md.bend.size()) {
      const float c = std::cos(md.bend[j]), s = std::sin(md.bend[j]);
      turn.m[0][0] = c;  turn.m[0][2] = s;
      turn.m[2][0] = -s; turn.m[2][2] = c;
      }
    placed[j] = (j==0 ? joints[j] : placed[j-1]*joints[j-1].inverse()*joints[j])*turn;
    }
  std::vector<rc::Mat4> skin(md.bind.size());
  for(size_t j=0; j<skin.size(); ++j)
    skin[j] = (j<placed.size() ? placed[j] : placed.empty() ? rc::Mat4() : placed.back())*md.bindInverse[j];
  Draw d;
  d.car   = car;
  d.first = verts.size();
  d.count = md.corners.size();
  d.light = look;
  for(auto& c:md.corners) {
    rc::Mat4 m = skin[0];
    if(c.count>0) {
      for(int y=0; y<3; ++y)
        for(int x=0; x<4; ++x) {
          float v = 0;
          for(uint8_t k=0; k<c.count; ++k)
            v += skin[c.joint[k]].m[y][x]*c.weight[k];
          m.m[y][x] = v;
          }
      }
    const rc::Vec3 p = m.point(c.pos);
    const rc::Vec3 n = m.vector(c.normal).normalized();
    LevelMesh::Vertex v = {};
    v.pos[0] = p.x;    v.pos[1] = p.y;    v.pos[2] = p.z;
    v.normal[0] = n.x; v.normal[1] = n.y; v.normal[2] = n.z;
    v.uvBase[0] = c.uv[0]; v.uvBase[1] = c.uv[1];
    verts.push_back(v);
    }
  draws.push_back(d);
  }

void AntennaView::draw(Encoder<CommandBuffer>& enc, uint8_t frame, const LevelMesh::Push& push) {
  if(verts.empty())
    return;
  vbo[frame%2] = device.vbo(BufferHeap::Upload,verts);
  for(auto& d:draws) {
    const Model& md = models[d.car];
    enc.setPipeline(pso.get(md.mode));
    LevelMesh::Push pp = push;
    pp.flags  = (md.mode==rc::AlphaMode::Cutout) ? uint32_t(LevelMesh::F_Cutout) : 0u;
    pp.envMap = 0;
    if(d.light.on) {
      // as ModelMesh::draw, the vertices being in world space
      pp.flags    |= LevelMesh::F_CarLight;
      pp.carPos[3] = d.light.ambient;
      pp.sunDir[0] = d.light.toLight.x; pp.sunDir[1] = d.light.toLight.y; pp.sunDir[2] = d.light.toLight.z;
      pp.sunDir[3] = d.light.direct;
      }
    enc.setPushData(pp);
    enc.setBinding(0,*md.tex);
    enc.setBinding(1,*md.tex);
    enc.setBinding(2,*md.tex);
    enc.setBinding(3,*md.tex);
    enc.setBinding(4,pso.noWater);
    enc.setBinding(5,res.white());
    enc.draw(vbo[frame%2],d.first,d.count);
    }
  }
