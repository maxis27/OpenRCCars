#include "effects.h"

#include <Tempest/Log>

#include <algorithm>
#include <cmath>

#include "physics/opponents.h"
#include "physics/vehicle.h"
#include "resources.h"
#include "shader.h"

using namespace Tempest;

namespace {

// the car's wheels for the effects: hubs from the model's wheel poses
template<class Car>
rc::fx::CarState carState(const Car& car, const rc::Mat4& pose, const rc::Vec3& velocity) {
  rc::fx::CarState s;
  s.pose     = pose;
  s.velocity = velocity;
  const auto& wheels = car.geometry().wheels;
  for(size_t i=0; i<wheels.size(); ++i) {
    rc::fx::CarState::Wheel w;
    w.hub    = pose.point(car.wheelPose(i).translation());
    w.radius = wheels[i].radius;
    w.front  = wheels[i].front;
    w.left   = wheels[i].left;
    s.wheels.push_back(w);
    }
  return s;
  }

}

Effects::Effects(Device& device, Resources& res)
  : device(device) {
  try {
    settings = rc::fx::EffectSettings::load([&res](const std::string& f){ return res.pack().read(f); });
    loaded   = true;
    }
  catch(const std::exception& e) {
    Log::e("effects: ",e.what(),": no dust or spray");
    }
  if(auto t = res.texture("dust"))
    dust = &t->tex;
  if(auto t = res.texture("particle"))
    droplet = &t->tex;
  if(dust==nullptr)
    dust = &res.white();
  if(droplet==nullptr)
    droplet = &res.white();

  // the HUD's shaders: sprites in pixels with a depth, tested against the scene, not written to it
  auto sh   = AppShader::get("hud.vert.sprv");
  auto vert = device.shader(sh.data,sh.len);
  sh        = AppShader::get("hud.frag.sprv");
  auto frag = device.shader(sh.data,sh.len);
  RenderState st;
  st.setCullFaceMode(RenderState::CullMode::NoCull);
  st.setZTestMode(RenderState::ZTestMode::LEqual);
  st.setZWriteEnabled(false);
  st.setBlendSource(RenderState::BlendMode::SrcAlpha);
  st.setBlendDest  (RenderState::BlendMode::OneMinusSrcAlpha);
  pso = device.pipeline(Topology::Triangles,st,vert,frag);
  }

void Effects::clear() {
  if(player)
    player->clear();
  opponents.clear();
  }

void Effects::tick(float dt, const rc::Vehicle& car, const rc::Opponents* ai, const rc::CollisionMesh& track,
                   const rc::Vec3& camera, bool gas, bool brake) {
  if(!loaded || dt<=0)
    return;
  if(player==nullptr)
    player = std::make_unique<rc::fx::CarEffects>(settings,true);
  auto state = carState(car,car.pose(),car.velocity());
  state.gas   = gas;
  state.brake = brake;
  state.sense(track);
  player->update(state,camera,dt,rnd);

  const size_t n = ai!=nullptr ? ai->size() : 0;
  if(opponents.size()!=n) {
    opponents.clear();
    opponents.resize(n);
    }
  for(size_t i=0; i<n; ++i) {
    const auto& o  = (*ai)[i];
    auto&       fx = opponents[i];
    if(fx.fx==nullptr)
      fx.fx = std::make_unique<rc::fx::CarEffects>(settings,false);
    // the replays have no velocity of their own: from the way they moved
    const rc::Mat4 pose = o.pose();
    const rc::Vec3 at   = pose.translation();
    rc::Vec3       vel;
    if(fx.placed)
      vel = (at-fx.last)*(1.f/dt);
    if(vel.length()>50.f) {   // put back on the grid
      vel = {};
      fx.fx->clear();
      }
    fx.last   = at;
    fx.placed = true;
    auto s = carState(o,pose,vel);
    s.gas = vel.length()>0.5f;
    s.sense(track);
    fx.fx->update(s,camera,dt,rnd);
    }
  }

void Effects::draw(Encoder<CommandBuffer>& enc, uint8_t frame, int w, int h, const rc::Mat4& vp, const rc::Vec3& eye) {
  sprites.clear();
  if(player)
    player->collect(sprites);
  for(auto& o:opponents)
    if(o.fx)
      o.fx->collect(sprites);
  if(sprites.empty() || w<=0 || h<=0)
    return;

  // the projection is diagonal across, so the first row of viewProj is the camera's right axis times its scale
  const float focal = std::sqrt(vp.m[0][0]*vp.m[0][0]+vp.m[0][1]*vp.m[0][1]+vp.m[0][2]*vp.m[0][2]);
  struct Item {
    const rc::fx::Billboard* b;
    float x, y, z, depth;
    };
  std::vector<Item> items;
  items.reserve(sprites.size());
  for(auto& b:sprites) {
    const rc::Vec3& p = b.pos;
    const float z = vp.m[3][0]*p.x+vp.m[3][1]*p.y+vp.m[3][2]*p.z+vp.m[3][3];
    if(z<0.02f)
      continue;
    const rc::Vec3 c = vp.point(p);
    // the sprite is flat, at one depth: tested at its middle, the ground it stands on would hide its lower half, and
    // more of it the lower the camera looks across the ground (the ground under its lower edge is nearer by about
    // depth x size / the camera's height over it). Like RCCars.exe, which adds a depth offset of the emitter to its
    // sprites (0x459260), it is tested nearer to the camera by that much; the 4th row of viewProj is the camera's
    // forward axis
    const float    half = b.pixels ? 0.05f : std::max(b.halfW,b.halfH);
    const float    over = std::max(eye.y-p.y,0.1f);
    const float    bias = std::clamp(half+z*half/over,0.f,z-0.02f);
    const rc::Vec3 fwd  = {vp.m[3][0],vp.m[3][1],vp.m[3][2]};
    const rc::Vec3 cb   = vp.point(p-fwd*bias);
    items.push_back({&b,(c.x/z*0.5f+0.5f)*float(w),(c.y/z*0.5f+0.5f)*float(h),z,cb.z/(z-bias)});
    }
  // far to near (0x459260 draws them sorted the same way)
  std::sort(items.begin(),items.end(),[](const Item& a, const Item& b){ return a.z>b.z; });

  verts.clear();
  struct Range { const Texture2d* tex; size_t first, count; };
  std::vector<Range> ranges;
  for(auto& it:items) {
    const auto& b = *it.b;
    float hw, hh;
    if(b.pixels) {
      // pixels of a 640 wide screen
      hw = b.halfW*float(w)/640.f;
      hh = b.halfH*float(w)/640.f;
      }
    else {
      hw = b.halfW*focal/it.z*float(w)*0.5f;
      hh = b.halfH*focal/it.z*float(w)*0.5f;
      }
    if(hw<0.05f || hh<0.05f || b.rgba[3]<=0.f)
      continue;
    const float cs = std::cos(b.angle), sn = std::sin(b.angle);
    auto corner = [&](float dx, float dy, float u, float v) {
      Vertex r;
      r.pos[0] = it.x + dx*cs - dy*sn;
      r.pos[1] = it.y + dx*sn + dy*cs;
      r.uv[0]  = u;
      r.uv[1]  = v;
      for(int c=0; c<4; ++c)
        r.color[c] = b.rgba[c];
      r.depth = it.depth;
      return r;
      };
    const Vertex a = corner(-hw,-hh,0,0), bb = corner(hw,-hh,1,0), c = corner(hw,hh,1,1), d = corner(-hw,hh,0,1);
    const Texture2d* tex = b.texture==rc::fx::Billboard::Dust ? dust : droplet;
    if(ranges.empty() || ranges.back().tex!=tex)
      ranges.push_back({tex,verts.size(),0});
    for(const Vertex* v:{&a,&bb,&c,&a,&c,&d})
      verts.push_back(*v);
    ranges.back().count += 6;
    }
  if(verts.empty())
    return;

  vbo[frame%2] = device.vbo(BufferHeap::Upload,verts);
  struct Push {
    float screen[2];
    float sharpen;   // hud.frag: 0 = plain texture
    } push = {{float(w),float(h)},0};
  enc.setPipeline(pso);
  enc.setPushData(push);
  for(auto& r:ranges) {
    enc.setBinding(0,*r.tex,Sampler::bilinear(ClampMode::ClampToEdge));
    enc.draw(vbo[frame%2],r.first,r.count);
    }
  }
