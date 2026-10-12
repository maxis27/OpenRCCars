#include "hud.h"

#include <Tempest/Log>
#include <Tempest/Pixmap>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

#include "resources.h"
#include "shader.h"

using namespace Tempest;

namespace {

// Language/<language>/msg_321_s_f.csi (512x256, the same layout in Polish and English)
constexpr float CellsY = 128;
const float Count3[4]    = {  0,     0, 128,      CellsY};
const float CountStart[4]= {128,     0, 512,      CellsY};
const float Count2[4]    = {  0,CellsY, 120,  2*CellsY};
const float Count1[4]    = {120,CellsY, 210,  2*CellsY};
const float CountFinish[4]={210,CellsY, 512,  2*CellsY};

// Settings/cockpit.ini, cockpit_time.ini (slider positions mapped to their FValues ranges), 800x600 units
constexpr float LapX       = 613,  LapY      = -20; // "lapPosX/Y": the "okrążenie" picture, top left
constexpr float TimeTotalY = 6;                     // "timeTotalPosY" (cockpit_time.ini)
constexpr float TimeLapY   = 43;                    // "timeLapPosY"
constexpr float LapBlink   = 4.2f;                  // "timeLapBlinkInSec": a finished lap's time blinks
constexpr float MessageY   = 300-142;               // "msgOffsetY" from the middle of the screen
// Settings/arrow.ini: "CX" 50, "CY" 87 (percent of the screen), "AngleVert" 45 (the arrow lies tilted)
constexpr float ArrowX     = 400,  ArrowY    = 600*0.87f;
constexpr float ArrowTilt  = 0.7071f;

constexpr float LapNumY    = 24;                    // "lapNumPosY": the lap number
constexpr float PlaceX     = 25,   PlaceY    = -20; // "placePosX/Y": place<n>.csi, 128x128
// the gauges: "indicatorAngMin" -34 and "indicatorAngMax" 127 degrees clockwise from the top are where the yellow
// scale of cockpit_sp1 and the red one of cockpit_sp2 begin and end around the dial's middle (130, 126.6 of 256).
// How the original fills it is not decoded: here the red scale covers the yellow one up to the value. The size and
// the place in the corner are measured from the original's picture of its cockpit (opt_cock_all): 256 texels are 200
// units, the dial's edge 10 units from the side and the bottom of the screen.
constexpr float GaugeAngMin = -34, GaugeAngMax = 127;
constexpr float GaugeCX     = 130, GaugeCY     = 126.6f;
constexpr float GaugeSize   = 200;
constexpr float GaugeX      = 10.f-36.f*GaugeSize/256.f, GaugeY = 590.f-219.f*GaugeSize/256.f;
constexpr float GaugeKmh    = 40;                   // the speed of a full gauge, above the fastest car's turbo
// the map: "mapPosX_0" 620, "mapPosY_0" 79 (map.ini), the size is ours
constexpr float MapX       = 620,  MapY      = 79,  MapSize = 160;

// which trackmap_<n> belongs to which track and how the world lies on it: fitted to the main checkpoints, which the
// maps mark with orange dots, and to the route drawn on them (a few texels off at most; the maps are turned freely)
struct TrackMap {
  const char* track;
  int         map;
  float       k[2];       // u+iv = (k0+i*k1)*(x+iz) + offset, texels of 512x512
  float       offset[2];
  };
const TrackMap TrackMaps[] = {
  {"beach_1",    1, {  1.5077f,  2.0881f}, {  216.138f,  307.066f}},
  {"beach_2",    6, {  1.8248f, -1.4697f}, {  250.019f,  226.633f}},
  {"beach_3",    3, { -1.9047f, -1.4350f}, {  283.909f,  179.057f}},
  {"beach_4",   10, { -1.8743f,  1.3614f}, {  234.348f,  224.769f}},
  {"country_1",  8, {  3.1648f, -0.0455f}, {  241.448f,  259.678f}},
  {"country_2",  5, {  1.0742f,  2.7172f}, {  224.194f,  248.974f}},
  {"country_3",  2, {  1.4848f, -1.8953f}, {  285.126f,  213.656f}},
  {"country_4",  9, {  0.7445f, -2.6495f}, {  265.358f,  234.597f}},
  {"urban_1",    7, {  1.5950f, -1.8126f}, {  250.418f,  196.683f}},
  {"urban_2",    4, {  0.5583f, -2.3726f}, {  261.783f,  283.176f}},
  };

const char* const Texts[2][7] = {
  // Smash fonts have no Polish letters
  {"Okr.", "Razem", "Najlepsze", "Enter - jeszcze raz", "Punkt",      "turbo", "km/h"},
  {"Lap",  "Total", "Best",      "Enter - race again",  "Checkpoint", "boost", "km/h"},
  };
// Settings/anim_cp.ini: Size 146, Shift 128, MinDist 237, MaxDist 900, BlinkDelta 10. Shift reads as texels of
// the 256 high picture: it is raised by half its height, so the shadow drawn at its bottom lies on the ground under
// the marker. The rest is read as centimetres and hundredths of a second per frame (a guess, the game's code that
// uses them is not decoded): the arrow is 1.46 m big, hidden when the camera is closer than 2.37 m, and farther
// than 9 m it keeps the size it has on the screen at 9 m (growing upwards from the ground), so that it shows the
// way from afar.
constexpr float CpArrowSize    = 1.46f;
constexpr float CpArrowShift   = 128.f/256.f;
constexpr float CpArrowMinDist = 2.37f;
constexpr float CpArrowMaxDist = 9.f;
constexpr float CpArrowFrame   = 0.1f;
constexpr float GateFlash  = 1.2f;                  // the gate just passed flashes green, and "Punkt n/m" with it
constexpr float GateRange  = 150.f;                 // gates farther away are not outlined

}

Hud::Hud(Device& device, Resources& res) : device(device), resources(res) {
  // Polish if installed, as the game itself ships it here; English otherwise
  for(const char* l:{"Polish","English"})
    if(std::filesystem::exists(res.gameDir()/"Language"/l/"msg_go.csi")) {
      lang = l;
      break;
      }
  if(lang.empty())
    Log::e("no Language/Polish or Language/English in the game folder: the race messages will be missing");
  msg321   = loadImage(res,"msg_321_s_f.csi");
  msgGo    = loadImage(res,"msg_go.csi");
  msgBest  = loadImage(res,"msg_bestlap.csi");
  msgWrong = loadImage(res,"msg_wrong_way.csi");
  msgPause = loadImage(res,"msg_pause.csi");
  lapLabel = loadImage(res,"lap.csi");
  loadFont(res,"Smash26",font);
  loadFont(res,"Smash20",fontSmall);
  for(int i=0; i<6; ++i)
    place[i] = loadImage(res,"place"+std::to_string(i+1)+".csi");
  auto tex = [&](const char* name) -> const Texture2d* {
    auto t = res.texture(name);
    if(t==nullptr)
      Log::e("no texture ",name,": a part of the cockpit will be missing");
    return t!=nullptr ? &t->tex : nullptr;
    };
  gaugeBase = tex("cockpit_sp1");
  gaugeFill = tex("cockpit_sp2");
  mapArrow  = tex("map_arrow");
  mapCp     = tex("map_cp");
  if(auto t = res.texture("coc_str_2"))
    arrow = &t->tex;
  for(int c=0; c<2; ++c)
    for(int f=0; f<3; ++f)
      if(auto t = res.texture("cp_ar_"+std::to_string(c+2)+"_f"+std::to_string(f+1)))
        cpArrow[c][f] = &t->tex;
  white = &res.white();

  auto sh   = AppShader::get("hud.vert.sprv");
  auto vert = device.shader(sh.data,sh.len);
  sh        = AppShader::get("hud.frag.sprv");
  auto frag = device.shader(sh.data,sh.len);

  RenderState st;
  st.setCullFaceMode(RenderState::CullMode::NoCull);
  st.setZTestMode(RenderState::ZTestMode::Always);
  st.setZWriteEnabled(false);
  st.setBlendSource(RenderState::BlendMode::SrcAlpha);
  st.setBlendDest  (RenderState::BlendMode::OneMinusSrcAlpha);
  pso = device.pipeline(Topology::Triangles,st,vert,frag);
  st.setZTestMode(RenderState::ZTestMode::LEqual);
  psoDepth = device.pipeline(Topology::Triangles,st,vert,frag);
  }

Hud::Image Hud::loadImage(Resources& res, std::string_view file, std::vector<uint8_t>* rgba) {
  Image ret;
  if(lang.empty())
    return ret;
  try {
    const auto data = res.readFile(std::filesystem::path("Language")/lang/std::string(file));
    const auto img  = rc::decodeCsi(data.data(),data.size());
    Pixmap pm(img.w,img.h,TextureFormat::RGBA8);
    std::memcpy(pm.data(),img.rgba.data(),img.rgba.size());
    ret.tex    = device.texture(pm,true);
    ret.w      = int(img.w);
    ret.h      = int(img.h);
    ret.loaded = true;
    if(rgba!=nullptr)
      *rgba = img.rgba;
    }
  catch(const std::exception& e) {
    Log::e("Language/",lang,"/",file,": ",e.what());
    }
  return ret;
  }

// Smash<size>.csi: 256x256, a grid of 10 columns and 9 rows (25.6 x 28.4 px) holding the characters of
// Smash<size>.dat in order (A..Z a..z 0..9 and punctuation, no Polish letters, no space). The letters are
// proportional: each glyph's width is measured from its opacity.
void Hud::loadFont(Resources& res, std::string_view name, Font& f) {
  std::vector<uint8_t> rgba;
  f.img = loadImage(res,std::string(name)+".csi",&rgba);
  if(!f.img.loaded)
    return;
  std::vector<uint8_t> chars;
  try {
    chars = res.readFile(std::filesystem::path("Language")/lang/(std::string(name)+".dat"));
    }
  catch(const std::exception& e) {
    Log::e(e.what());
    f.img.loaded = false;
    return;
    }
  while(!chars.empty() && chars.back()=='\0')
    chars.pop_back();
  const size_t rows = std::max<size_t>(1,(chars.size()+9)/10);
  const float  cw   = float(f.img.w)/10.f;
  f.cell = float(f.img.h)/float(rows);
  for(size_t i=0; i<chars.size(); ++i) {
    const uint8_t ch = chars[i];
    const int x0 = int(std::ceil(float(i%10)*cw)),     x1 = std::min(f.img.w,int(std::floor(float(i%10+1)*cw)));
    const int y0 = int(std::ceil(float(i/10)*f.cell)), y1 = std::min(f.img.h,int(std::floor(float(i/10+1)*f.cell)));
    int minX = x1, maxX = x0-1;
    for(int y=y0; y<y1; ++y)
      for(int x=x0; x<x1; ++x)
        if(rgba[(size_t(y)*size_t(f.img.w)+size_t(x))*4+3]>40) {
          minX = std::min(minX,x);
          maxX = std::max(maxX,x);
          }
    if(maxX<minX)
      continue;
    Glyph g;
    g.u0 = float(minX);
    g.u1 = float(maxX+1);
    g.v0 = float(y0);
    g.v1 = float(y1);
    f.glyphs[ch] = g;
    f.has[ch]    = true;
    }
  // a space as wide as a third of a cell, drawn as nothing
  f.glyphs[' '] = Glyph{0,cw*0.35f,0,0};
  f.has[' ']    = true;
  }

void Hud::setTrack(const std::string& name) {
  map = nullptr;
  for(auto& t:TrackMaps)
    if(name==t.track) {
      if(auto tx = resources.texture("trackmap_"+std::to_string(t.map)))
        map = &tx->tex;
      mapK      = {t.k[0],t.k[1]};
      mapOffset = {t.offset[0],t.offset[1]};
      }
  }

void Hud::event(rc::Race::Event e) {
  switch(e) {
    case rc::Race::Event::None:
      return;
    case rc::Race::Event::Go:
      message   = Message::Go;
      messageAt = clock;
      return;
    case rc::Race::Event::Checkpoint:
      break;
    case rc::Race::Event::BestLap:
      message   = Message::BestLap;
      messageAt = clock;
      lapAt     = clock;
      break;
    case rc::Race::Event::Lap:
      lapAt     = clock;
      break;
    case rc::Race::Event::Finish:
      message   = Message::Finish;
      messageAt = clock;
      lapAt     = clock;
      break;
    }
  // a main checkpoint passed (a lap ends on cp_1): its gate and "Punkt n/m" flash
  flashAt = clock;
  }

std::string Hud::formatTime(float seconds) {
  const int cs = int(std::lround(std::max(0.f,seconds)*100.f));
  char buf[32];
  std::snprintf(buf,sizeof(buf),"%d:%02d.%02d",cs/6000,(cs/100)%60,cs%100);
  return buf;
  }

void Hud::quad(const Texture2d& tex, int tw, int th, const Rect& src, float x, float y, float w, float h,
               const Color& c, float angle, float squash) {
  const float u0 = src.x0/float(tw), u1 = src.x1/float(tw);
  const float v0 = src.y0/float(th), v1 = src.y1/float(th);
  const float cx = x+w*0.5f, cy = y+h*0.5f;
  const float cs = std::cos(angle), sn = std::sin(angle);
  auto vert = [&](float dx, float dy, float u, float v) {
    // turn on the screen (positive = counter-clockwise), then flatten vertically (lying on the ground)
    const float rx =  dx*cs + dy*sn;
    const float ry = -dx*sn + dy*cs;
    Vertex vx = {{cx+rx, cy+ry*squash},{u,v},{c[0],c[1],c[2],c[3]}};
    return vx;
    };
  const float hw = w*0.5f, hh = h*0.5f;
  const Vertex a = vert(-hw,-hh,u0,v0), b = vert(hw,-hh,u1,v0), d = vert(hw,hh,u1,v1), e = vert(-hw,hh,u0,v1);
  if(draws.empty() || draws.back().tex!=&tex || draws.back().depth)
    draws.push_back({&tex,verts.size(),0,false});
  for(auto& v:{a,b,d,a,d,e})
    verts.push_back(v);
  draws.back().count += 6;
  }

void Hud::poly(const Texture2d& tex, const std::array<std::array<float,2>,4>& p, const Color& c) {
  if(draws.empty() || draws.back().tex!=&tex || draws.back().depth)
    draws.push_back({&tex,verts.size(),0,false});
  for(int i:{0,1,2,0,2,3})
    verts.push_back(Vertex{{p[i][0],p[i][1]},{0.5f,0.5f},{c[0],c[1],c[2],c[3]}});
  draws.back().count += 6;
  }

void Hud::line3d(const rc::Mat4& vp, const rc::Vec3& a, const rc::Vec3& b, float width, const Color& c) {
  // clip space: x y z from Mat4::point, w is the 4th row (the distance along the view direction)
  auto clip = [&](const rc::Vec3& p, float& w) {
    w = vp.m[3][0]*p.x+vp.m[3][1]*p.y+vp.m[3][2]*p.z+vp.m[3][3];
    return vp.point(p);
    };
  float          wa, wb;
  rc::Vec3       ca = clip(a,wa), cb = clip(b,wb);
  constexpr float Near = 0.1f;
  if(wa<Near && wb<Near)
    return;
  // cut the part behind the camera
  if(wa<Near || wb<Near) {
    const float t = (Near-wa)/(wb-wa);
    const rc::Vec3 cm = ca+(cb-ca)*t;
    if(wa<Near) { ca = cm; wa = Near; } else { cb = cm; wb = Near; }
    }
  const float x0 = (ca.x/wa*0.5f+0.5f)*float(scrW), y0 = (ca.y/wa*0.5f+0.5f)*float(scrH);
  const float x1 = (cb.x/wb*0.5f+0.5f)*float(scrW), y1 = (cb.y/wb*0.5f+0.5f)*float(scrH);
  float nx = y0-y1, ny = x1-x0;
  const float len = std::sqrt(nx*nx+ny*ny);
  if(len<1e-3f)
    return;
  nx *= width*0.5f/len;
  ny *= width*0.5f/len;
  poly(*white,{{{x0+nx,y0+ny},{x1+nx,y1+ny},{x1-nx,y1-ny},{x0-nx,y0-ny}}},c);
  }

void Hud::gates(const rc::Race& race, const rc::Mat4& vp) {
  const auto& cps   = race.checkpoints();
  const auto& route = cps.route();
  if(route.size()<2)
    return;
  const size_t n     = route.size()-1;     // the last point is cp_1 again
  const size_t next  = cps.next();
  const size_t after = next==n ? cps.nextMain(0) : cps.nextMain(next);
  for(size_t k=0; k<n; ++k) {
    const size_t i = k==0 ? n : k;           // route index; cp_1 is reached as route[n]
    const auto&  p = route[i];
    Color c;
    float wdt = 2.f*scale;
    if(!p.main) {
      // a sub-checkpoint: only the one the arrow shows, thin
      if(i!=cps.guide() || i==next)
        continue;
      c = {0.3f,0.85f,1.f,0.8f};
      }
    else if(i==(cps.lastPassed()==0 ? n : cps.lastPassed()) && clock-flashAt<GateFlash) {
      const float f = 1.f-(clock-flashAt)/GateFlash;
      c   = {0.3f,1.f,0.3f,0.4f+0.6f*f};
      wdt = (3.f+6.f*f)*scale;
      }
    else if(i==next) {
      c   = {0.2f,1.f,0.3f,0.95f};
      wdt = 4.f*scale;
      }
    else if(i==after)
      c = {1.f,0.85f,0.2f,0.8f};
    else if(i<next && race.phase()!=rc::Race::Phase::Countdown)
      c = {0.6f,0.6f,0.6f,0.35f};            // passed in this lap
    else
      c = {1.f,1.f,1.f,0.45f};
    // skip the far ones (clip w is the distance along the view direction)
    const float w = vp.m[3][0]*p.position.x+vp.m[3][1]*p.position.y+vp.m[3][2]*p.position.z+vp.m[3][3];
    if(w>GateRange || w<-p.width)
      continue;
    const rc::Vec3 side = rc::Vec3{-p.dir.z,0,p.dir.x}*(p.width*0.5f);
    const float    top  = p.position.y+p.height+0.5f;
    const rc::Vec3 l0 = {p.position.x-side.x,p.bottom,p.position.z-side.z};
    const rc::Vec3 r0 = {p.position.x+side.x,p.bottom,p.position.z+side.z};
    const rc::Vec3 l1 = {l0.x,top,l0.z}, r1 = {r0.x,top,r0.z};
    line3d(vp,l0,l1,wdt,c);
    line3d(vp,r0,r1,wdt,c);
    line3d(vp,l1,r1,wdt,c);
    line3d(vp,l0,r0,wdt,c);
    }
  }

void Hud::checkpointArrow(const rc::Race& race, const rc::Mat4& vp) {
  const auto& route = race.checkpoints().route();
  if(route.size()<2 || race.phase()==rc::Race::Phase::Finished)
    return;
  // red over cp_1, where the lap ends; green over the other main checkpoints. The frames go 1 2 3 2 1 ..
  const size_t   next   = race.checkpoints().next();
  const int      colour = next+1==route.size() ? 1 : 0;
  const int      step   = int(clock/CpArrowFrame)%4;
  const auto*    tex    = cpArrow[colour][step==3 ? 1 : step];
  if(tex==nullptr)
    return;
  const auto&    p  = route[next];
  const rc::Vec3 foot = {p.position.x,p.ground,p.position.z};
  float depth = 0;
  auto project = [&](const rc::Vec3& v, float& sx, float& sy) {
    const float w = vp.m[3][0]*v.x+vp.m[3][1]*v.y+vp.m[3][2]*v.z+vp.m[3][3];
    const rc::Vec3 c = vp.point(v);
    sx    = (c.x/w*0.5f+0.5f)*float(scrW);
    sy    = (c.y/w*0.5f+0.5f)*float(scrH);
    depth = c.z/w;
    return w;
    };
  float x0, y0, x1, y1;
  const float dist = project(foot,x1,y1);
  if(dist<CpArrowMinDist)
    return;
  // the size: kept from growing smaller on the screen beyond MaxDist
  const float    size = CpArrowSize*std::max(1.f,dist/CpArrowMaxDist);
  const rc::Vec3 at   = foot+rc::Vec3{0,size*CpArrowShift,0};
  // a flat picture facing the camera at the arrow's distance, brought half a metre nearer so that the ground right
  // under it does not cut off its shadow: terrain and objects in front of it hide it. The 4th row of the matrix is
  // the camera's forward direction.
  const rc::Vec3 fwd = {vp.m[3][0],vp.m[3][1],vp.m[3][2]};
  float sx, sy;
  project(at-fwd*0.5f,sx,sy);
  const float z = depth;
  // its height on the screen from the foot to the top, the width the same
  project(foot+rc::Vec3{0,size,0},x0,y0);
  const float h     = std::abs(y1-y0);
  const float x     = x1;
  const float alpha = std::min(1.f,dist-CpArrowMinDist);   // fades in over the first metre
  quadAt(*tex,x-h*0.5f,std::min(y0,y1),h,h,z,Color{1,1,1,alpha});
  }

void Hud::gauge(bool right, float value, const std::string& number, std::string_view label) {
  if(gaugeBase==nullptr || gaugeFill==nullptr)
    return;
  const float k  = GaugeSize/256.f*scale;
  const float x0 = right ? float(scrW)-(GaugeX+GaugeSize)*scale : px(GaugeX);
  const float y0 = py(GaugeY);
  // texels -> the screen, mirrored on the right
  auto at = [&](float u, float v) {
    return std::array<float,2>{x0+(right ? 256.f-u : u)*k, y0+v*k};
    };
  const Color white4 = {1,1,1,1};
  auto tri = [&](const Texture2d& tex, const std::array<std::array<float,2>,3>& t) {
    if(draws.empty() || draws.back().tex!=&tex || draws.back().depth)
      draws.push_back({&tex,verts.size(),0,false});
    for(auto& c:t) {
      const auto p = at(c[0],c[1]);
      verts.push_back(Vertex{{p[0],p[1]},{c[0]/256.f,c[1]/256.f},{1,1,1,1}});
      }
    draws.back().count += 3;
    };
  const Rect src = right ? Rect{256,0,0,256} : Rect{0,0,256,256};
  quad(*gaugeBase,256,256,src,x0,y0,256.f*k,256.f*k,white4);
  // the red scale up to the value: a fan of thin slices around the dial's middle, wide enough for the whole scale
  // (it starts a little before the scale's beginning, to take its rounded end)
  const float end = GaugeAngMin+(GaugeAngMax-GaugeAngMin)*std::clamp(value,0.f,1.f);
  constexpr float R = 110, Deg = 3.14159265f/180.f;
  for(float a0=GaugeAngMin-6.f; value>0.005f && a0<end; a0+=4.f) {
    const float a1 = std::min(a0+4.f,end);
    tri(*gaugeFill,{{{GaugeCX,GaugeCY},
                     {GaugeCX+R*std::sin(a0*Deg),GaugeCY-R*std::cos(a0*Deg)},
                     {GaugeCX+R*std::sin(a1*Deg),GaugeCY-R*std::cos(a1*Deg)}}});
    }
  // the number over a line, the unit under it, in the dial's middle
  const auto c = at(GaugeCX,GaugeCY);
  text(font,number,c[0],c[1]-34.f*scale,32,white4,0);
  quad(*white,1,1,{0,0,1,1},c[0]-30.f*scale,c[1]+1.f*scale,60.f*scale,2.f*scale,Color{1,1,1,0.85f});
  text(fontSmall,label,c[0],c[1]+7.f*scale,20,white4,0);
  }

void Hud::trackMap(const rc::Race& race, const Info& info) {
  const auto& route = race.checkpoints().route();
  if(map==nullptr || route.empty())
    return;
  auto toMap = [&](const rc::Vec3& p) {
    return std::array<float,2>{mapK[0]*p.x-mapK[1]*p.z+mapOffset[0], mapK[1]*p.x+mapK[0]*p.z+mapOffset[1]};
    };
  // the square of the map around the route, with a margin
  float u0 = 512, v0 = 512, u1 = 0, v1 = 0;
  for(auto& p:route) {
    const auto m = toMap(p.position);
    u0 = std::min(u0,m[0]); u1 = std::max(u1,m[0]);
    v0 = std::min(v0,m[1]); v1 = std::max(v1,m[1]);
    }
  const float side = std::min(512.f,std::max(u1-u0,v1-v0)+48.f);
  const float cu   = std::clamp((u0+u1)*0.5f,side*0.5f,512.f-side*0.5f);
  const float cv   = std::clamp((v0+v1)*0.5f,side*0.5f,512.f-side*0.5f);
  const Rect  crop = {cu-side*0.5f,cv-side*0.5f,cu+side*0.5f,cv+side*0.5f};

  const float x = px(MapX,true), y = py(MapY), size = MapSize*scale;
  auto toScreen = [&](const rc::Vec3& p) {
    const auto m = toMap(p);
    return std::array<float,2>{x+(m[0]-crop.x0)/side*size, y+(m[1]-crop.y0)/side*size};
    };
  const float b = 2.f*scale;
  // a light frame, the sea of the map is see-through: a dark backing
  quad(*white,1,1,{0,0,1,1},x-b,y-b,size+2*b,size+2*b,Color{0.85f,0.85f,0.85f,0.9f});
  quad(*white,1,1,{0,0,1,1},x,y,size,size,Color{0.05f,0.06f,0.12f,0.85f});
  quad(*map,512,512,crop,x,y,size,size,Color{1,1,1,1});

  // the main checkpoints, the next one glowing
  const float dot = 11.f*scale;
  if(mapCp!=nullptr) {
    const size_t next = race.checkpoints().next();
    for(size_t i=0; i+1<route.size(); ++i) {
      if(!route[i].main)
        continue;
      const auto s     = toScreen(route[i].position);
      const bool isNext = (i==0 ? route.size()-1 : i)==next && race.phase()!=rc::Race::Phase::Finished;
      if(isNext) {
        const float g = dot*(2.2f+0.4f*std::sin(clock*6.f));
        quad(*mapCp,64,32,{32,0,64,32},s[0]-g*0.5f,s[1]-g*0.5f,g,g,Color{1,1,1,0.9f});
        }
      quad(*mapCp,64,32,{0,0,32,32},s[0]-dot*0.5f,s[1]-dot*0.5f,dot,dot,Color{1,1,1,1});
      }
    }
  // the cars: arrows pointing where each one heads
  if(mapArrow!=nullptr) {
    const float sz = 20.f*scale;
    auto car = [&](const rc::Vec3& pos, const rc::Vec3& fwd, float u) {
      const auto s = toScreen(pos);
      const auto f = toScreen(pos+fwd);
      const float dx = f[0]-s[0], dy = f[1]-s[1];
      if(std::abs(dx)+std::abs(dy)<1e-4f)
        return;
      // quad() turns counter-clockwise: its "up" (0,-1) goes to (dx,dy)
      quad(*mapArrow,64,32,{u,0,u+32,32},s[0]-sz*0.5f,s[1]-sz*0.5f,sz,sz,Color{1,1,1,1},std::atan2(-dx,-dy));
      };
    for(auto& o:info.opponents)
      car(o.pos,o.forward,32);
    car(info.carPos,info.carForward,0);
    }
  }

void Hud::quadAt(const Texture2d& tex, float x, float y, float w, float h, float depth, const Color& c) {
  if(draws.empty() || draws.back().tex!=&tex || !draws.back().depth)
    draws.push_back({&tex,verts.size(),0,true});
  const Vertex a = {{x,  y  },{0,0},{c[0],c[1],c[2],c[3]},depth};
  const Vertex b = {{x+w,y  },{1,0},{c[0],c[1],c[2],c[3]},depth};
  const Vertex d = {{x+w,y+h},{1,1},{c[0],c[1],c[2],c[3]},depth};
  const Vertex e = {{x,  y+h},{0,1},{c[0],c[1],c[2],c[3]},depth};
  for(auto& v:{a,b,d,a,d,e})
    verts.push_back(v);
  draws.back().count += 6;
  }

void Hud::sprite(const Image& img, const Rect& src, float cx, float cy, float size, const Color& c) {
  if(!img.loaded)
    return;
  const float w = (src.x1-src.x0)*size*scale, h = (src.y1-src.y0)*size*scale;
  quad(img.tex,img.w,img.h,src,cx-w*0.5f,cy-h*0.5f,w,h,c);
  }

float Hud::textWidth(const Font& f, std::string_view s, float size) const {
  if(!f.img.loaded)
    return 0;
  const float k = size*scale/f.cell;
  float w = 0;
  for(char ch:s) {
    const uint8_t c = uint8_t(ch);
    if(f.has[c])
      w += (f.glyphs[c].u1-f.glyphs[c].u0+2.f)*k;
    }
  return w;
  }

float Hud::text(const Font& f, std::string_view s, float x, float y, float size, const Color& c, int align) {
  if(!f.img.loaded)
    return x;
  const float k = size*scale/f.cell;
  const float w = textWidth(f,s,size);
  if(align==0) x -= w*0.5f;
  if(align>0)  x -= w;
  for(char ch:s) {
    const uint8_t cc = uint8_t(ch);
    if(!f.has[cc])
      continue;
    const Glyph& g = f.glyphs[cc];
    if(g.v1>g.v0)
      quad(f.img.tex,f.img.w,f.img.h,{g.u0,g.v0,g.u1,g.v1},x,y,(g.u1-g.u0)*k,(g.v1-g.v0)*k,c);
    x += (g.u1-g.u0+2.f)*k;
    }
  return x;
  }

void Hud::draw(Encoder<CommandBuffer>& enc, uint8_t frame, int w, int h, const rc::Race& race, const Info& info) {
  scrW    = w;
  scrH    = h;
  scale   = float(h)/600.f;
  offsetX = (float(w)-800.f*scale)*0.5f;
  verts.clear();
  draws.clear();

  const Color white4 = {1,1,1,1};
  const Color yellow = {1.f,0.85f,0.2f,1};
  const bool  blink  = std::fmod(clock,0.5f)<0.3f;
  const auto  phase  = race.phase();
  const int   li     = (lang=="Polish") ? 0 : 1;

  const auto&  cps  = race.checkpoints();
  const size_t n    = cps.mainCount();
  if(info.gates)
    gates(race,info.viewProj);
  checkpointArrow(race,info.viewProj);

  // top right: "okrążenie" and the lap number
  if(lapLabel.loaded) {
    const float x = px(LapX,true), y = py(LapY);
    quad(lapLabel.tex,lapLabel.w,lapLabel.h,{0,0,float(lapLabel.w),float(lapLabel.h)},x,y,
         float(lapLabel.w)*scale,float(lapLabel.h)*scale,white4);
    // after the picture's lettering (x 6..120, y 39..84 of 128): the Polish one is wider than the English one, which
    // "lapNumPosX" 725 is set for
    const std::string lap = std::to_string(race.lap())+"/"+std::to_string(race.laps());
    text(font,lap,x+130.f*scale,py(LapNumY),34,white4);
    }
  // top left: the place, when there are opponents
  if(info.place>=1 && info.place<=6 && place[info.place-1].loaded) {
    const Image& im = place[info.place-1];
    quad(im.tex,im.w,im.h,{0,0,float(im.w),float(im.h)},px(PlaceX),py(PlaceY),float(im.w)*scale,float(im.h)*scale,
         white4);
    }
  // under it the map
  trackMap(race,info);
  // under the map: the checkpoints passed in this lap, green for a moment after each one
  if(n>0) {
    const bool        lit    = clock-flashAt<GateFlash;
    const Color       green  = {0.3f,1.f,0.35f,1};
    const size_t      passed = phase==rc::Race::Phase::Finished ? n : cps.mainPassed();
    const std::string s      = std::string(Texts[li][4])+" "+std::to_string(passed)+"/"+std::to_string(n);
    text(font,s,float(w)-15.f*scale,py(MapY+MapSize+8),26,lit ? green : white4,1);
    if(lit && phase==rc::Race::Phase::Racing) {
      const float f = 1.f-(clock-flashAt)/GateFlash;
      const size_t      k   = passed==0 ? n : passed;  // cp_1 again: the lap's last
      const std::string got = std::string(Texts[li][4])+" "+std::to_string(k)+"/"+std::to_string(n);
      text(font,got,px(400),py(MessageY+70),30+10*f,Color{green[0],green[1],green[2],std::min(1.f,2.f*f)},0);
      }
    }

  // top middle: the total time, below it the lap time (a finished lap's time blinks for a while)
  text(font,formatTime(race.time()),px(400),py(TimeTotalY),34,white4,0);
  if(clock-lapAt<LapBlink && !race.lapTimes().empty()) {
    if(blink) {
      const bool best = race.lapTimes().back()<=race.bestLap();
      text(font,formatTime(race.lapTimes().back()),px(400),py(TimeLapY),26,best ? yellow : white4,0);
      }
    }
  else if(phase==rc::Race::Phase::Racing) {
    text(font,formatTime(race.lapTime()),px(400),py(TimeLapY),26,white4,0);
    }

  // bottom: turbo on the left, the speed on the right
  gauge(false,info.turbo,std::to_string(int(std::lround(info.turbo*100.f))),Texts[li][5]);
  gauge(true,info.speed/GaugeKmh,std::to_string(int(std::lround(info.speed))),Texts[li][6]);

  // the arrow towards the next checkpoint: red and blinking on the wrong way
  if(arrow!=nullptr && phase!=rc::Race::Phase::Finished) {
    const bool  wrong = race.wrongWay();
    const Color c     = wrong ? Color{1.f,0.15f,0.1f,blink ? 1.f : 0.35f} : Color{1,1,1,0.9f};
    const float size  = 96.f*scale;
    quad(*arrow,128,128,{0,0,128,128},px(ArrowX)-size*0.5f,py(ArrowY)-size*0.5f,size,size,c,info.direction,ArrowTilt);
    }

  // one message in the middle at a time
  const float my = py(MessageY);
  if(info.paused) {
    sprite(msgPause,{0,0,float(msgPause.w),float(msgPause.h)},px(400),my,1,white4);
    }
  else if(phase==rc::Race::Phase::Countdown) {
    const float  left = race.countdown();
    const float* r    = left>2.f ? Count3 : left>1.f ? Count2 : Count1;
    // each number pops up and shrinks a little during its second
    const float  t    = left-std::floor(left);
    sprite(msg321,{r[0],r[1],r[2],r[3]},px(400),my,0.9f+0.4f*t,white4);
    }
  else if(message==Message::Go && clock-messageAt<1.2f) {
    sprite(msg321,{CountStart[0],CountStart[1],CountStart[2],CountStart[3]},px(400),my,1,white4);
    }
  else if(phase==rc::Race::Phase::Finished) {
    sprite(msg321,{CountFinish[0],CountFinish[1],CountFinish[2],CountFinish[3]},px(400),my,1,white4);
    }
  else if(race.wrongWay()) {
    if(blink)
      sprite(msgWrong,{0,0,float(msgWrong.w),float(msgWrong.h)},px(400),my,1,white4);
    }
  else if(message==Message::BestLap && clock-messageAt<2.5f) {
    sprite(msgBest,{0,0,float(msgBest.w),float(msgBest.h)},px(400),my,1,white4);
    }

  // after the finish: the lap times
  if(phase==rc::Race::Phase::Finished) {
    const auto& times = race.lapTimes();
    const float line  = 34, x0 = px(250), x1 = px(550);
    float       y     = 230;
    const float rows  = float(times.size()+3);
    quad(*white,1,1,{0,0,1,1},px(230),py(y-12),px(570)-px(230),py(rows*line+24),Color{0,0,0,0.45f});
    for(size_t i=0; i<times.size(); ++i, y+=line) {
      const Color c = times[i]<=race.bestLap() ? yellow : white4;
      text(font,std::string(Texts[li][0])+" "+std::to_string(i+1),x0,py(y),28,c,-1);
      text(font,formatTime(times[i]),x1,py(y),28,c,1);
      }
    text(font,Texts[li][1],x0,py(y),28,white4,-1);
    text(font,formatTime(race.time()),x1,py(y),28,white4,1);
    y += line;
    text(font,Texts[li][2],x0,py(y),28,yellow,-1);
    text(font,formatTime(race.bestLap()),x1,py(y),28,yellow,1);
    y += line*1.3f;
    text(font,Texts[li][3],px(400),py(y),24,white4,0);
    }

  if(verts.empty())
    return;
  vbo[frame%2] = device.vbo(BufferHeap::Upload,verts);
  struct Push {
    float screen[2];
    } push = {{float(w),float(h)}};
  for(auto& d:draws) {
    enc.setPipeline(d.depth ? psoDepth : pso);
    enc.setPushData(push);
    enc.setBinding(0,*d.tex,Sampler::bilinear(ClampMode::ClampToEdge));
    enc.draw(vbo[frame%2],d.first,d.count);
    }
  }
