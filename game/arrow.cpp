#include "arrow.h"

#include <Tempest/Log>

#include <algorithm>
#include <cmath>
#include <string_view>
#include <vector>

#include "formats/scene.h"
#include "formats/settings.h"
#include "resources.h"
#include "shader.h"

using namespace Tempest;

namespace {

constexpr float DegToRad   = 3.14159265358979f/180.f;
// RCCars.exe 0x4e8e50, 0x4e8f00: degrees and colour per second, ten times both right after a checkpoint until the
// arrow is within a degree of the new way
constexpr float TurnSpeed  = 90.f;
constexpr float ShadeSpeed = 1.f;
constexpr float SnapFactor = 10.f;
constexpr float SnapDone   = 1.f;
// blinking: shown while the timer is at most 0.25 s, restarted at 0.5 s on the wrong way or once the car has been
// within 5 m of the next checkpoint
constexpr float BlinkShown = 0.25f;
constexpr float BlinkCycle = 0.5f;
constexpr float NearMiss   = 5.f;

// the angle in 0..360
float wrap(float a) {
  a = std::fmod(a,360.f);
  return a<0 ? a+360.f : a;
  }

// RCCars.exe 0x40c580: `from` turned towards `to` by at most `step` degrees, the shorter way
float approach(float from, float to, float step) {
  float d = wrap(to-from);
  if(d>180.f)
    d -= 360.f;
  if(std::abs(d)<=step)
    return wrap(to);
  return wrap(from+(d>0 ? step : -step));
  }

float angleDiff(float a, float b) {
  const float d = wrap(a-b);
  return std::min(d,360.f-d);
  }

}

DirectionArrow::DirectionArrow(Device& device, Resources& res) {
  // Settings/arrow.ini, read the way 0x4b1360 does: "NearPlane" in mm, "AngleVert" the field of view in degrees
  // (the same across), "CX", "CY" and "Len" the middle and the side of the square in percent of the screen's
  // width and height, the camera at (0, (pos_y-200)/100, (pos_z-200)/100) looking at (0, cos dir, sin dir) with
  // "dir" in degrees from 90. The camera "Arrow_cam" of RCCars.sb is replaced by them.
  try {
    const auto d = res.pack().read("Settings\\arrow.ini");
    const auto s = rc::SliderFile::parse(std::string_view(reinterpret_cast<const char*>(d.data()),d.size()));
    nearPlane = float(s.raw("NearPlane",10))*0.001f;
    fov       = float(s.raw("AngleVert",45));
    centreX   = float(s.raw("CX",50))*0.01f;
    centreY   = float(s.raw("CY",87))*0.01f;
    size      = float(s.raw("Len",17))*0.01f;
    eye[1]    = float(s.raw("pos_y",312)-200)*0.01f;
    eye[2]    = float(s.raw("pos_z",300)-200)*0.01f;
    const float dir = float(s.raw("dir",129)-90)*DegToRad;
    target[1] = std::cos(dir);
    target[2] = std::sin(dir);
    }
  catch(const std::exception& e) {
    Log::e("Settings\\arrow.ini: ",e.what());
    }

  std::vector<Vertex> bodyVerts, topVerts;
  try {
    const auto sb      = rc::SbScene::load(res.readFile("RCCarsDB/cockpit.sb"));
    const auto modelId = sb.findModel("Arrow");
    for(auto& m:sb.meshes) {
      if(m.model!=modelId || m.positions.empty())
        continue;
      // 0x4af3c9: extru9 and extru9_3 get the colour
      auto& out = (m.name=="extru9" || m.name=="extru9_3") ? topVerts : bodyVerts;
      for(auto& f:m.faces) {
        auto base = f.layer(rc::LayerTag::Base);
        auto vert = [&](size_t corner) {
          Vertex v = {};
          if(f.pos[corner]>=m.positions.size())
            return v;
          const rc::Vec3 p = m.world.point(m.positions[f.pos[corner]]);
          v.pos[0] = p.x; v.pos[1] = p.y; v.pos[2] = p.z;
          if(base!=nullptr && corner<base->uv.size() && base->uv[corner]<m.uvs.size()) {
            v.uv[0] = m.uvs[base->uv[corner]].x;
            v.uv[1] = m.uvs[base->uv[corner]].y;
            }
          return v;
          };
        for(size_t i=1; i+1<f.pos.size(); ++i) {
          out.push_back(vert(0));
          out.push_back(vert(i));
          out.push_back(vert(i+1));
          }
        }
      }
    }
  catch(const std::exception& e) {
    Log::e("RCCarsDB/cockpit.sb: ",e.what(),": no direction arrow");
    }
  if(topVerts.empty())
    Log::e("can't locate extru9 and extru9_3 on cockpit arrow");
  body = device.vbo(bodyVerts);
  tops = device.vbo(topVerts);

  if(auto t = res.texture("coc_str_2"))
    tex = &t->tex;
  else
    tex = &res.white();

  auto sh   = AppShader::get("arrow.vert.sprv");
  auto vert = device.shader(sh.data,sh.len);
  sh        = AppShader::get("arrow.frag.sprv");
  auto frag = device.shader(sh.data,sh.len);
  RenderState st;
  st.setCullFaceMode(RenderState::CullMode::NoCull);
  st.setZTestMode(RenderState::ZTestMode::LEqual);
  pso = device.pipeline(Topology::Triangles,st,vert,frag);
  }

void DirectionArrow::reset() {
  current  = aim = 0;
  shade    = shadeAim = 1;
  blink    = 0;
  nearest  = 1e6f;
  next     = size_t(-1);
  snap     = false;
  }

void DirectionArrow::tick(float dt, const Input& in) {
  // a checkpoint passed: the way on is taken at once (0x4ea8d0 -> 0x4e8ed0)
  const bool passed = next!=size_t(-1) && in.next!=next;
  if(in.next!=next) {
    next    = in.next;
    nearest = 1e6f;
    }
  nearest = std::min(nearest,in.distance);

  if(!snap) {
    aim    = wrap(in.direction/DegToRad);
    blink -= dt;
    if(blink<0) {
      blink = -1;
      if(nearest<NearMiss || in.wrongWay)
        blink = BlinkCycle;
      }
    }
  shadeAim = in.wrongWay ? 0.f : std::clamp(in.left,0.f,1.f);

  const float k = snap ? SnapFactor : 1.f;
  current = approach(current,aim,TurnSpeed*k*dt);
  if(snap && angleDiff(current,aim)<SnapDone)
    snap = false;
  if(shade<shadeAim)
    shade = std::min(shadeAim,shade+ShadeSpeed*k*dt);
  else
    shade = std::max(shadeAim,shade-ShadeSpeed*k*dt);

  if(passed) {
    snap  = true;
    blink = -1;
    }
  }

void DirectionArrow::draw(Encoder<CommandBuffer>& enc, int w, int h) const {
  if(blink>BlinkShown || w<=0 || h<=0)
    return;

  // the model turned about its upright axis: its tip (-Z) towards the way on
  const float    a  = current*DegToRad;
  rc::Mat4       model;
  model.m[0][0] =  std::cos(a); model.m[0][2] = std::sin(a);
  model.m[2][0] = -std::sin(a); model.m[2][2] = std::cos(a);

  // the camera (right-handed like Camera::view, +Z forward in view space)
  const rc::Vec3 e = {eye[0],eye[1],eye[2]};
  const rc::Vec3 f = (rc::Vec3{target[0],target[1],target[2]}-e).normalized();
  const rc::Vec3 r = rc::Vec3::cross(f,{0,1,0}).normalized();
  const rc::Vec3 u = rc::Vec3::cross(r,f);
  rc::Mat4 view;
  view.m[0][0] = r.x; view.m[0][1] = r.y; view.m[0][2] = r.z; view.m[0][3] = -rc::Vec3::dot(r,e);
  view.m[1][0] = u.x; view.m[1][1] = u.y; view.m[1][2] = u.z; view.m[1][3] = -rc::Vec3::dot(u,e);
  view.m[2][0] = f.x; view.m[2][1] = f.y; view.m[2][2] = f.z; view.m[2][3] = -rc::Vec3::dot(f,e);

  // the same field of view across and up (0x405e80 with aspect 1) over a rectangle of Len of the screen's width
  // and height: like the rest of the cockpit, laid out on 800x600 scaled to the height
  const float scale = float(h)/600.f;
  const float cx = float(w)*0.5f + (centreX-0.5f)*800.f*scale, cy = centreY*600.f*scale;
  const float hw = size*800.f*scale*0.5f, hh = size*600.f*scale*0.5f;
  const float g  = 1.f/std::tan(fov*DegToRad*0.5f);
  const float zFar = 10.f, k = zFar/(zFar-nearPlane);
  rc::Mat4 proj;
  proj.m[0][0] = g;
  proj.m[1][1] = -g;
  proj.m[2][2] = k;  proj.m[2][3] = -nearPlane*k;
  proj.m[3][2] = 1;  proj.m[3][3] = 0;
  // clip space squeezed into the rectangle (the offsets multiply w, the view depth)
  rc::Mat4 rect;
  rect.m[0][0] = hw*2.f/float(w); rect.m[0][3] = cx*2.f/float(w)-1.f;
  rect.m[1][1] = hh*2.f/float(h); rect.m[1][3] = cy*2.f/float(h)-1.f;
  rect.m[3][3] = 1;
  const rc::Mat4 mvp = rect*proj*view*model;

  struct Push {
    float mvp[16];
    float color[4];
    } push = {};
  mvp.toColumnMajor(push.mvp);

  enc.setPipeline(pso);
  enc.setBinding(0,*tex,Sampler::bilinear(ClampMode::ClampToEdge));
  push.color[0] = push.color[1] = push.color[2] = push.color[3] = 1.f;
  enc.setPushData(push);
  enc.draw(body);
  // 0x4b10d4: ARGB from v = 512*colour: red min(512-v,255), green min(v,235), no blue
  const int v = int(shade*512.f);
  push.color[0] = float(std::min(512-v,255))/255.f;
  push.color[1] = float(std::min(v,235))/255.f;
  push.color[2] = 0;
  enc.setPushData(push);
  enc.draw(tops);
  }
