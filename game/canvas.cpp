#include "canvas.h"

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

Canvas::Canvas(Device& device, Resources& res) : device(device), resources(res) {
  // Polish if installed, as the game itself ships it here; English otherwise
  for(const char* l:{"Polish","English"})
    if(std::filesystem::exists(res.gameDir()/"Language"/l/"msg_go.csi")) {
      lang = l;
      break;
      }
  if(lang.empty())
    Log::e("no Language/Polish or Language/English in the game folder: the game's texts and messages will be missing");
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

Canvas::Image Canvas::loadImage(Resources& res, std::string_view file, std::vector<uint8_t>* rgba) {
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
void Canvas::loadFont(Resources& res, std::string_view name, Font& f, bool extra) {
  loadGlyphs(res,name,f,0);
  // Polish has the font's letters with diacritics in a file of their own, on the same grid
  if(extra && f.img.loaded &&
     std::filesystem::exists(res.gameDir()/"Language"/lang/(std::string(name)+"b.csi")))
    loadGlyphs(res,std::string(name)+"b",f,1);
  }

void Canvas::loadGlyphs(Resources& res, std::string_view name, Font& f, uint8_t page) {
  std::vector<uint8_t> rgba;
  Image&               img = page==0 ? f.img : f.extra;
  img = loadImage(res,std::string(name)+".csi",&rgba);
  if(!img.loaded)
    return;
  std::vector<uint8_t> chars;
  try {
    chars = res.readFile(std::filesystem::path("Language")/lang/(std::string(name)+".dat"));
    }
  catch(const std::exception& e) {
    Log::e(e.what());
    img.loaded = false;
    return;
    }
  while(!chars.empty() && chars.back()=='\0')
    chars.pop_back();
  const size_t rows = std::max<size_t>(1,(chars.size()+9)/10);
  const float  cw   = float(img.w)/10.f;
  if(page==0)
    f.cell = std::floor(float(img.h)/float(rows));
  for(size_t i=0; i<chars.size(); ++i) {
    const uint8_t ch = chars[i];
    const int x0 = int(std::ceil(float(i%10)*cw)),     x1 = std::min(img.w,int(std::floor(float(i%10+1)*cw)));
    const int y0 = int(std::ceil(float(i/10)*f.cell)), y1 = std::min(img.h,int(std::floor(float(i/10+1)*f.cell)));
    int minX = x1, maxX = x0-1;
    for(int y=y0; y<y1; ++y)
      for(int x=x0; x<x1; ++x)
        if(rgba[(size_t(y)*size_t(img.w)+size_t(x))*4+3]>40) {
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
    g.page = page;
    f.glyphs[ch] = g;
    f.has[ch]    = true;
    }
  if(page!=0)
    return;
  // a space as wide as a third of a cell, drawn as nothing
  f.glyphs[' '] = Glyph{0,cw*0.35f,0,0};
  f.has[' ']    = true;
  }

std::string Canvas::formatTime(float seconds) {
  const int cs = int(std::lround(std::max(0.f,seconds)*100.f));
  char buf[32];
  std::snprintf(buf,sizeof(buf),"%d:%02d.%02d",cs/6000,(cs/100)%60,cs%100);
  return buf;
  }

void Canvas::quad(const Texture2d& tex, int tw, int th, const Rect& src, float x, float y, float w, float h,
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
  if(draws.empty() || draws.back().tex!=&tex || draws.back().depth || draws.back().sharpen!=sharpen)
    draws.push_back({&tex,verts.size(),0,false,sharpen});
  for(auto& v:{a,b,d,a,d,e})
    verts.push_back(v);
  draws.back().count += 6;
  }

void Canvas::poly(const Texture2d& tex, const std::array<std::array<float,2>,4>& p, const Color& c) {
  if(draws.empty() || draws.back().tex!=&tex || draws.back().depth)
    draws.push_back({&tex,verts.size(),0,false});
  for(int i:{0,1,2,0,2,3})
    verts.push_back(Vertex{{p[i][0],p[i][1]},{0.5f,0.5f},{c[0],c[1],c[2],c[3]}});
  draws.back().count += 6;
  }

void Canvas::quadAt(const Texture2d& tex, float x, float y, float w, float h, float depth, const Color& c) {
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

void Canvas::sprite(const Image& img, const Rect& src, float cx, float cy, float size, const Color& c) {
  if(!img.loaded)
    return;
  const float w = (src.x1-src.x0)*size*scale, h = (src.y1-src.y0)*size*scale;
  quad(img.tex,img.w,img.h,src,cx-w*0.5f,cy-h*0.5f,w,h,c);
  }

float Canvas::textWidth(const Font& f, std::string_view s, float size, float gap) const {
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

float Canvas::text(const Font& f, std::string_view s, float x, float y, float size, const Color& c, int align,
               const Color* bottom, float gap) {
  if(!f.img.loaded)
    return x;
  const float k = size*scale/f.cell;
  const float w = textWidth(f,s,size,gap);
  // the font's opacity is all or nothing: enlarged, its edges follow the bilinear filter's smooth outline, about a
  // pixel soft (as the original draws them), instead of a ramp as wide as a texel that shows the texel grid
  sharpen = f.hard ? std::max(2.f,k) : 0.f;   // at least 2: a clear texel (0) stays clear around the 0.4 edge
  if(align==0) x -= w*0.5f;
  if(align>0)  x -= w;
  for(char ch:s) {
    const uint8_t cc = uint8_t(ch);
    if(!f.has[cc])
      continue;
    const Glyph& g = f.glyphs[cc];
    const Image& img = g.page==0 ? f.img : f.extra;
    if(g.v1>g.v0)
      quad(img.tex,img.w,img.h,{g.u0,g.v0,g.u1,g.v1},x,y,(g.u1-g.u0)*k,(g.v1-g.v0)*k,c,0,1,bottom);
    x += (g.u1-g.u0+gap)*k;
    }
  sharpen = 0;
  return x;
  }

void Canvas::begin(int w, int h) {
  scrW    = w;
  scrH    = h;
  scale   = float(h)/600.f;
  offsetX = (float(w)-800.f*scale)*0.5f;
  verts.clear();
  draws.clear();
  }

size_t Canvas::mark() {
  // an empty draw: what comes after it starts a draw of its own
  draws.push_back({white,verts.size(),0,false,sharpen});
  return draws.size()-1;
  }

void Canvas::flush(Encoder<CommandBuffer>& enc, uint8_t frame, const std::function<void()>& between, size_t at) {
  if(!verts.empty())
    vbo[frame%2] = device.vbo(BufferHeap::Upload,verts);
  struct Push {
    float screen[2];
    float sharpen;
    } push = {{float(scrW),float(scrH)},0};
  for(size_t i=0; i<draws.size(); ++i) {
    auto& d = draws[i];
    if(i==at && between)
      between();
    if(d.count==0)
      continue;
    enc.setPipeline(d.depth ? psoDepth : pso);
    push.sharpen = d.sharpen;
    enc.setPushData(push);
    enc.setBinding(0,*d.tex,Sampler::bilinear(ClampMode::ClampToEdge));
    enc.draw(vbo[frame%2],d.first,d.count);
    }
  verts.clear();
  draws.clear();
  }

// The cockpit's numbers are not in the Smash font: RCCars.exe draws them with "cock_num", a texture kept in
// RCCars.sb itself (XTLG, not in the pack): 256x128, cells of 32x64 in rows of 8 holding "0123456789:'/.,", metal
// grey digits with a soft edge and their shadow drawn in. In each cell two pairs of clear marker texels (row 47..48)
// bound the character's box: the text advances by its width, so the digits stand 18 texels apart.
void Canvas::loadNumbers(Resources& res, Font& f) {
  constexpr std::string_view Chars = "0123456789:'/.,";
  std::vector<uint8_t> sb;
  try {
    sb = res.readFile("RCCars.sb");
    }
  catch(const std::exception& e) {
    Log::e(e.what(),": the cockpit's numbers fall back to the Smash font");
    return;
    }
  static const char name[] = "cock_num";
  auto at = std::search(sb.begin(),sb.end(),name,name+sizeof(name)-1);
  if(at!=sb.end())
    at = std::search(at,sb.end(),"MISC","MISC"+4);
  if(at==sb.end()) {
    Log::e("RCCars.sb: no cock_num texture, the cockpit's numbers fall back to the Smash font");
    return;
    }
  rc::Image img;
  try {
    img = rc::decodeCsi(&*at,size_t(sb.end()-at));
    }
  catch(const std::exception& e) {
    Log::e("RCCars.sb cock_num: ",e.what());
    return;
    }
  Pixmap pm(img.w,img.h,TextureFormat::RGBA8);
  std::memcpy(pm.data(),img.rgba.data(),img.rgba.size());
  f.img.tex    = device.texture(pm,true);
  f.img.w      = int(img.w);
  f.img.h      = int(img.h);
  f.img.loaded = true;
  f.cell       = 64;
  f.hard       = false;
  auto px = [&](int x, int y) { return &img.rgba[(size_t(y)*img.w+size_t(x))*4]; };
  for(size_t i=0; i<Chars.size(); ++i) {
    const int cx = int(i%8)*32, cy = int(i/8)*64;
    // the markers: clear but coloured texels in row 47
    int first = -1, last = -1;
    for(int x=0; x<32; ++x) {
      const uint8_t* p = px(cx+x,cy+47);
      if(p[3]<10 && (p[0]>200 || p[1]>200 || p[2]>200)) {
        if(first<0)
          first = x;
        last = x;
        }
      }
    if(first<0 || last-first<4)
      continue;
    // the box lies between the pairs: from after the left pair (2 texels) up to before the right one
    Glyph g;
    g.u0 = float(cx+first+2);
    g.u1 = float(cx+last-1);
    g.v0 = float(cy);
    g.v1 = float(cy+64);
    f.glyphs[uint8_t(Chars[i])] = g;
    f.has[uint8_t(Chars[i])]    = true;
    }
  }
