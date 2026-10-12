#include "hud.h"

#include <Tempest/Log>
#include <Tempest/Pixmap>

#include <algorithm>
#include <numbers>
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
constexpr float TimeSize   = 50;                    // the time and the lap number: digits 31 px high in the original
constexpr float TimeY      = -2;                    // digits 7 px from the top as in the original ("timeTotalPosY" 6)
constexpr float TimeLapY   = 43;                    // "timeLapPosY"
constexpr float LapBlink   = 4.2f;                  // "timeLapBlinkInSec": a finished lap's time blinks
constexpr float MessageY   = 300-142;               // "msgOffsetY" from the middle of the screen

constexpr float LapNumY    = 16;                    // the lap number's top ("lapNumPosY" reads 24)
constexpr float LapNumDX   = 115;                   // from the "okrążenie" picture's left edge
constexpr float PlaceX     = 25,   PlaceY    = -20; // "placePosX/Y": place<n>.csi, 128x128
// the gauges: "indicatorAngMin" -34 and "indicatorAngMax" 127 degrees clockwise from the top are where the tube of
// cockpit_sp1 (yellow) and cockpit_sp2 (red; its grey tube lies at 140..300) begins and ends around the dial's middle
// (130, 126.6 of 256, radius 87). As in the original (Mateusz's screenshot): the tube fills from its lower end (127,
// zero) towards the upper one (-34, full) in red; above the value it stays yellow up to what the car can reach (its
// turbo tank, its top speed), with the dark cap of cockpit_sp4 there, and grey beyond it. The silver clamp of
// cockpit_sp4 sits on the value. The size and the place are measured from a screenshot of the original at 800x600:
// the dial's brown plate is 107 units across with its middle at (94, 497), the number 30 units high.
constexpr float GaugeAngMin = -34, GaugeAngMax = 127;
constexpr float GaugeCX     = 130, GaugeCY     = 126.6f, GaugeR = 86;
// the tube's own middle (its edges fitted in cockpit_sp1/sp2: radius 79.6..92.6), and that of cockpit_sp2's grey tube,
// which is turned onto the scale
constexpr float TubeCX      = 128.5f, TubeCY = 128.5f;
constexpr float GreyCX      = 127.5f, GreyCY = 127.5f, GaugeGreyAt = 184;
constexpr float GaugeSize   = 245;
constexpr float GaugeMidX   = 94,  GaugeMidY = 497;  // the dial's middle, from the left (right) and the top
constexpr float GaugeX      = GaugeMidX-GaugeCX*GaugeSize/256.f, GaugeY = GaugeMidY-GaugeCY*GaugeSize/256.f;
// a full speed gauge, km/h: in the screenshot the fully upgraded Overkill (turbo tank 82, 42.7 km/h with turbo)
// reaches 0.85 of it
constexpr float GaugeKmh    = 50;
// cockpit_sp4: the cap (where the car's range ends) and the clamp (on the value), texels, and their size on the
// dial (texels of cockpit_sp1) along and across the tube, as in the screenshot: the picture's x goes along the tube
const float GaugeCap[4]   = { 29,69, 75, 95};
const float GaugeClamp[4] = {149,55,227,111};
constexpr float GaugeCapSize[2]   = { 5,19};
constexpr float GaugeClampSize[2] = {10,27};
// Settings/cockpit.ini "overtakingLeftPosX/Y" (400, 530) and "overtakingBRectOffsetX" -57: overtaking.csi's bracket
// (x 50..86, y 14..116 of 128) on both sides of the direction arrow; its red mark (x 14..27) shows a car behind
const float Bracket[4]    = { 50,14, 86,116};
constexpr float BracketX    = 400, BracketY  = 530, BracketDX = 57, BracketScale = 0.9f;
// the map: "mapPosX_0" 620, "mapPosY_0" 79 (map.ini), the size is ours
constexpr float MapX       = 620,  MapY      = 79,  MapSize = 165;
// the map is shown around the car: 140 of its 512 texels (about 50 m) inside a 9 unit frame, as in the original
constexpr float MapView    = 140,  MapInset  = 10,  MapCorner = 30;

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
  {"Okr.", "Razem", "Najlepsze", "Enter - jeszcze raz", "dopalacz", "km/h", "Ty"},
  {"Lap",  "Total", "Best",      "Enter - race again",  "boost", "km/h", "You"},
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
constexpr float GateFlash  = 1.2f;                  // the checkpoint just passed flashes green
constexpr float GateRange  = 150.f;                 // checkpoints farther away are not outlined

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
  msgHits  = loadImage(res,"msg_hits.csi");
  lapLabel = loadImage(res,"lap.csi");
  loadFont(res,"Smash26",font);
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
  gaugePieces = tex("cockpit_sp4");
  overtaking  = tex("overtaking");
  panel       = tex("messagebox");
  mapArrow  = tex("map_arrow");
  mapCp     = tex("map_cp");
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

// Smash<size>.csi: 256x256, a grid of 10 columns 25.6 px wide and rows 28 px high (the baselines step by exactly
// 28 px, the last 4 px are unused) holding the characters of Smash<size>.dat in order (A..Z a..z 0..9 and
// punctuation, no space). The letters are proportional: each glyph's width is measured from its opacity, and the
// game sets them side by side with no gap (the 1 px shadow on their right separates them).
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
  f.cell = std::floor(float(f.img.h)/float(rows));
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
    case rc::Race::Event::Count:
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
  // a main checkpoint passed (a lap ends on cp_1): its sphere flashes
  flashAt = clock;
  }

std::string Hud::formatTime(float seconds) {
  const int cs = int(std::lround(std::max(0.f,seconds)*100.f));
  char buf[32];
  std::snprintf(buf,sizeof(buf),"%d:%02d.%02d",cs/6000,(cs/100)%60,cs%100);
  return buf;
  }

void Hud::quad(const Texture2d& tex, int tw, int th, const Rect& src, float x, float y, float w, float h,
               const Color& c, float angle, float squash, const Color* bottom) {
  const float u0 = src.x0/float(tw), u1 = src.x1/float(tw);
  const float v0 = src.y0/float(th), v1 = src.y1/float(th);
  const float cx = x+w*0.5f, cy = y+h*0.5f;
  const float cs = std::cos(angle), sn = std::sin(angle);
  auto vert = [&](float dx, float dy, float u, float v) {
    // turn on the screen (positive = counter-clockwise), then flatten vertically (lying on the ground)
    const float rx =  dx*cs + dy*sn;
    const float ry = -dx*sn + dy*cs;
    const Color& col = (bottom!=nullptr && dy>0) ? *bottom : c;
    Vertex vx = {{cx+rx, cy+ry*squash},{u,v},{col[0],col[1],col[2],col[3]}};
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
    if(w>GateRange || w<-rc::Checkpoints::PassRadius)
      continue;
    // the sphere the car has to touch, as three circles: flat, along the route and across it
    const float    r     = rc::Checkpoints::PassRadius;
    const rc::Vec3 along = p.dir, side = {-p.dir.z,0,p.dir.x}, up = {0,1,0};
    const rc::Vec3 axes[3][2] = {{along,side},{along,up},{side,up}};
    constexpr int  Segments = 24;
    for(auto& ax:axes)
      for(int k=0; k<Segments; ++k) {
        const float a0 = float(k)*6.2831853f/Segments, a1 = float(k+1)*6.2831853f/Segments;
        line3d(vp,p.position+(ax[0]*std::cos(a0)+ax[1]*std::sin(a0))*r,
                  p.position+(ax[0]*std::cos(a1)+ax[1]*std::sin(a1))*r,wdt,c);
        }
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

void Hud::gauge(bool right, float value, float reach, const std::string& number, std::string_view label) {
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
  constexpr float Deg = 3.14159265f/180.f;
  // a part of the tube from angle a0 to a1, cut out of `tex` turned by `turn` degrees: a fan of thin slices
  // (the texture's tube around (scx, scy))
  auto tube = [&](const Texture2d& tex, float a0, float a1, float turn, float scx, float scy) {
    constexpr float R0 = 72, R1 = 100;
    for(float s0=a0; s0<a1; s0+=2.f) {
      const float s1 = std::min(s0+2.f,a1);
      const std::array<float,2> pt[4] = {
        {TubeCX+R0*std::sin(s0*Deg),TubeCY-R0*std::cos(s0*Deg)}, {TubeCX+R1*std::sin(s0*Deg),TubeCY-R1*std::cos(s0*Deg)},
        {TubeCX+R1*std::sin(s1*Deg),TubeCY-R1*std::cos(s1*Deg)}, {TubeCX+R0*std::sin(s1*Deg),TubeCY-R0*std::cos(s1*Deg)},
        };
      const float t0 = (s0+turn)*Deg, t1 = (s1+turn)*Deg;
      const std::array<float,2> uv[4] = {
        {scx+R0*std::sin(t0),scy-R0*std::cos(t0)}, {scx+R1*std::sin(t0),scy-R1*std::cos(t0)},
        {scx+R1*std::sin(t1),scy-R1*std::cos(t1)}, {scx+R0*std::sin(t1),scy-R0*std::cos(t1)},
        };
      if(draws.empty() || draws.back().tex!=&tex || draws.back().depth)
        draws.push_back({&tex,verts.size(),0,false});
      for(int i:{0,1,2,0,2,3}) {
        const auto p = at(pt[i][0],pt[i][1]);
        verts.push_back(Vertex{{p[0],p[1]},{uv[i][0]/256.f,uv[i][1]/256.f},{1,1,1,1}});
        }
      draws.back().count += 6;
      }
    };
  // a piece of cockpit_sp4 on the tube at angle `a`
  auto piece = [&](const float* r, const float* size, float a) {
    if(gaugePieces==nullptr)
      return;
    const auto  c  = at(TubeCX+GaugeR*std::sin(a*Deg),TubeCY-GaugeR*std::cos(a*Deg));
    const float w  = size[0]*k, h = size[1]*k;
    // the picture's x along the tube: level at the top, turned with the angle
    const float turn = right ? a*Deg : -a*Deg;
    quad(*gaugePieces,256,256,{r[0],r[1],r[2],r[3]},c[0]-w*0.5f,c[1]-h*0.5f,w,h,white4,turn);
    };

  const Rect src = right ? Rect{256,0,0,256} : Rect{0,0,256,256};
  quad(*gaugeBase,256,256,src,x0,y0,256.f*k,256.f*k,white4);
  const float span = GaugeAngMax-GaugeAngMin;
  value = std::clamp(value,0.f,1.f);
  reach = std::clamp(reach,value,1.f);
  const float valueAt = GaugeAngMax-span*value;
  const float reachAt = GaugeAngMax-span*reach;
  // red up to the value (with the tube's rounded lower end), grey beyond the reach (with its upper end)
  if(value>0.f)
    tube(*gaugeFill,valueAt,GaugeAngMax+8.f,0,TubeCX,TubeCY);
  if(reach<1.f)
    tube(*gaugeFill,GaugeAngMin-10.f,reachAt,GaugeGreyAt,GreyCX,GreyCY);
  if(reach<1.f)
    piece(GaugeCap,GaugeCapSize,reachAt);
  piece(GaugeClamp,GaugeClampSize,valueAt);

  // the number over a line, the unit under it, in the dial's middle
  const auto c = at(GaugeCX,GaugeCY);
  metalText(font,number,c[0],c[1]-44.f*scale,53,0);
  quad(*white,1,1,{0,0,1,1},c[0]-46.f*scale,c[1]+1.f*scale,92.f*scale,2.5f*scale,Color{1,1,1,0.9f});
  text(font,label,c[0],c[1]+8.f*scale,28,white4,0);
  }

void Hud::trackMap(const rc::Race& race, const Info& info) {
  if(map==nullptr)
    return;
  const auto& route = race.checkpoints().route();
  auto toMap = [&](const rc::Vec3& p) {
    return std::array<float,2>{mapK[0]*p.x-mapK[1]*p.z+mapOffset[0], mapK[1]*p.x+mapK[0]*p.z+mapOffset[1]};
    };
  // the frame: messagebox.csi's box (x 0..128 of 256, open on the right) and the same mirrored
  const float x = px(MapX,true), y = py(MapY), size = MapSize*scale;
  if(panel!=nullptr) {
    quad(*panel,256,128,{0,0,64,128},x,y,size*0.5f,size,Color{1,1,1,1});
    quad(*panel,256,128,{64,0,0,128},x+size*0.5f,y,size*0.5f,size,Color{1,1,1,1});
    }
  // inside it the map around the car, north of the map up
  const float in   = MapInset*scale;
  const float view = MapView;
  const auto  car  = toMap(info.carPos);
  const float cu   = std::clamp(car[0],view*0.5f,512.f-view*0.5f);
  const float cv   = std::clamp(car[1],view*0.5f,512.f-view*0.5f);
  const Rect  crop = {cu-view*0.5f,cv-view*0.5f,cu+view*0.5f,cv+view*0.5f};
  const float inner = size-2*in;
  // cut to the frame's inside: a square with rounded corners (messagebox: straight edges 8 texels in from the
  // outside, corners of radius 23 texels, in a 128 high picture)
  const float round = MapCorner*scale;
  auto inside = [&](float sx, float sy) {
    const float dx = std::max(0.f,std::abs(sx-(x+size*0.5f))-(inner*0.5f-round));
    const float dy = std::max(0.f,std::abs(sy-(y+size*0.5f))-(inner*0.5f-round));
    return std::abs(sx-(x+size*0.5f))<=inner*0.5f && std::abs(sy-(y+size*0.5f))<=inner*0.5f && dx*dx+dy*dy<=round*round;
    };
  {
    // a fan from the middle around the outline, 8 steps on each corner
    std::vector<std::array<float,2>> outline;
    const float c0 = in+round, c1 = size-in-round;
    const std::array<std::array<float,3>,4> corners = {{{c1,c0,-0.5f},{c1,c1,0.f},{c0,c1,0.5f},{c0,c0,1.f}}};
    for(auto& k:corners)
      for(int i=0; i<=8; ++i) {
        const float a = (k[2]+float(i)/16.f)*std::numbers::pi_v<float>;
        outline.push_back({x+k[0]+std::cos(a)*round, y+k[1]+std::sin(a)*round});
        }
    auto vert = [&](float sx, float sy) {
      const float u = crop.x0+(sx-x-in)/inner*view, v = crop.y0+(sy-y-in)/inner*view;
      return Vertex{{sx,sy},{u/512.f,v/512.f},{1,1,1,1}};
      };
    if(draws.empty() || draws.back().tex!=map || draws.back().depth)
      draws.push_back({map,verts.size(),0,false});
    const Vertex mid = vert(x+size*0.5f,y+size*0.5f);
    for(size_t i=0; i<outline.size(); ++i) {
      const auto& a = outline[i];
      const auto& b = outline[(i+1)%outline.size()];
      verts.push_back(mid);
      verts.push_back(vert(a[0],a[1]));
      verts.push_back(vert(b[0],b[1]));
      }
    draws.back().count += uint32_t(outline.size()*3);
  }
  auto toScreen = [&](const rc::Vec3& p, bool& visible) {
    const auto  m = toMap(p);
    const float sx = x+in+(m[0]-crop.x0)/view*inner, sy = y+in+(m[1]-crop.y0)/view*inner;
    visible = inside(sx,sy);
    return std::array<float,2>{sx,sy};
    };

  // the main checkpoints, the next one glowing
  const float dot = 11.f*scale;
  if(mapCp!=nullptr) {
    const size_t next = race.checkpoints().next();
    for(size_t i=0; i+1<route.size(); ++i) {
      if(!route[i].main)
        continue;
      bool       vis    = false;
      const auto s      = toScreen(route[i].position,vis);
      const bool isNext = (i==0 ? route.size()-1 : i)==next && race.phase()!=rc::Race::Phase::Finished;
      if(!vis)
        continue;
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
    auto drawCar = [&](const rc::Vec3& pos, const rc::Vec3& fwd, float u) {
      bool vis = false, dummy = false;
      const auto s = toScreen(pos,vis);
      const auto f = toScreen(pos+fwd,dummy);
      const float dx = f[0]-s[0], dy = f[1]-s[1];
      if(!vis || std::abs(dx)+std::abs(dy)<1e-4f)
        return;
      // quad() turns counter-clockwise: its "up" (0,-1) goes to (dx,dy)
      quad(*mapArrow,64,32,{u,0,u+32,32},s[0]-sz*0.5f,s[1]-sz*0.5f,sz,sz,Color{1,1,1,1},std::atan2(-dx,-dy));
      };
    for(auto& o:info.opponents)
      drawCar(o.pos,o.forward,32);
    drawCar(info.carPos,info.carForward,0);
    }
  }

void Hud::metalText(const Font& f, std::string_view s, float x, float y, float size, int align, float gap) {
  // the original's numbers: white fading to grey downwards (the font's own 1 px shadow is all the outline they have)
  const Color top = {1,1,1,1}, bottom = {0.55f,0.55f,0.58f,1};
  text(f,s,x,y,size,top,align,&bottom,gap);
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

float Hud::textWidth(const Font& f, std::string_view s, float size, float gap) const {
  if(!f.img.loaded)
    return 0;
  const float k = size*scale/f.cell;
  float w = 0;
  for(char ch:s) {
    const uint8_t c = uint8_t(ch);
    if(f.has[c])
      w += (f.glyphs[c].u1-f.glyphs[c].u0+gap)*k;
    }
  return w;
  }

float Hud::text(const Font& f, std::string_view s, float x, float y, float size, const Color& c, int align,
               const Color* bottom, float gap) {
  if(!f.img.loaded)
    return x;
  const float k = size*scale/f.cell;
  const float w = textWidth(f,s,size,gap);
  if(align==0) x -= w*0.5f;
  if(align>0)  x -= w;
  for(char ch:s) {
    const uint8_t cc = uint8_t(ch);
    if(!f.has[cc])
      continue;
    const Glyph& g = f.glyphs[cc];
    if(g.v1>g.v0)
      quad(f.img.tex,f.img.w,f.img.h,{g.u0,g.v0,g.u1,g.v1},x,y,(g.u1-g.u0)*k,(g.v1-g.v0)*k,c,0,1,bottom);
    x += (g.u1-g.u0+gap)*k;
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

  if(info.gates)
    gates(race,info.viewProj);
  checkpointArrow(race,info.viewProj);

  // top right: "okrążenie" and the lap number
  if(lapLabel.loaded) {
    const float x = px(LapX,true), y = py(LapY);
    quad(lapLabel.tex,lapLabel.w,lapLabel.h,{0,0,float(lapLabel.w),float(lapLabel.h)},x,y,
         float(lapLabel.w)*scale,float(lapLabel.h)*scale,white4);
    // right after the picture's lettering (x 6..120, y 39..84 of 128), as in the original's screenshot: the Polish
    // one is wider than the English one, which "lapNumPosX" 725 is set for
    const std::string lap = std::to_string(race.lap())+"/"+std::to_string(race.laps());
    metalText(font,lap,x+LapNumDX*scale,py(LapNumY),TimeSize,-1);
    }
  // top left: the place, when there are opponents
  if(info.place>=1 && info.place<=6 && place[info.place-1].loaded) {
    const Image& im = place[info.place-1];
    quad(im.tex,im.w,im.h,{0,0,float(im.w),float(im.h)},px(PlaceX),py(PlaceY),float(im.w)*scale,float(im.h)*scale,
         white4);
    }
  // under it the map
  trackMap(race,info);

  // top middle: the total time; under it a finished lap's time blinks for a while (the original shows no running
  // lap time)
  metalText(font,formatTime(race.time()),px(400),py(TimeY),TimeSize,0,1);
  if(clock-lapAt<LapBlink && !race.lapTimes().empty() && blink) {
    const bool best = race.lapTimes().back()<=race.bestLap();
    text(font,formatTime(race.lapTimes().back()),px(400),py(TimeLapY),26,best ? yellow : white4,0);
    }

  // bottom: turbo on the left, the speed on the right
  const float tank = info.turbo*info.turboMax;
  gauge(false,tank,info.turboMax,std::to_string(int(std::lround(tank*100.f))),Texts[li][4]);
  gauge(true,info.speed/GaugeKmh,info.speedMax>0 ? info.speedMax/GaugeKmh : 1.f,
        std::to_string(int(std::lround(info.speed))),Texts[li][5]);

  // the brackets around the arrow (overtaking.csi), the left one as drawn, the right one mirrored
  if(overtaking!=nullptr) {
    const float bw = (Bracket[2]-Bracket[0])*BracketScale*scale, bh = (Bracket[3]-Bracket[1])*BracketScale*scale;
    const float by = py(BracketY)-bh*0.5f;
    quad(*overtaking,128,128,{Bracket[0],Bracket[1],Bracket[2],Bracket[3]},px(BracketX-BracketDX)-bw*0.5f,by,bw,bh,
         white4);
    quad(*overtaking,128,128,{Bracket[2],Bracket[1],Bracket[0],Bracket[3]},px(BracketX+BracketDX)-bw*0.5f,by,bw,bh,
         white4);
    }
  // one message in the middle at a time
  const float my = py(MessageY);
  if(info.paused) {
    sprite(msgPause,{0,0,float(msgPause.w),float(msgPause.h)},px(400),my,1,white4);
    }
  else if(phase==rc::Race::Phase::Countdown && race.countdown()<=rc::Race::CountTime) {
    // nothing yet during the entry delay (rc::Race::EntryDelay)
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
  else if(clock-hitAt<1.f) {
    // msg_hits.csi: "Łup!" in the top half, "Wielkie Łup!" in the bottom one. RCCars.exe ranks it over the wrong way
    // and the best lap and shows it for 1 s (0x4afba0, 0x56d2fc)
    const float half = float(msgHits.h)*0.5f;
    sprite(msgHits,{0,hitGreat ? half : 0.f,float(msgHits.w),hitGreat ? float(msgHits.h) : half},px(400),my,1,white4);
    }
  else if(race.wrongWay()) {
    if(blink)
      sprite(msgWrong,{0,0,float(msgWrong.w),float(msgWrong.h)},px(400),my,1,white4);
    }
  else if(message==Message::BestLap && clock-messageAt<2.5f) {
    sprite(msgBest,{0,0,float(msgBest.w),float(msgBest.h)},px(400),my,1,white4);
    }

  // after the finish: the lap times, or against opponents the standings (filled in as they finish)
  if(phase==rc::Race::Phase::Finished && !info.standings.empty()) {
    const auto& st   = info.standings;
    const float line = 34, x0 = px(230), x1 = px(570);
    float       y    = 200;
    const float rows = float(st.size()+2);
    quad(*white,1,1,{0,0,1,1},px(210),py(y-12),px(590)-px(210),py(rows*line+24),Color{0,0,0,0.45f});
    for(size_t i=0; i<st.size(); ++i, y+=line) {
      const Color c = st[i].player ? yellow : white4;
      text(font,std::to_string(i+1)+". "+(st[i].player ? std::string(Texts[li][6]) : st[i].name),x0,py(y),28,c,-1);
      text(font,st[i].time>=0 ? formatTime(st[i].time) : std::string("-"),x1,py(y),28,c,1);
      }
    text(font,Texts[li][2],x0,py(y),28,yellow,-1);
    text(font,formatTime(race.bestLap()),x1,py(y),28,yellow,1);
    y += line*1.3f;
    text(font,Texts[li][3],px(400),py(y),24,white4,0);
    }
  else if(phase==rc::Race::Phase::Finished) {
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
