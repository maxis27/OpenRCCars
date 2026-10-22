#include "menu.h"

#include <Tempest/Log>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <utility>

#include "resources.h"
#include "sounds.h"

using namespace Tempest;

namespace {

// Language/Game texts (RCCars.exe crsGetString ids)
enum : uint32_t {
  T_Back        = 10003, // "Wróć"
  T_Exit        = 10004, // "Wyjdź"
  T_MainMenu    = 10006, // "Menu główne"
  T_Race        = 10007, // "Wyścig"
  T_QuickRace   = 10014, // "Szybki wyścig"
  T_Summary     = 10015, // "Podsumowanie"
  T_MapInfo     = 10016, // "Mapa i informacje"
  T_TrackStats  = 10017, // "Statystyki toru"
  T_Demo        = 10019, // "Demo gry"
  T_Credits     = 10020, // "Autorzy"
  T_ChangePlayer= 10021, // "Zmień gracza"
  T_Options     = 10044, // "Opcje"
  T_GhostRace   = 10050, // "Wyścig duchów"
  T_Championship= 10051, // "Mistrzostwa"
  T_Garage      = 10052, // "Garaż"
  T_Abort       = 10062, // "Przerwij"
  T_RaceAgain   = 10067, // "Jeszcze raz?"
  T_Apply       = 10070, // "Zastosuj"
  T_Multiplayer = 10131, // "Wielu graczy"
  T_TrackName   = 40000, // + track
  T_TrackInfo   = 40020, // + track: the quick race's description
  T_TrackBlurb  = 40040, // + track: the main menu's one
  T_TrackCode   = 40050, // + track: "Beach_1" ...
  T_CarName     = 40100, // + 4*car
  T_Car         = 41100, // "Samochód"
  T_Booster     = 41101, // "Dopalacz"
  T_Engine      = 41102, // "Silnik"
  T_Tires       = 41103, // "Koła"
  T_Laps        = 41104, // "Liczba okrążeń"
  T_Skill       = 41105, // "Umiejętności przeciwników"
  T_SkillLow    = 41107, // "Niskie", "Norma", "Ekspert", "Ultra"
  T_Loading     = 41920, // "Wczytywanie..."
  T_BackOnTrack = 42000, // "Wróć na tor"
  T_StockPart   = 41800, // "Dom." (41800 booster, 41801 engine, 41802 tires)
  };

// the track codes in the game's order when the texts are missing (40050..40059)
const char* const TrackCodes[Menu::TrackCount] = {
  "beach_1","country_3","beach_3","urban_2","country_2","beach_2","urban_1","country_1","country_4","beach_4",
  };

// the right buttons' places (0x570468), fractions of the screen
const float Slots[8][4] = {
  {0.725f,  0.215f,   1.f,0.27833f}, {0.7525f, 0.29f,    1.f,0.35333f},
  {0.77f,   0.365f,   1.f,0.42833f}, {0.78375f,0.44f,    1.f,0.50333f},
  {0.79125f,0.515f,   1.f,0.57833f}, {0.795f,  0.59f,    1.f,0.65333f},
  {0.79f,   0.69833f, 1.f,0.76167f}, {0.78f,   0.77333f, 1.f,0.83667f},
  };

// the common frame of the menus (0x4de6d0): header, race and back buttons
constexpr float HeaderBox[4] = {0.f,   0.03f,    0.315f,0.1367f};
constexpr float RaceBox[4]   = {0.f,   0.874f,   0.225f,0.98f};
constexpr float BackBox[4]   = {0.83f, 0.9033f,  1.f,   0.97f};

// the textures' parts (0x56ca28 ..)
constexpr float BtnUv[6][4] = {   // ButtonsTextures: red, red lit, grey, orange, orange lit, grey (circle)
  {0,0,1,0.1255f}, {0,0.1255f,1,0.251f}, {0,0.749f,1,0.8745f},
  {0,0.3765f,1,0.502f}, {0,0.502f,1,0.6275f}, {0,0.8784f,1,1},
  };
constexpr float RadioUv[3][4] = {{0,0.0078f,1,0.2578f},{0,0.2578f,1,0.5078f},{0,0.5078f,1,0.7578f}};
constexpr float RaceUv[5][4]  = {  // Button_race: grey, green, green lit ..
  {0.119f,0.0039f,0.8333f,0.125f}, {0.119f,0.1289f,0.8333f,0.25f}, {0.119f,0.2539f,0.8333f,0.375f},
  {0.119f,0.3789f,0.8333f,0.5f},   {0.119f,0.5039f,0.8333f,0.625f},
  };
constexpr float BackUv[3][4]  = {{0.0078f,0.0078f,0.543f,0.2578f},{0.0078f,0.2578f,0.543f,0.5078f},
                                 {0.0078f,0.5078f,0.543f,0.7578f}};
constexpr float PlateUv[4]    = {0,0.2031f,1,0.7969f};
constexpr float PillUv[4][4]  = {{0.51f,0,0.99f,0.25f},{0.51f,0.25f,0.99f,0.5f},{0.51f,0.5f,0.99f,0.75f},
                                 {0.51f,0.75f,0.99f,1.f}};
// HeaderSkin: the red bar with a dark arrow (moving), with a white one, the silver one under it
constexpr float HeaderUv[3][4] = {{0,0,1,0.25f},{0,0.25f,1,0.5f},{0,0.5f,1,0.75f}};

constexpr float Grey  = 0xd0/255.f;  // text of a button, an enum (0xd0d0d0)
constexpr float SlideTime = 0.5f;    // the change of screen

float lerpSlide(float a, float t) {
  // RCCars.exe: `a` at t <= 0, falling to 0 at t = 0.5, 0 after
  if(t<0)    return a;
  if(t>0.5f) return 0;
  return a-2.f*a*t;
  }

std::string lower(std::string_view s) {
  std::string r(s);
  for(auto& c:r)
    c = char(std::tolower(uint8_t(c)));
  return r;
  }

// "Beach_1" -> "beach1": the shots' names
std::string shotPrefix(size_t track) {
  std::string s = lower(TrackCodes[track]);
  s.erase(std::remove(s.begin(),s.end(),'_'),s.end());
  return "shot_"+s;
  }

}

Menu::Menu(Device& device, Resources& res, Sounds& sounds) : Canvas(device,res), sounds(sounds) {
  loadFont(res,"Smash20",font20,true);
  loadFont(res,"Smash26",font26,true);
  if(!language().empty()) {
    try {
      const auto d = res.readFile(std::filesystem::path("Language")/"Game"/(lower(language())+".tbl"));
      texts = rc::TextTable::parse(d.data(),d.size());
      }
    catch(const std::exception& e) {
      Log::e("Language/Game: ",e.what(),": the menus have no texts");
      }
    }
  desktop = loadImage(res,"Desktop.csi");

  const char* podlNames[4] = {"Podl_LeftTop","Podl_RightTop","Podl_LeftBottom","Podl_RightBottom"};
  for(size_t i=0; i<podl.size(); ++i)
    podl[i] = tex(podlNames[i]);
  for(size_t i=0; i<plates.size(); ++i)
    plates[i] = tex("ButtonPodl_right_"+std::to_string(i+1));
  buttons    = tex("ButtonsTextures");
  radio      = tex("RadioButtonsTextures");
  raceBtn    = tex("Button_race");
  backBtn    = tex("Button_back");
  flare      = tex("Flar_1");
  header     = tex("HeaderSkin");
  arrows     = tex("enumarrows");
  msgBox     = tex("messagebox");
  cursor     = tex("cursor");
  progressor = tex("Progressor");
  logo       = tex("logoRC_Main");

  auto spline = [&res](const char* name) {
    try {
      const auto d = res.pack().read(std::string("Splines\\")+name);
      return rc::CurveSpline::parse(std::string_view(reinterpret_cast<const char*>(d.data()),d.size()));
      }
    catch(const std::exception& e) {
      Log::e("Splines\\",name,": ",e.what());
      }
    return rc::CurveSpline();
    };
  focusIn    = spline("ButtonFocSpline.spl");
  focusOut   = spline("ButtonUnFocSpline.spl");
  blick      = spline("ButtonBlickSpline.spl");
  blickAlpha = spline("ButtonBlickAlphaSpline.spl");
  }

std::string Menu::trackName(size_t index) {
  return index<TrackCount ? TrackCodes[index] : "";
  }

size_t Menu::trackIndex(std::string_view name) {
  const std::string n = lower(name);
  for(size_t i=0; i<TrackCount; ++i)
    if(n==TrackCodes[i])
      return i;
  return TrackCount;
  }

std::string_view Menu::str(uint32_t id) const {
  return texts.get(id);
  }

Menu::Tex Menu::tex(const std::string& name) {
  Tex t;
  if(auto* r = resources.texture(name)) {
    t.tex = &r->tex;
    t.w   = int(r->tex.w());
    t.h   = int(r->tex.h());
    }
  return t;
  }

Menu::Box Menu::slotBox(int slot) const {
  if(slot>=0 && slot<8)
    return {Slots[slot][0]*800.f,Slots[slot][1]*600.f,Slots[slot][2]*800.f,Slots[slot][3]*600.f};
  return {BackBox[0]*800.f,BackBox[1]*600.f,BackBox[2]*800.f,BackBox[3]*600.f};
  }

Menu::Box Menu::buttonBox(const Button& b) const {
  if(b.kind==Button::Race)
    return {RaceBox[0]*800.f,RaceBox[1]*600.f,RaceBox[2]*800.f,RaceBox[3]*600.f};
  return slotBox(b.slot);
  }

void Menu::open(Page p) {
  if(p==current && slide==0)
    return;
  if(current==Page::None || current==Page::InRace || p==Page::InRace || p==Page::None) {
    // into or out of a race: at once
    build(p);
    return;
    }
  leave(p,Command::None);
  }

void Menu::close() {
  build(Page::None);
  }

Menu::Command Menu::take() {
  return std::exchange(command,Command::None);
  }

void Menu::leave(Page to, Command cmd) {
  // the buttons go out (0x268), then the next screen comes (0x267)
  if(slide==1)
    return;
  next    = to;
  nextCmd = cmd;
  slide   = 1;
  slideT  = SlideTime;
  playScreen(to==Page::Main ? 1 : 0);
  }

void Menu::build(Page p) {
  current = p;
  right.clear();
  pills.clear();
  enums.clear();
  slide   = 0;
  slideT  = 0;
  carAlpha = 0;

  auto add = [this](int slot, uint32_t text, Button::Kind kind, Command cmd, Page to, bool enabled) {
    Button b;
    b.slot    = slot;
    b.text    = text;
    b.kind    = kind;
    b.command = cmd;
    b.to      = to;
    b.enabled = enabled;
    right.push_back(b);
    return &right.back();
    };

  switch(p) {
    case Page::None:
      return;
    case Page::Main:
      // 0x4c9500: all of them are tabs; those of the parts not made yet are greyed
      headerText = T_MainMenu;
      if(shotAt/4!=choice.track)
        shotAt = choice.track*4;
      add(0,T_Championship,Button::Tab,Command::None,Page::None,false);
      add(1,T_QuickRace,   Button::Tab,Command::None,Page::QuickRace,true);
      add(2,T_GhostRace,   Button::Tab,Command::None,Page::None,false);
      add(3,T_Multiplayer, Button::Tab,Command::None,Page::None,false);
      add(4,T_ChangePlayer,Button::Tab,Command::None,Page::None,false);
      add(5,T_Options,     Button::Tab,Command::None,Page::None,false);
      add(6,T_Demo,        Button::Tab,Command::None,Page::None,false);
      add(7,T_Credits,     Button::Tab,Command::None,Page::None,false);
      add(8,T_Exit,        Button::Back,Command::Quit,Page::None,true);
      add(-1,T_Race,       Button::Race,Command::Race,Page::None,true);
      break;
    case Page::QuickRace: {
      // dlgRACESUM: its pages as radio buttons, the garage as a tab
      headerText = T_QuickRace;
      add(0,T_Summary,   Button::Radio,Command::None,Page::None,true)->checked = true;
      add(1,T_MapInfo,   Button::Radio,Command::None,Page::None,false);
      add(2,T_TrackStats,Button::Radio,Command::None,Page::None,false);
      add(3,T_Garage,    Button::Tab,  Command::None,Page::None,false);
      add(8,T_MainMenu,  Button::Back, Command::None,Page::Main,true);
      add(-1,T_Race,     Button::Race, Command::Race,Page::None,true);

      // Settings\dlgRACESUM.ini
      trackEnum.box   = {71,191,71+273,191+25};
      trackEnum.split = 0;
      trackEnum.values.assign(TrackCount,"");
      trackEnum.at    = choice.track;

      Enum laps;
      laps.box    = {115,265,115+450,265+25};
      laps.split  = 0.44f;
      laps.label  = T_Laps;
      laps.values = {"3","5","7"};
      laps.at     = size_t(std::clamp((choice.laps-3)/2,0,2));
      enums.push_back(laps);

      Enum skill;
      skill.box   = {115,300,115+450,300+25};
      skill.split = 0.44f;
      skill.label = T_Skill;
      for(uint32_t i=0; i<3; ++i)
        skill.values.emplace_back(str(T_SkillLow+i));
      skill.at    = size_t(std::clamp(choice.skill,0,2));
      enums.push_back(skill);

      carEnum.box    = {311,392,311+304,392+25};
      carEnum.split  = 0;
      carEnum.values = {"","",""};
      carEnum.at     = choice.car;
      break;
      }
    case Page::InRace: {
      // dlgINRACE (0x4c7f30): the pills along the bottom, placed by Settings\dlgINRACE.ini
      headerText = 0;
      Pill abort;  abort.box  = {101,518,101+145,518+38}; abort.text = T_Abort;       abort.red = true;
      abort.command = Command::Abort;
      Pill reset;  reset.box  = {247,518,247+166,518+38}; reset.text = T_BackOnTrack;
      reset.command = Command::BackOnTrack;
      Pill back;   back.box   = {413,518,413+166,518+38}; back.text  = T_Apply;
      back.command  = Command::Resume;
      Pill again;  again.box  = {579,518,579+145,518+38}; again.text = T_RaceAgain;   again.red = true;
      again.command = Command::RaceAgain;
      pills = {abort,reset,back,again};
      return;
      }
    }
  // the new buttons come in
  slide  = 2;
  slideT = 0;
  }

void Menu::playControl(int sound) {
  sounds.playInterface("Control sounds",sound);
  }

void Menu::playScreen(int sound) {
  sounds.playInterface("Dialog sounds",sound);
  }

void Menu::tick(float dt) {
  time += dt;
  if(slide==1) {
    slideT -= dt;
    if(slideT<0) {
      const Command cmd = nextCmd;
      nextCmd = Command::None;
      build(next);
      if(cmd!=Command::None)
        command = cmd;
      }
    }
  else if(slide==2) {
    slideT += dt;
    if(slideT>SlideTime)
      slide = 0;
    }

  for(auto& b:right) {
    if(b.anim!=0) {
      b.animT += dt;
      if(b.animT>0.6f)
        b.anim = 0;
      }
    b.pressT = std::max(0.f,b.pressT-dt);
    }
  for(auto* e:{&trackEnum,&carEnum})
    e->pushL = std::max(0.f,e->pushL-dt), e->pushR = std::max(0.f,e->pushR-dt);
  for(auto& e:enums)
    e.pushL = std::max(0.f,e.pushL-dt), e.pushR = std::max(0.f,e.pushR-dt);

  if(current==Page::QuickRace) {
    carYaw   = std::fmod(carYaw+dt*60.f,360.f);
    carAlpha = std::min(1.f,carAlpha+dt/0.5f);
    }
  if(shotScroll!=0) {
    const float step = dt/0.3f;
    shotScroll = std::abs(shotScroll)<=step ? 0.f : shotScroll-std::copysign(step,shotScroll);
    }
  }

void Menu::changeShot(int step) {
  const size_t n = TrackCount*4;
  shotAt       = (shotAt+n+size_t(step+int(n)))%n;
  choice.track = shotAt/4;
  shotScroll  += float(step);
  shotScroll   = std::clamp(shotScroll,-2.f,2.f);
  playControl(2);
  }

void Menu::changeEnum(Enum& e, int step) {
  if(e.values.empty())
    return;
  e.at = (e.at+e.values.size()+size_t(step+int(e.values.size())))%e.values.size();
  (step<0 ? e.pushL : e.pushR) = 0.5f;
  playControl(2);
  if(&e==&trackEnum) {
    choice.track = e.at;
    return;
    }
  if(&e==&carEnum) {
    choice.car = e.at;
    carAlpha   = 0;
    return;
    }
  if(e.label==T_Laps)
    choice.laps = 3+int(e.at)*2;
  if(e.label==T_Skill)
    choice.skill = int(e.at);
  }

void Menu::pressButton(Button& b) {
  if(!b.enabled || slide==1)
    return;
  b.pressT = 0.4f;
  playControl(b.kind==Button::Race ? 5 : 3);
  if(b.kind==Button::Radio) {
    for(auto& o:right)
      if(o.kind==Button::Radio)
        o.checked = (&o==&b);
    return;
    }
  if(b.command==Command::Race || b.command==Command::Quit) {
    command = b.command;
    return;
    }
  if(b.to!=Page::None)
    leave(b.to,Command::None);
  }

void Menu::mouseMove(int x, int y) {
  mouseX  = (float(x)-offsetX)/scale;
  mouseY  = float(y)/scale;
  mouseIn = true;
  if(slide!=0)
    return;
  for(auto& b:right) {
    Box box = buttonBox(b);
    if(b.kind!=Button::Race)
      box.x0 -= 25.f;   // the focused one moves left: it stays focused there
    const bool in = box.contains(mouseX,mouseY);
    if(in==b.hover)
      continue;
    b.hover = in;
    if(b.kind==Button::Race) {
      if(in && b.enabled)
        playControl(4);
      continue;
      }
    if(!b.enabled)
      continue;
    // the focus coming (bit 1) or going (bit 2), 0.6 s of ButtonFocSpline / ButtonUnFocSpline
    b.anim  = in ? 1 : 2;
    b.animT = 0;
    if(in)
      playControl(0);
    }
  for(auto& p:pills) {
    const bool in = p.box.contains(mouseX,mouseY);
    if(in && !p.hover)
      playControl(0);
    p.hover = in;
    }
  auto arrowsOf = [](const Enum& e, Box& l, Box& r) {
    const float h  = e.box.y1-e.box.y0;
    const float s  = h*1.2f;
    const float cy = (e.box.y0+e.box.y1)*0.5f;
    const float lx = e.box.x0+(e.box.x1-e.box.x0)*e.split;
    l = {lx,cy-s*0.5f,lx+s,cy+s*0.5f};
    r = {e.box.x1-s,cy-s*0.5f,e.box.x1,cy+s*0.5f};
    };
  auto hoverEnum = [&](Enum& e) {
    Box l, r;
    arrowsOf(e,l,r);
    const bool hl = l.contains(mouseX,mouseY), hr = r.contains(mouseX,mouseY);
    if((hl && !e.hoverL) || (hr && !e.hoverR))
      playControl(1);
    e.hoverL = hl;
    e.hoverR = hr;
    };
  if(current==Page::QuickRace) {
    hoverEnum(trackEnum);
    hoverEnum(carEnum);
    for(auto& e:enums)
      hoverEnum(e);
    }
  }

void Menu::mouseDown(int x, int y) {
  mouseMove(x,y);
  if(slide!=0)
    return;
  for(auto& b:right)
    if(b.hover) {
      pressButton(b);
      return;
      }
  for(auto& p:pills)
    if(p.hover) {
      playControl(3);
      command = p.command;
      return;
      }
  if(current==Page::QuickRace) {
    for(auto* e:{&trackEnum,&carEnum})
      if(e->hoverL || e->hoverR) {
        changeEnum(*e,e->hoverL ? -1 : 1);
        return;
        }
    for(auto& e:enums)
      if(e.hoverL || e.hoverR) {
        changeEnum(e,e.hoverL ? -1 : 1);
        return;
        }
    }
  if(current==Page::Main) {
    // a track beside the chosen one: that one comes to the middle (dlgMAIN shotListTracks)
    const float side = 100.f+3.f;
    for(int k:{-2,-1,1,2}) {
      const float x0 = k<0 ? 223.f-3.f+float(k)*side : 223.f+199.f+3.f+float(k-1)*side;
      if(Box{x0,303,x0+100,379}.contains(mouseX,mouseY)) {
        changeShot(k);
        return;
        }
      }
    }
  }

void Menu::mouseUp(int, int) {
  }

void Menu::keyDown(Event::KeyType key) {
  if(slide!=0)
    return;
  auto find = [this](Command c) -> Button* {
    for(auto& b:right)
      if(b.command==c)
        return &b;
    return nullptr;
    };
  switch(current) {
    case Page::None:
      return;
    case Page::Main:
      if(key==Event::K_Left)   changeShot(-1);
      if(key==Event::K_Right)  changeShot(1);
      if(key==Event::K_Return) if(auto* b = find(Command::Race)) pressButton(*b);
      return;
    case Page::QuickRace:
      if(key==Event::K_Left)   changeEnum(trackEnum,-1);
      if(key==Event::K_Right)  changeEnum(trackEnum,1);
      if(key==Event::K_Up)     changeEnum(carEnum,-1);
      if(key==Event::K_Down)   changeEnum(carEnum,1);
      if(key==Event::K_Return) if(auto* b = find(Command::Race)) pressButton(*b);
      if(key==Event::K_ESCAPE) leave(Page::Main,Command::None);
      return;
    case Page::InRace:
      if(key==Event::K_ESCAPE)
        command = Command::Resume;
      return;
    }
  }

std::optional<Menu::Preview> Menu::carPreview() const {
  if(current!=Page::QuickRace)
    return std::nullopt;
  // dlgRACESUM animCar
  Preview p;
  p.x = px(306);
  p.y = py(254);
  p.w = 374.f*scale;
  p.h = 304.f*scale;
  p.yaw   = carYaw;
  p.alpha = carAlpha;
  if(slide==1)
    p.alpha = std::min(p.alpha,slideT/SlideTime);
  return p;
  }

// drawing

void Menu::image(const Tex& t, const Box& b, const Uv& uv, const Color& c) {
  if(!t)
    return;
  quad(*t.tex,1,1,{uv.u0,uv.v0,uv.u1,uv.v1},px(b.x0),py(b.y0),(b.x1-b.x0)*scale,(b.y1-b.y0)*scale,
       {c[0],c[1],c[2],c[3]*fade});
  }

void Menu::imageOpaque(const Tex& t, const Box& b, const Uv& uv) {
  sharpen = -1;
  image(t,b,uv,{1,1,1,1});
  sharpen = 0;
  }

void Menu::fill(const Box& b, const Color& c) {
  quad(*white,1,1,{0,0,1,1},px(b.x0),py(b.y0),(b.x1-b.x0)*scale,(b.y1-b.y0)*scale,{c[0],c[1],c[2],c[3]*fade});
  }

void Menu::label(const Font& f, std::string_view s, const Box& b, int ax, int ay, const Color& c, float k) {
  if(s.empty() || !f.img.loaded)
    return;
  // the Smash fonts' letters stand in the upper part of their cell, their baseline at about 2/3 of it; put at the
  // bottom, they stand 4 units above it
  const float h  = f.cell*k;
  const float y  = ay==0 ? b.y0 : ay==1 ? (b.y0+b.y1)*0.5f-h*0.5f-h*0.06f : b.y1-h*0.78f-4.f;
  const float x  = ax==0 ? b.x0 : ax==1 ? (b.x0+b.x1)*0.5f : b.x1;
  text(f,s,px(x),py(y),h,{c[0],c[1],c[2],c[3]*fade},ax-1);
  }

void Menu::lines(const Font& f, std::string_view s, const Box& b, int ax, float step, const Color& c) {
  float y = b.y0;
  while(!s.empty()) {
    const size_t n = s.find('\n');
    std::string_view l = s.substr(0,n);
    if(!l.empty() && l.back()=='\r')
      l.remove_suffix(1);
    label(f,l,{b.x0,y,b.x1,y+step},ax,1,c);
    y += step;
    if(n==std::string_view::npos)
      break;
    s.remove_prefix(n+1);
    }
  }

void Menu::underline(float x0, float x1, float y, const Color& c) {
  // a light line, 2 pixels, with a dark one under it
  fill({x0,y-1,x1,y+1},c);
  fill({x0+1,y+1,x1+1,y+2},{0,0,0,c[3]*0.6f});
  }

void Menu::drawBackground() {
  // 0x4de1a0: the four Podl_ parts, first opaque (their colour where they are clear makes the dark centre), then
  // over it once more with their opacity
  const Box boxes[4] = {{0,0,512,512},{512,0,800,512},{0,512,512,600},{512,512,800,600}};
  const Uv  uvs[4]   = {{0,0,1,1},{0,0,1,1},{0,0,1,0.34f},{0,0,1,0.34f}};
  for(size_t i=0; i<4; ++i)
    imageOpaque(podl[i],boxes[i],uvs[i]);
  for(size_t i=0; i<4; ++i)
    image(podl[i],boxes[i],uvs[i],{1,1,1,1});
  }

void Menu::drawHeader() {
  if(headerText==0 || !header)
    return;
  // 0x4b63a0: the silver bar comes in from the left, then the red one with the text
  const Box   box = {HeaderBox[0]*800.f,HeaderBox[1]*600.f,HeaderBox[2]*800.f,HeaderBox[3]*600.f};
  const float w   = box.x1-box.x0;
  float silver = 0, red = 0;
  if(slide!=0) {
    const float t = slideT*3.f+(slide==1 ? -0.5f : 0.f);
    silver = lerpSlide(w,t);
    const float t2 = slideT*1.5f-(slide==1 ? 0.25f : 0.f);
    red = silver>=1e-3f ? w : lerpSlide(w,t2);
    }
  image(header,{box.x0-silver,box.y0,box.x1-silver,box.y1},{HeaderUv[2][0],HeaderUv[2][1],HeaderUv[2][2],HeaderUv[2][3]},
        {1,1,1,1});
  const auto& uv = HeaderUv[red>=1e-3f ? 0 : 1];
  image(header,{box.x0-red,box.y0,box.x1-red,box.y1},{uv[0],uv[1],uv[2],uv[3]},{1,1,1,1});
  label(font26,str(headerText),{box.x0-red,box.y0,box.x1-red-0.06875f*800.f,box.y1},2,1,{1,1,1,1});
  }

void Menu::drawButton(const Button& b) {
  const Box   r    = buttonBox(b);
  const int   slot = b.slot & 7;
  // the change of screen (0x4ab3d0): the plate first, then the button, from the right; lower ones later
  float plateOff = 0, off = 0;   // fractions of the screen's width
  if(slide!=0) {
    const float a = (float(slot)*0.25f+1.f)*0.4f;
    const float k = 2.f-float(slot)*0.125f;
    plateOff = lerpSlide(a,k*slideT*2.f);
    off      = plateOff<1e-6f ? lerpSlide(a,k*slideT) : 1.f;
    }
  // the focus (0x4ab8f0, 0x4abd50): the button moves left, a glint runs along it
  float glint = -1;
  const bool lit = b.enabled && (b.hover || b.checked);
  if(off==0 || off<1.f) {
    if(b.anim==1) {
      off  += -0.1f*focusIn.at(b.animT/0.6f);
      glint = b.animT/0.6f;
      }
    else if(b.anim==2)
      off  += -0.1f*focusOut.at(b.animT/0.6f);
    else if(lit)
      off  += -0.1f*focusIn.at(1.f);
    }
  if(b.kind==Button::Race) {
    // Button_race: grey, green; lit when focused
    const auto& uv = RaceUv[!b.enabled ? 0 : b.hover ? 4 : 1];
    const float dx = std::max(0.f,plateOff>=1e-6f ? 1.f : off)*800.f;
    const Box   s  = {r.x0-dx,r.y0,r.x1-dx,r.y1};
    image(raceBtn,s,{uv[0],uv[1],uv[2],uv[3]},{1,1,1,1});
    label(font26,str(b.text),{s.x0-0.025f*800.f,s.y0,s.x1,s.y1},1,1,{1,1,1,1});
    return;
    }
  // the plate under it (ButtonPodl_right_<slot+1>)
  const float cy = (r.y0+r.y1)*0.5f-0.02917f*600.f;
  const Box   pb = {0.68f*800.f+plateOff*800.f,cy,800.f+plateOff*800.f,cy+0.0633f*600.f};
  image(plates[size_t(std::clamp(b.slot,0,8))],pb,{PlateUv[0],PlateUv[1],PlateUv[2],PlateUv[3]},{1,1,1,1});

  const bool  back = b.kind==Button::Back;
  const Box   s    = {r.x0+off*800.f,r.y0+(back ? 5.f : 4.f),r.x1+off*800.f,r.y1-(back ? 3.f : 2.f)};
  const bool  pressed = b.pressT>0;
  const Tex*  t  = &buttons;
  const float* uv = nullptr;
  switch(b.kind) {
    case Button::Tab:
    case Button::Action: {
      int row = !b.enabled ? 2 : (b.hover && !pressed) ? 1 : 0;
      if(b.kind==Button::Action)
        row += 3;
      uv = BtnUv[row];
      break;
      }
    case Button::Radio:
      t  = &radio;
      uv = RadioUv[!b.enabled ? 2 : b.checked ? 1 : 0];
      break;
    case Button::Back:
      t  = &backBtn;
      uv = BackUv[!b.enabled ? 2 : (b.hover && !pressed) ? 1 : 0];
      break;
    case Button::Race:
      break;
    }
  image(*t,s,{uv[0],uv[1],uv[2],uv[3]},{1,1,1,1});
  // moved left, the button's last column stretched to the screen's edge (0x4ac2d0)
  if(s.x1<800.f)
    image(*t,{s.x1,s.y0,800.f,s.y1},{uv[2],uv[1],uv[2],uv[3]},{1,1,1,1});
  if(glint>=0 && flare) {
    const float gx = r.x0+0.2f*800.f*blick.at(glint);
    const float a  = 1.f-std::clamp(blickAlpha.at(glint),0.f,1.f);
    image(flare,{gx,r.y0,gx+0.03f*800.f,r.y0+0.03f*600.f},{0,0,1,1},{1,1,1,a});
    }
  // the text, right aligned at 0.995 of the screen (0x4abfa0)
  const float c = lit ? 1.f : Grey;
  label(font20,str(b.text),{s.x0,s.y0,0.995f*800.f,s.y1},2,1,{c,c,c,1});
  }

void Menu::drawPill(const Pill& p) {
  if(!msgBox)
    return;
  // 0x4cc2d0: the round ends kept, the middle stretched
  const auto& uv = PillUv[(p.red ? 0 : 2)+(p.hover ? 1 : 0)];
  const float h  = p.box.y1-p.box.y0;
  const float cap = std::min((p.box.x1-p.box.x0)*0.5f,h*3.0303f*800.f/600.f*0.5f);
  const float um  = (uv[0]+uv[2])*0.5f;
  const float uc  = (uv[2]-uv[0])*0.5f*std::min(1.f,cap/(h*3.0303f*800.f/600.f*0.5f));
  image(msgBox,{p.box.x0,p.box.y0,p.box.x0+cap,p.box.y1},{uv[0],uv[1],uv[0]+uc,uv[3]},{1,1,1,1});
  if(p.box.x1-cap>p.box.x0+cap)
    image(msgBox,{p.box.x0+cap,p.box.y0,p.box.x1-cap,p.box.y1},{um,uv[1],um,uv[3]},{1,1,1,1});
  image(msgBox,{p.box.x1-cap,p.box.y0,p.box.x1,p.box.y1},{uv[2]-uc,uv[1],uv[2],uv[3]},{1,1,1,1});
  const float c = p.hover ? 1.f : 0xaf/255.f;
  // the race's dialogs show their font larger than the menus
  label(font20,str(p.text),{p.box.x0,p.box.y0+0.00333f*600.f,p.box.x1,p.box.y1},1,1,{c,c,c,1},1.2f);
  }

void Menu::drawEnum(const Enum& e, bool marker) {
  const float w  = e.box.x1-e.box.x0;
  const float lx = e.box.x0+w*e.split;
  const Color grey = {Grey,Grey,Grey,1};
  if(e.label!=0) {
    // the label on the left, standing on its line (0x4b4630, 0x4ac6e0)
    label(font26,str(e.label),{e.box.x0,e.box.y0,lx,e.box.y1-3},0,2,grey);
    underline(e.box.x0,lx,e.box.y1-2,grey);
    if(marker && arrows) {
      // the small red arrow in front of it (0x4ac600)
      const float cy = (e.box.y0+e.box.y1)*0.5f;
      image(arrows,{e.box.x0-16,cy-5,e.box.x0-6,cy+5},{0.5f,0,1,0.5f},{1,1,1,1});
      }
    }
  if(!arrows)
    return;
  // the arrows: a white one behind, the red one over it, moving when focused or pressed
  const float h  = e.box.y1-e.box.y0;
  const float s  = h*1.2f;
  const float cy = (e.box.y0+e.box.y1)*0.5f;
  const bool  many = e.values.size()>1;
  auto arrow = [&](float x0, bool left, bool hover, float push) {
    const float hv = hover ? 1.f : 0.f;
    const float sh = (hv+1.f)*3.f;
    const float mv = 3.f*hv+4.f-8.f*std::sin(std::min(push,0.5f)*2.f*3.14159265f*0.5f);
    const float d  = left ? 1.f : -1.f;
    const Uv    back = left ? Uv{0,0,0.5f,0.5f} : Uv{0.5f,0,0,0.5f};
    const Uv    red  = left ? (many ? Uv{0.5f,0,1,0.5f} : Uv{0.5f,0.5f,1,1})
                            : (many ? Uv{1,0,0.5f,0.5f} : Uv{1,0.5f,0.5f,1});
    image(arrows,{x0-d*sh,cy-s*0.5f,x0-d*sh+s,cy+s*0.5f},back,{1,1,1,1});
    image(arrows,{x0+d*mv,cy-s*0.5f,x0+d*mv+s,cy+s*0.5f},red,{1,1,1,1});
    };
  arrow(lx,true,e.hoverL,e.pushL);
  arrow(e.box.x1-s,false,e.hoverR,e.pushR);
  if(e.at<e.values.size() && !e.values[e.at].empty())
    label(font26,e.values[e.at],{lx+s,e.box.y0-2,e.box.x1-s,e.box.y1-2},1,1,grey);
  }

void Menu::drawShot(size_t track, int shot, const Box& b, float alpha, const Box* clip) {
  const Tex t = tex(shotPrefix(track)+"_"+std::to_string(shot));
  if(!t)
    return;
  // the picture with its frame inside the texture (28,52)..(228,204) of 256
  Uv  uv  = {28.f/256.f,52.f/256.f,228.f/256.f,204.f/256.f};
  Box box = b;
  if(clip!=nullptr) {
    if(box.x1<=clip->x0 || box.x0>=clip->x1)
      return;
    const float du = (uv.u1-uv.u0)/(box.x1-box.x0);
    if(box.x0<clip->x0) { uv.u0 += (clip->x0-box.x0)*du; box.x0 = clip->x0; }
    if(box.x1>clip->x1) { uv.u1 -= (box.x1-clip->x1)*du; box.x1 = clip->x1; }
    }
  image(t,box,uv,{1,1,1,alpha});
  }

void Menu::drawMain() {
  // dlgMAIN: the tracks in a row, the chosen one large in the middle (shotListTracks)
  const Box   clip = {12,0,632,600};
  const float side = 100.f+3.f;
  const float sc   = shotScroll*side;
  for(int k=-3; k<=3; ++k) {
    if(k==0)
      continue;
    const size_t e  = (shotAt+TrackCount*4*2+size_t(k))%(TrackCount*4);
    const float  x0 = (k<0 ? 223.f-3.f+float(k)*side : 223.f+199.f+3.f+float(k-1)*side)+sc;
    drawShot(e/4,int(e%4),{x0,303,x0+100,379},1.f,&clip);
    }
  const float cross = 1.f-std::min(1.f,std::abs(shotScroll));
  drawShot(shotAt/4,int(shotAt%4),{223,269,223+199,269+152},std::max(0.25f,cross));

  // staticInfoHeader (Smash26, on its line), staticInfo (Smash20)
  const Color white = {1,1,1,1};
  label(font26,str(T_TrackName+uint32_t(choice.track)),{233,417,233+174,417+30},0,2,white);
  underline(233,233+174,417+30,white);
  lines(font20,str(T_TrackBlurb+uint32_t(choice.track)),{234,452,234+240,452+140},0,22.5f,white);
  }

void Menu::drawQuickRace() {
  // dlgRACESUM
  drawShot(choice.track,0,{110,82,110+196,82+153},1.f);
  drawEnum(trackEnum,false);
  const Color white = {1,1,1,1};
  label(font26,str(T_TrackName+uint32_t(choice.track)),{323,70,323+197,70+40},0,2,white);
  underline(323,323+197,70+40,white);
  lines(font20,str(T_TrackInfo+uint32_t(choice.track)),{323,115,323+219,115+80},0,22.5f,white);
  for(auto& e:enums)
    drawEnum(e,true);

  // the car: its name and parts (0x4d4200: "%s\n%s: %s\n%s: %s\n%s: %s")
  drawEnum(carEnum,false);
  label(font26,str(T_Car),{115,387,115+194,387+25},0,2,white);
  underline(115,115+194,387+25,white);
  const size_t car = std::min<size_t>(choice.car,2);
  const int    up  = std::clamp(choice.upgrade,0,3);
  auto part = [&](uint32_t stock, uint32_t base) {
    // 0x49fd80 ..: "Dom." without an upgrade, then the car's three; the cars' bases go down by 3
    return std::string(up==0 ? str(stock) : str(base-3*uint32_t(car)+uint32_t(up-1)));
    };
  std::string info = std::string(str(T_CarName+4*uint32_t(car)));
  info += "\n"+std::string(str(T_Booster))+": "+part(T_StockPart,40212);
  info += "\n"+std::string(str(T_Engine)) +": "+part(T_StockPart+1,40203);
  info += "\n"+std::string(str(T_Tires))  +": "+part(T_StockPart+2,40194);
  lines(font20,info,{85,418,85+225,418+110},2,22.5f,white);
  }

void Menu::drawCursor() {
  if(!mouseIn || !cursor)
    return;
  image(cursor,{mouseX,mouseY,mouseX+32,mouseY+32},{0,0,1,1},{1,1,1,1});
  }

void Menu::draw(Encoder<CommandBuffer>& enc, uint8_t frame, int w, int h, const std::function<void()>& preview) {
  begin(w,h);
  size_t at = size_t(-1);
  if(current==Page::InRace) {
    // over the race: the scene darkened
    quad(*white,1,1,{0,0,1,1},0,0,float(w),float(h),{0,0,0,0.5f});
    for(auto& p:pills)
      drawPill(p);
    drawCursor();
    flush(enc,frame);
    return;
    }
  drawBackground();

  if(current==Page::QuickRace)
    at = mark();
  // the screen's own controls fade out and in with the change of screen
  fade = slide==0 ? 1.f : std::clamp(slideT/SlideTime,0.f,1.f);
  if(current==Page::Main) {
    if(logo)
      image(logo,{800-100,0,800,100},{0,0,1,1},{1,1,1,1});
    drawMain();
    }
  if(current==Page::QuickRace)
    drawQuickRace();
  fade = 1;
  for(auto& b:right)
    drawButton(b);
  drawHeader();
  drawCursor();
  flush(enc,frame,preview,at);
  }

void Menu::drawLoading(Encoder<CommandBuffer>& enc, uint8_t frame, int w, int h, size_t track, float progress) {
  begin(w,h);
  // 0x570888: Desktop, the map, two lines, four shots, the progress, the name and "Wczytywanie..."
  if(desktop.loaded) {
    sharpen = -1;
    quad(desktop.tex,desktop.w,desktop.h,{0,0,float(desktop.w),float(desktop.h)},px(0),py(0),800*scale,600*scale,
         {1,1,1,1});
    sharpen = 0;
    }
  const std::string prefix = track<TrackCount ? shotPrefix(track) : "";
  if(!prefix.empty()) {
    if(auto t = tex(prefix+"_L"))
      image(t,{0.2f*800,0,0.84f*800,0.8533f*600},{0,0,1,1},{1,1,1,1});
    const Box shots[4] = {{0.0025f*800,0.05f*600,0.3225f*800,0.4767f*600},{0.6775f*800,0.05f*600,0.9975f*800,0.4767f*600},
                          {0.0025f*800,0.365f*600,0.3225f*800,0.7917f*600},{0.6775f*800,0.365f*600,0.9975f*800,0.7917f*600}};
    for(int i=0; i<4; ++i)
      if(auto t = tex(prefix+"_"+std::to_string(i)))
        image(t,shots[i],{0,0,1,1},{1,1,1,1});
    }
  const Color white = {1,1,1,1};
  underline(0.05375f*800,0.9337f*800,0.09f*600,white);
  underline(0.05375f*800,0.9337f*800,0.7583f*600,white);
  if(track<TrackCount)
    label(font26,str(T_TrackName+uint32_t(track)),{0,0,800,0.7467f*600},1,2,white);
  label(font26,str(T_Loading),{0,0,800,0.9033f*600},1,2,white);

  // the progress (class PGS, 0x4b89e0): the grey bar with round ends, the red one from the left as far as the
  // loading is, a dot at every tenth and the slider
  if(progressor) {
    const Box   b  = {0.04125f*800,0.9117f*600,0.9588f*800,0.955f*600};
    const float i0 = b.x0+27, i1 = b.x1-27;   // where the red one goes
    const float xp = i0+(i1-i0)*std::clamp(progress,0.f,1.f);
    image(progressor,{b.x0,b.y0,b.x0+64,b.y1},{0,0,1,0.25f},white);
    image(progressor,{b.x0,b.y0,b.x0+64,b.y1},{0,0.25f,1,0.5f},white);
    image(progressor,{b.x1-64,b.y0,b.x1,b.y1},{1,0,0,0.25f},white);
    image(progressor,{i0,b.y0,b.x1-64,b.y1},{0.5f,0,1,0.25f},white);
    if(xp>i0)
      image(progressor,{i0,b.y0,xp,b.y1},{0.5f,0.25f,1,0.5f},white);
    for(int i=0; i<=10; ++i) {
      const float x = i0+(i1-i0)*float(i)/10.f;
      image(progressor,{x-2,b.y0,x+3,b.y1},{0.4688f,0.5f,0.5469f,0.75f},white);
      }
    image(progressor,{xp-32,b.y0,xp+32,b.y1},{0,0.75f,1,1},white);
    }
  flush(enc,frame);
  }
