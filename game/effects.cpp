#include "effects.h"

#include <Tempest/Log>

#include <algorithm>
#include <cmath>

#include "formats/scene.h"
#include "formats/fog.h"
#include "physics/opponents.h"
#include "physics/vehicle.h"
#include "resources.h"
#include "shader.h"

using namespace Tempest;

namespace {

// the car's wheels for the effects: hubs from the model's wheel poses, numbered as RCCars.exe does
template<class Car>
rc::fx::CarState carState(const Car& car, const rc::Mat4& pose, const rc::Vec3& velocity, int upgrade) {
  rc::fx::CarState s;
  s.pose     = pose;
  s.velocity = velocity;
  s.upgrade  = upgrade;
  const auto& g      = car.geometry();
  const auto& wheels = g.wheels;
  for(size_t i=0; i<wheels.size(); ++i) {
    rc::fx::CarState::Wheel w;
    const rc::Mat4 wp = car.wheelPose(i);
    w.hub    = pose.point(wp.translation());
    w.axle   = pose.vector(wp.vector({1,0,0})).normalized();
    w.radius = wheels[i].radius;
    w.front  = wheels[i].front;
    w.left   = wheels[i].left;
    const int pair = w.front ? 0 : wheels[i].name.find("MIDDLE")!=std::string::npos ? 4 : 2;
    w.slot   = pair+(w.left ? 0 : 1);
    s.wheels.push_back(w);
    }
  s.body       = pose.point((g.bodyMin+g.bodyMax)*0.5f);
  s.bodyHeight = g.bodyMax.y-g.bodyMin.y;
  if(!g.exhausts.empty()) {
    const auto& e = g.exhausts[size_t(std::clamp(upgrade,0,int(g.exhausts.size())-1))];
    s.exhaust = rc::fx::CarState::Exhaust{pose.point(e.pos),pose.vector(e.dir)};
    }
  return s;
  }

const Tempest::Texture2d* find(Resources& res, const std::string& name) {
  auto t = res.texture(name);
  return t!=nullptr ? &t->tex : &res.white();
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
  try {
    world       = rc::fx::WorldSettings::load([&res](const std::string& f){ return res.pack().read(f); });
    worldLoaded = true;
    gunfire     = rc::fx::Gunfire(world);
    footprints  = rc::fx::Footprints(world);
    }
  catch(const std::exception& e) {
    Log::e("effects: ",e.what(),": no splashes, waves, gunfire or footprints");
    }
  if(auto t = res.texture("dust"))
    dust = &t->tex;
  if(auto t = res.texture("particle"))
    droplet = &t->tex;
  if(dust==nullptr)
    dust = &res.white();
  if(droplet==nullptr)
    droplet = &res.white();

  // sprites in pixels with a depth, tested against the scene, not written to it (the HUD's, with fog)
  auto sh   = AppShader::get("sprite.vert.sprv");
  auto vert = device.shader(sh.data,sh.len);
  sh        = AppShader::get("sprite.frag.sprv");
  auto frag = device.shader(sh.data,sh.len);
  RenderState st;
  st.setCullFaceMode(RenderState::CullMode::NoCull);
  st.setZTestMode(RenderState::ZTestMode::LEqual);
  st.setZWriteEnabled(false);
  st.setBlendSource(RenderState::BlendMode::SrcAlpha);
  st.setBlendDest  (RenderState::BlendMode::OneMinusSrcAlpha);
  pso = device.pipeline(Topology::Triangles,st,vert,frag);

  // tracks, wake and foam: quads in the world
  for(int i=0; i<4; ++i)
    tyre[i] = find(res,"t_halfdry_tire2_"+std::to_string(i+1));
  wake[0] = find(res,"wtr_trace");
  wake[1] = find(res,"wtr_trace_alpha");
  foam[0] = find(res,"whr_horse");
  foam[1] = find(res,"whr_horse_alpha");
  wave[0] = find(res,"water_wave");
  wave[1] = find(res,"water_wave_alpha");
  hole    = find(res,"bullet_footprint");
  sole    = find(res,"sole_footprint");
  white   = &res.white();
  sh   = AppShader::get("marks.vert.sprv");
  vert = device.shader(sh.data,sh.len);
  sh   = AppShader::get("marks.frag.sprv");
  frag = device.shader(sh.data,sh.len);
  psoWake = device.pipeline(Topology::Triangles,st,vert,frag);
  // the tracks darken or lighten what is under them: twice the source times the destination
  st.setBlendSource(RenderState::BlendMode::DstColor);
  st.setBlendDest  (RenderState::BlendMode::SrcColor);
  psoTrack = device.pipeline(Topology::Triangles,st,vert,frag);
  }

void Effects::clear(bool keep) {
  if(player)
    player->clear(keep);
  if(!keep) {
    // a car put back keeps its smoke and the bullet holes stay (RCCars.exe 0x508b50)
    gunfire.clear();
    splashes.clear();
    shore.clear();
    footprints.clear();
    hatch.clear();
    }
  if(keep) {
    for(auto& o:opponents)
      if(o.fx)
        o.fx->clear(true);
    return;
    }
  opponents.clear();
  }

float Effects::dirt() const {
  return player ? player->dirt() : 0.f;
  }

void Effects::tick(float dt, const rc::Vehicle& car, const rc::Opponents* ai, const rc::CollisionMesh& track,
                   const rc::Vec3& camera, bool gas, bool brake) {
  if(!loaded || dt<=0)
    return;
  if(player==nullptr)
    player = std::make_unique<rc::fx::CarEffects>(settings,true);
  auto state = carState(car,car.pose(),car.velocity(),car.params().upgrade);
  state.gas   = gas;
  state.brake = brake;
  state.boost = car.boosting();
  for(size_t i=0; i<state.wheels.size(); ++i)
    state.wheels[i].slip = car.wheelSlip(i,&state.wheels[i].rimSpeed);
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
    auto s = carState(o,pose,vel,0);
    s.gas = vel.length()>0.5f;
    s.sense(track);
    fx.fx->update(s,camera,dt,rnd);
    }
  }

void Effects::setTrack(const rc::SbScene& scene, const rc::WaterWaves* sea) {
  splashes = {};
  shore    = {};
  hatch    = {};
  gunfire.clear();
  if(!worldLoaded)
    return;
  // numbered from 1 until one is missing: at most 16 splashes (0x5277a0) and 8 waves (0x5255c0)
  auto markers = [&scene](const std::string& name, int max) {
    std::vector<rc::Vec3> r;
    for(int i=1; i<=max; ++i) {
      const rc::SbMarker* m = scene.marker(name+std::to_string(i));
      if(m==nullptr)
        break;
      r.push_back(m->position());
      }
    return r;
    };
  splashes = rc::fx::WaterSplashes(world,markers("water_splash_",16));
  // the coolers (0x52b750): the box of the node and everything below it
  std::vector<rc::fx::HatchSmoke::Source> coolers;
  for(size_t i=0; i<scene.meshes.size(); ++i) {
    if(scene.meshes[i].name.find("KOOLER")==std::string::npos)
      continue;
    rc::fx::HatchSmoke::Source b;
    b.lo = { 1e9f, 1e9f, 1e9f};
    b.hi = {-1e9f,-1e9f,-1e9f};
    for(size_t j=0; j<scene.meshes.size(); ++j) {
      bool under = false;
      for(int32_t p=int32_t(j); p>=0 && !under; p=scene.meshes[size_t(p)].parent)
        under = size_t(p)==i;
      if(!under)
        continue;
      for(auto& v:scene.meshes[j].positions) {
        const rc::Vec3 w = scene.meshes[j].world.point(v);
        b.lo = {std::min(b.lo.x,w.x),std::min(b.lo.y,w.y),std::min(b.lo.z,w.z)};
        b.hi = {std::max(b.hi.x,w.x),std::max(b.hi.y,w.y),std::max(b.hi.z,w.z)};
        }
      }
    if(b.lo.x<=b.hi.x)
      coolers.push_back(b);
    }
  hatch = rc::fx::HatchSmoke(world,coolers);
  if(sea!=nullptr)
    shore = rc::fx::ShoreWaves(world,markers("water_wave_",8),*sea);
  }

void Effects::tickWorld(float dt, const rc::fx::View& view, float time, const rc::fx::ShoreWaves::Water& water,
                        const std::vector<rc::fx::Gunfire::Order>& bursts,
                        const std::vector<rc::fx::Gunfire::Target>& cars, const rc::CollisionMesh& track,
                        const std::vector<rc::fx::Footprints::Walker>& walkers) {
  if(!worldLoaded || dt<=0)
    return;
  splashes.update(view,dt,rnd);
  shore.update(view,time,dt,water,rnd);
  gunfire.update(bursts,cars,track,dt,rnd);
  footprints.update(walkers,dt);
  hatch.update(view,dt,rnd);
  }

void Effects::drawTracks(Encoder<CommandBuffer>& enc, uint8_t frame, const rc::Mat4& vp, const rc::Vec3& eye) {
  drawMarks(enc,frame,vp,eye,true);
  }

void Effects::drawMarks(Encoder<CommandBuffer>& enc, uint8_t frame, const rc::Mat4& vp, const rc::Vec3& eye, bool tracks) {
  marks.clear();
  if(player)
    player->collect(marks,eye);
  for(auto& o:opponents)
    if(o.fx)
      o.fx->collect(marks,eye);
  shore.collect(marks,eye);
  gunfire.collect(marks);
  footprints.collect(marks);
  // the tyre tracks and the footprints share a blend (0x80000021, 0x530018 and 0x50fc96)
  auto doubled = [](rc::fx::Mark::Kind k) { return k==rc::fx::Mark::Track || k==rc::fx::Mark::Sole; };
  marks.erase(std::remove_if(marks.begin(),marks.end(),[tracks,&doubled](const rc::fx::Mark& m){
    return doubled(m.kind)!=tracks;
    }),marks.end());
  markVerts.clear();
  if(marks.empty())
    return;
  // the wake before the foam (0x531ab0), the waves, the bullet holes; each kind and texture in a run of its own
  std::stable_sort(marks.begin(),marks.end(),[](const rc::fx::Mark& a, const rc::fx::Mark& b){
    return a.kind<b.kind || (a.kind==b.kind && a.kind==rc::fx::Mark::Track && a.texture<b.texture);
    });
  struct Range { rc::fx::Mark::Kind kind; uint8_t texture; size_t first, count; };
  std::vector<Range> ranges;
  for(auto& m:marks) {
    if(ranges.empty() || ranges.back().kind!=m.kind || ranges.back().texture!=m.texture)
      ranges.push_back({m.kind,m.texture,markVerts.size(),0});
    for(int i:{0,1,2,0,2,3}) {
      const auto& v = m.v[i];
      MarkVertex r;
      r.pos[0] = v.pos.x; r.pos[1] = v.pos.y; r.pos[2] = v.pos.z;
      r.uv[0]  = v.uv[0]; r.uv[1]  = v.uv[1];
      r.uv2[0] = v.uv2[0]; r.uv2[1] = v.uv2[1];
      for(int c=0; c<4; ++c)
        r.color[c] = v.rgba[c];
      markVerts.push_back(r);
      }
    ranges.back().count += 6;
    }
  auto& vbuf = markVbo[tracks ? 1 : 0][frame%2];
  vbuf = device.vbo(BufferHeap::Upload,markVerts);
  struct Push {
    float    viewProj[16];
    uint32_t mode;
    uint32_t fogColor;
    float    fogDensity;
    } push = {};
  vp.toColumnMajor(push.viewProj);
  push.fogColor   = fogColor;
  push.fogDensity = fogDensity;
  const auto repeat = Sampler::bilinear(ClampMode::Repeat);
  for(auto& r:ranges) {
    const Texture2d* t[2] = {tyre[0],tyre[0]};
    switch(r.kind) {
      case rc::fx::Mark::Track: t[0] = t[1] = tyre[std::min<size_t>(r.texture,3)]; break;
      case rc::fx::Mark::Wake:  t[0] = wake[0]; t[1] = wake[1]; break;
      case rc::fx::Mark::Foam:  t[0] = foam[0]; t[1] = foam[1]; break;
      case rc::fx::Mark::Wave:  t[0] = wave[0]; t[1] = wave[1]; break;
      case rc::fx::Mark::Decal: t[0] = hole;    t[1] = white;   break;
      case rc::fx::Mark::Sole:  t[0] = sole;    t[1] = white;   break;
      }
    push.mode = doubled(r.kind) ? 0u : 1u;
    enc.setPipeline(doubled(r.kind) ? psoTrack : psoWake);
    enc.setPushData(push);
    enc.setBinding(0,*t[0],repeat);
    enc.setBinding(1,*t[1],Sampler::bilinear(ClampMode::ClampToEdge));
    enc.draw(vbuf,r.first,r.count);
    }
  }

void Effects::draw(Encoder<CommandBuffer>& enc, uint8_t frame, int w, int h, const rc::Mat4& vp, const rc::Vec3& eye) {
  drawMarks(enc,frame,vp,eye,false);
  sprites.clear();
  if(player)
    player->collect(sprites);
  for(auto& o:opponents)
    if(o.fx)
      o.fx->collect(sprites);
  splashes.collect(sprites);
  gunfire.collect(sprites);
  hatch.collect(sprites);
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
    const float    bias = std::clamp(b.depthBias>=0 ? b.depthBias : half+z*half/over,0.f,z-0.02f);
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
      r.fog   = rc::Fog::visibility(fogDensity,it.z); // the sprite is at one depth, w = z
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
    float    screen[2];
    uint32_t fogColor;
    } push = {{float(w),float(h)},fogColor};
  enc.setPipeline(pso);
  enc.setPushData(push);
  for(auto& r:ranges) {
    enc.setBinding(0,*r.tex,Sampler::bilinear(ClampMode::ClampToEdge));
    enc.draw(vbo[frame%2],r.first,r.count);
    }
  }
