#include "intro.h"

#include <Tempest/Log>
#include <Tempest/Pixmap>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>

#include "formats/settings.h"
#include "formats/texttable.h"
#include "resources.h"
#include "sounds.h"

using namespace Tempest;

namespace {

constexpr uint32_t T_Loading = 41920; // "Wczytywanie..."

// the table 0x56b8a0 after Desktop: name, x0, y0, x1, y1 as parts of the screen
struct Logo {
  const char* name;
  float       x0, y0, x1, y1;
  };
constexpr Logo Logos[3] = {
  {"logoRC",0.16f,   0.04f,       0.8f,     0.893333f},
  {"logo1C",0.03125f,0.0116667f,  0.19125f, 0.225f},
  {"logoCR",0.80875f,0.0116667f,  0.96875f, 0.225f},
  };

std::string lower(std::string s) {
  for(auto& c:s)
    c = char(std::tolower(uint8_t(c)));
  return s;
  }

// the folder's file of that name in any case (Scripts\intro.ini says "intro.dat", the file is Intro.dat)
std::filesystem::path findFile(const std::filesystem::path& dir, const std::string& name) {
  std::error_code ec;
  for(auto& e:std::filesystem::directory_iterator(dir,ec))
    if(lower(e.path().filename().string())==lower(name))
      return e.path();
  return {};
  }

}

Intro::Info Intro::loadInfo(Resources& res) {
  // 0x4a3030: no introName or introNum (or introNum < 1): no intro; at most 4 parts; a part not there is 0 .. 0
  Info ret;
  std::string text;
  try {
    const auto d = res.pack().read("Scripts\\intro.ini");
    text.assign(d.begin(),d.end());
    }
  catch(const std::exception& e) {
    Log::e("Scripts\\intro.ini: ",e.what());
    return ret;
    }
  // introName (string) = "intro.dat"
  for(size_t at = text.find("introName"); at!=std::string::npos; at = text.find("introName",at+1)) {
    const size_t eol = text.find('\n',at);
    const size_t q0  = text.find('"',at);
    const size_t q1  = q0==std::string::npos ? q0 : text.find('"',q0+1);
    if(q1!=std::string::npos && q1<eol) {
      ret.film = text.substr(q0+1,q1-q0-1);
      break;
      }
    }
  const auto ini = rc::KeySections::parse(text);
  const auto& num = ini.get("INTRO_INFO","introNum");
  if(ret.film.empty() || num.empty() || num[0]<1) {
    ret.film.clear();
    return ret;
    }
  const int n = std::min(int(num[0]),4);
  for(int i=0; i<n; ++i) {
    const auto& v = ini.get("INTRO_INFO","part_"+std::to_string(i+1));
    Part p;
    p.from = v.size()==2 ? v[0] : 0.f;
    p.to   = v.size()==2 ? v[1] : 0.f;
    ret.parts.push_back(p);
    }
  return ret;
  }

Intro::Intro(Device& device, Resources& res) : Canvas(device,res) {
  desktop = loadImage(res,"Desktop.csi");
  // Language\<language>\, as Desktop (the Polish logos are the pack's Textures.1 ones)
  for(size_t i=0; i<3; ++i)
    logos[i] = loadImage(res,std::string(Logos[i].name)+".csi");
  // the font of 0x1499d58 (0x4a17b7) is made from cells of 25x28 as the Smash fonts are; Smash26 as the loading
  // screen's "Wczytywanie..."
  loadFont(res,"Smash26",font,true);
  if(!language().empty()) {
    try {
      const auto d = res.readFile(std::filesystem::path("Language")/"Game"/(lower(language())+".tbl"));
      loading = std::string(rc::TextTable::parse(d.data(),d.size()).get(T_Loading));
      }
    catch(const std::exception& e) {
      Log::e("Language/Game: ",e.what());
      }
    }
  }

Intro::~Intro() {
  stop();
  }

bool Intro::start(Sounds& snd) {
  info = loadInfo(resources);
  if(info.film.empty() || info.parts.empty())
    return false;
  const auto path = findFile(resources.gameDir()/"Tracks",info.film);
  if(path.empty()) {
    Log::e("Tracks\\",info.film,": not found, no intro");
    return false;
    }
  try {
    film = std::make_unique<rc::Film>(resources.readFile(std::filesystem::relative(path,resources.gameDir())));
    }
  catch(const std::exception& e) {
    Log::e("Tracks\\",info.film,": ",e.what());
    film.reset();
    return false;
    }
  sounds = &snd;
  sounds->startFilm(film->sound());
  play(0);
  return true;
  }

void Intro::play(size_t p) {
  // 0x4a3030 step 4: the next part, or the end
  part = p;
  if(part>=info.parts.size()) {
    stop();
    return;
    }
  film->seek(info.parts[part].from);
  }

void Intro::skip() {
  if(film!=nullptr)
    play(part+1);
  }

void Intro::stop() {
  if(sounds!=nullptr)
    sounds->stopFilm();
  sounds = nullptr;
  film.reset();
  }

void Intro::tick(float dt) {
  if(film==nullptr)
    return;
  // a frame held up (the window moved) is not caught up all at once
  if(film->advance(std::min(dt,0.1f)))
    ++version;
  const float to = info.parts[part].to;
  if(film->ended() || (to>=0 && film->time()>=to))
    play(part+1);
  }

void Intro::draw(Encoder<CommandBuffer>& enc, uint8_t frame, int w, int h) {
  begin(w,h);
  if(film!=nullptr && version>0) {
    auto& pic = pictures[frame%pictures.size()];
    if(pic.version!=version) {
      // the frame in flight that drew this one is done: its picture can go
      Pixmap pm(uint32_t(film->width()),uint32_t(film->height()),TextureFormat::RGBA8);
      std::memcpy(pm.data(),film->picture().data(),film->picture().size());
      pic.tex     = device.texture(pm,false);
      pic.version = version;
      }
    // 4:3 as the original's 800x600 window, as large as the window allows
    const float fw = std::min(float(w),float(h)*4.f/3.f);
    const float fh = fw*3.f/4.f;
    sharpen = -1;
    quad(pic.tex,film->width(),film->height(),{0,0,float(film->width()),float(film->height())},
         (float(w)-fw)*0.5f,(float(h)-fh)*0.5f,fw,fh,{1,1,1,1});
    sharpen = 0;
    }
  flush(enc,frame);
  }

void Intro::drawStart(Encoder<CommandBuffer>& enc, uint8_t frame, int w, int h) {
  begin(w,h);
  const Color white = {1,1,1,1};
  if(desktop.loaded) {
    sharpen = -1;
    quad(desktop.tex,desktop.w,desktop.h,{0,0,float(desktop.w),float(desktop.h)},0,0,float(w),float(h),white);
    sharpen = 0;
    }
  // on a wide screen the 800x600 screen in the middle, the corner logos by the corners
  for(size_t i=0; i<3; ++i) {
    const Image& t = logos[i];
    if(!t.loaded)
      continue;
    const Logo& l  = Logos[i];
    const float x0 = i==1 ? pl(l.x0*800.f) : i==2 ? px(l.x0*800.f,true) : px(l.x0*800.f);
    const float x1 = i==1 ? pl(l.x1*800.f) : i==2 ? px(l.x1*800.f,true) : px(l.x1*800.f);
    quad(t.tex,t.w,t.h,{0,0,float(t.w),float(t.h)},x0,py(l.y0*600.f),x1-x0,
         py((l.y1-l.y0)*600.f),white);
    }
  // centred in 0, 390 .. 640, 480 of 640x480
  if(!loading.empty() && font.img.loaded) {
    const float size = font.cell;
    text(font,loading,float(w)*0.5f,py((390.f+480.f)*0.5f/480.f*600.f-size*0.56f),size,white,0);
    }
  flush(enc,frame);
  }
