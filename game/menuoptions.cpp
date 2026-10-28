#include "menu.h"

#include <Tempest/Log>

#include <algorithm>
#include <cmath>
#include <fstream>

#include "formats/settings.h"
#include "sounds.h"

using namespace Tempest;

// "Opcje" (RCCars.exe 0x4d1980): the frame's radio buttons show its pages (0x56f554), each of which reads its
// values from RCCars.crs when it comes and writes them back when it goes (its message 0xf). And "Autorzy"
// (0x4c3e40) at the end.

namespace {

enum : uint32_t {
  T_Yes        = 12,    // "Tak", "Nie"
  T_No         = 13,
  T_None       = 19,    // "Brak"
  T_On         = 20,    // "Wł.", "Wył."
  T_Off        = 21,
  T_Low        = 25,    // "Niskie", "Średnie", "Wysokie"
  T_PageVideo  = 10037, // "Opcje wideo"
  T_PageCock   = 10031, // "Opcje kokpitu"
  T_PageSound  = 10032, // "Efekty dźwiękowe"
  T_PageInput  = 10034, // "Urządzenia wejściowe"
  T_PageKeys   = 10035, // "Dostosuj sterowanie"
  T_PageAdv    = 10038, // "Zaawansowane"
  T_PageChat   = 10065, // "Wiadomości"
  T_Bpp32      = 10088,
  T_Bpp16      = 10089,
  T_NoLimit    = 10096, // "Bez limitu"
  T_Fps        = 10097, // "%i FPS"
  T_Driver     = 10099, // "Sprzętowy sterownik 3D"
  T_ChatShort  = 10128, // "Krótki", "Pełen"
  T_Rock       = 10210, // "Rock", "Techno", "Oba"
  T_User       = 10311, // "Użytk.", "Klaw.1", "Klaw.2"
  T_NoConflict = 10330, // "Brak konfliktów"
  T_Conflict   = 10331, // "Konflikty z graczem #1, klucz %s", #2
  T_Preview    = 41700, // "Podgląd kabiny"
  T_ChatLine   = 43300, // "wiadomość #1" ..
  T_ChatHead   = 43307, // "Wiadomości"
  T_Player     = 43600, // "Gracz #1", "Gracz #2"
  T_Joystick   = 43602,
  T_JoySens    = 43603,
  T_DeadZone   = 43604,
  T_CtrlType   = 43607, // "Typ sterowania": "Klaw.", "Joystick/Kierownica", "Gamepad"
  T_ShowFor    = 43700, // "Pokaż sterowanie dla": "Gracz 1", "Gracz 2"
  T_Layout     = 43701,
  };

constexpr float Grey = 0xd0/255.f;
constexpr int   PageCount = 7;
constexpr uint32_t PageText[PageCount] = {T_PageVideo,T_PageCock,T_PageSound,T_PageInput,T_PageKeys,T_PageAdv,
                                          T_PageChat};

// the lines one under another (0x4c63f0, 0x4d7930 ..): 25 high, 10 between them (0.04167 and 0.01667 of the screen)
constexpr float RowH   = 0.0416667f*600;
constexpr float RowGap = 0.0166667f*600;

// "Opcje kokpitu" (0x4c1b00, Settings\dlgOPT_COCK.ini): the preview (shotlist, the upper 196 of the 256 lines) and
// over it the part each line shows, the focused one blinking (0x4c2090)
constexpr float CockShot[4]  = {290,280,290+256,280+196};
constexpr float CockName[4]  = {114,346,114+171,346+25};
// "Dostosuj sterowanie" (0x4c2f20, dlgCONTROL_PLAYER.ini): the keys' lines one under another, the first at 0.304 of
// the screen; what the focused line's keys clash with
constexpr float KeysX0 = 0.14125f*800, KeysX1 = 0.80375f*800;
// the lines over and under them: the static 0x2847, made 24/640 wider on the left and 8/640 on the right, then put
// where rectLines says (0x4c34f0)
constexpr float KeysLines[4] = {89,176,89+456,176+288};
constexpr float KeysRow0     = 0.3041667f*600;
constexpr float KeysExplain[4] = {112,470,112+419,470+100};
// "Wiadomości" (dlgOPT_CHATMSG.ini)
constexpr float ChatHead[4]  = {117,95,117+140,95+25};
constexpr float ChatFrame[4] = {263,114,263+285,114+355};

// the video modes offered: the original lists the device's ones from 640x480 (0x4c6810)
const std::pair<int32_t,int32_t> Modes[] = {
  {640,480},{800,600},{1024,768},{1152,864},{1280,720},{1280,960},{1280,1024},{1366,768},{1600,900},{1600,1200},
  {1680,1050},{1920,1080},{1920,1200},{2560,1440},
  };

std::vector<uint8_t> readCrs(const std::filesystem::path& p) {
  std::ifstream f(p,std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());
  }

}

Menu::Option* Menu::option(int id) {
  for(auto& o:options)
    if(o.id==id)
      return &o;
  return nullptr;
  }

void Menu::buildOptions(int page) {
  optPage  = std::clamp(page,0,PageCount-1);
  options.clear();
  optFocus = -1;
  optWait  = -1;
  optEdit  = -1;
  options.reserve(16);
  if(optCrs.empty())
    optCrs = readCrs(resources.gameDir()/"RCCars.crs");
  const auto crs = rc::ConfigFile::parse(optCrs);

  // the lines from (x0, y0) to x1, one under another
  float x0 = 0, x1 = 0, y = 0;
  auto next = [&]() {
    const float at = y;
    y += RowH+RowGap;
    return at;
    };
  auto line = [&](Option::Kind kind, int id, uint32_t text, float split, float inner) -> Option& {
    Option o;
    o.kind    = kind;
    o.id      = id;
    const float at = next();
    o.e.box   = {x0,at,x1,at+RowH};
    o.e.label = text;
    o.e.split = split;
    o.e.inner = inner;
    options.push_back(std::move(o));
    return options.back();
    };
  // a Choice of `names` keeping `stored`, at the value RCCars.crs has
  auto choice = [&](Option& o, const char* section, const char* key, std::vector<std::string> names,
                    std::vector<int32_t> stored, int32_t def) {
    o.section  = section;
    o.key      = key;
    o.e.values = std::move(names);
    o.stored   = std::move(stored);
    const int32_t v = crs.integer(section,key,def);
    o.e.at    = 0;
    o.unknown = true;
    for(size_t i=0; i<o.stored.size(); ++i)
      if(o.stored[i]==v) {
        o.e.at    = i;
        o.unknown = false;
        break;
        }
    };
  auto onOff = [&](Option& o, const char* section, const char* key, bool yesNo, int32_t def) {
    // 0x4b5b60 "Nie", "Tak"; 0x4b5bc0 "Wył.", "Wł."
    choice(o,section,key,{std::string(str(yesNo ? T_No : T_Off)),std::string(str(yesNo ? T_Yes : T_On))},{0,1},def);
    };
  auto slider = [&](Option& o, const char* section, const char* key, int32_t mn, int32_t mx, int32_t st,
                    int32_t def) {
    o.section = section;
    o.key     = key;
    o.min     = mn;
    o.max     = mx;
    o.step    = st;
    o.value   = std::clamp(section!=nullptr ? crs.integer(section,key,def) : def,mn,mx);
    };
  auto quality = [&](Option& o, const char* key, std::vector<int32_t> stored, int32_t def) {
    choice(o,o.section ? o.section : "Video",key,{std::string(str(T_Low)),std::string(str(T_Low+1)),
                                                   std::string(str(T_Low+2))},std::move(stored),def);
    };

  switch(optPage) {
    case 0: {
      // "Opcje wideo" (0x4c63f0, table 0x56eb10): from 0.14625, 0.225 to 0.67 of the screen, labels 0.4
      x0 = 0.14625f*800; x1 = 0.67f*800; y = 0.225f*600;
      auto& res = line(Option::Choice,0x2760,10080,0.4f,0.65f);
      res.section = "VideoRace";
      const int32_t w = crs.integer("VideoRace","XResolution",800), h = crs.integer("VideoRace","YResolution",600);
      bool listed = false;
      for(auto& [mw,mh]:Modes) {
        res.e.values.push_back(std::to_string(mw)+"x"+std::to_string(mh));
        res.stored.push_back(mw*0x10000+mh);
        if(mw==w && mh==h) {
          res.e.at = res.e.values.size()-1;
          listed = true;
          }
        }
      if(!listed) {
        res.e.values.push_back(std::to_string(w)+"x"+std::to_string(h));
        res.stored.push_back(w*0x10000+h);
        res.e.at = res.e.values.size()-1;
        }
      choice(line(Option::Choice,0x2761,10081,0.4f,0.65f),"VideoRace","BPP",
             {std::string(str(T_Bpp32)),std::string(str(T_Bpp16))},{32,16},32);
      onOff(line(Option::Choice,0x2762,10082,0.4f,0.65f),"Video","VIDEO_RunInWindow",true,0);
      // the textures' quality the other way round: "Niskie" 2 .. "Wysokie" 0
      quality(line(Option::Choice,0x2763,10083,0.4f,0.65f),"VIDEO_TexQual",{2,1,0},0);
      quality(line(Option::Choice,0x2764,10084,0.4f,0.65f),"RenderQual",{0,1,2},2);
      quality(line(Option::Choice,0x2765,10085,0.4f,0.65f),"SFXQual",{0,1,2},2);
      // "Gamma" 1 .. 200 by 20, only when the device can change it (Gamma_Changes): not here
      auto& gamma = line(Option::Slider,0x2766,10086,0.4f,1);
      slider(gamma,"Video","VIDEO_Gamma",1,200,20,100);
      gamma.e.enabled = false;
      break;
      }
    case 1:
      // "Opcje kokpitu" (0x4c1b00, table 0x56e620): from 0.14, 0.2 to 0.65, labels 0.5, the arrows 0.6 of the rest
      x0 = 0.14f*800; x1 = 0.65f*800; y = 0.2f*600;
      onOff(line(Option::Choice,0x278b,10123,0.5f,0.6f),"Cockpit","ShowList",false,1);
      onOff(line(Option::Choice,0x278a,10122,0.5f,0.6f),"Cockpit","ShowMap",false,1);
      onOff(line(Option::Choice,0x2789,10121,0.5f,0.6f),"Cockpit","DirectArrow",false,1);
      choice(line(Option::Choice,0x2788,10120,0.5f,0.6f),"Player","AutoChatPrior",
             {std::string(str(T_ChatShort)),std::string(str(T_ChatShort+1))},{0,1},1);
      break;
    case 2: {
      // "Efekty dźwiękowe" (0x4d7930, table 0x56f988): from 0.14625, 0.2 to 0.67125
      x0 = 0.14625f*800; x1 = 0.67125f*800; y = 0.2f*600;
      onOff(line(Option::Choice,0x27d9,10201,0.4f,0.65f),"Sound","Use_Sound",true,1);
      auto& q = line(Option::Choice,0x27dd,10205,0.4f,0.65f);
      q.section = "Sound";
      quality(q,"SoundQuality",{0,1,1},1);
      slider(line(Option::Slider,0x27db,10203,0.4f,1),"Sound","SoundVolume",0,100,10,70);
      onOff(line(Option::Choice,0x27df,10207,0.4f,0.65f),"Sound","Use_BkSound",true,1);
      slider(line(Option::Slider,0x27e0,10208,0.4f,1),"Sound","SoundBkVolume",0,100,10,100);
      choice(line(Option::Choice,0x27e1,10209,0.4f,0.65f),"Sound","Snd_Style",
             {std::string(str(T_Rock)),std::string(str(T_Rock+1)),std::string(str(T_Rock+2))},{0,1,-1},-1);
      // a line left empty (0x27e4), then the system's volume: the mixer's, not RCCars.crs'
      next();
      slider(line(Option::Slider,0x27da,10202,0.4f,1),nullptr,nullptr,0,100,10,sounds.masterVolume());
      break;
      }
    case 3:
      // "Urządzenia wejściowe" (0x4c2470, dlgCONTROL.ini): each player's way of steering and joystick, the second
      // player's 220 lower; no joystick here: "Brak", its sliders greyed
      for(int p=0; p<2; ++p) {
        const float dy = float(p)*220;
        const std::string n = std::to_string(p+1);
        static const char* const kbd[2]  = {"KbdJoy1","KbdJoy2"};
        static const char* const sens[2] = {"Joy1Sens","Joy2Sens"};
        static const char* const dz[2]   = {"Joy1DZone","Joy2DZone"};
        x0 = 115; x1 = 115+425; y = 120+dy;
        choice(line(Option::Choice,0x1772+p*4,T_CtrlType,0.4f,0.65f),"Input",kbd[p],
               {std::string(str(T_CtrlType+1)),std::string(str(T_CtrlType+2)),std::string(str(T_CtrlType+3))},
               {0,1,2},1);
        auto& joy = line(Option::Choice,0x1773+p*4,T_Joystick,0.4f,0.65f);
        joy.e.values = {std::string(str(T_None))};
        auto& s = line(Option::Slider,0x1774+p*4,T_JoySens,0.4f,1);
        slider(s,"Input",sens[p],0,100,10,50);
        s.e.enabled = false;
        auto& d = line(Option::Slider,0x1775+p*4,T_DeadZone,0.4f,1);
        slider(d,"Input",dz[p],0,100,10,50);
        d.e.enabled = false;
        }
      break;
    case 4: {
      // "Dostosuj sterowanie" (0x4c2f20): whose keys, their layout, and the eight actions (table 0x56e888)
      ctrlUser   = {Controls::read(crs,0),Controls::read(crs,1)};
      ctrlLayout = {crs.integer("Input","Player1Layout",Controls::Keys1),
                    crs.integer("Input","Player2Layout",Controls::Keys2)};
      x0 = 115; x1 = 115+360; y = 90;
      auto& who = line(Option::Choice,0x17d4,T_ShowFor,0.38f,0.7f);
      who.e.values = {std::string(str(T_ShowFor+2)),std::string(str(T_ShowFor+3))};
      y = 130;
      line(Option::Choice,0x17d5,T_Layout,0.38f,0.7f);
      // Gaz, Wsteczny, Skręt w lewo, Skręt w prawo, Dopalacz, Skok, Hamulec, Zresetuj
      static const std::pair<int,Controls::Action> rows[] = {
        {0x283c,Controls::Forw},{0x283d,Controls::Back},{0x283e,Controls::Left},{0x283f,Controls::Right},
        {0x2840,Controls::Boost},{0x2842,Controls::Jump},{0x2841,Controls::Brake},{0x2843,Controls::Reset},
        };
      x0 = KeysX0; x1 = KeysX1; y = KeysRow0;
      for(auto& [id,a]:rows) {
        auto& k = line(Option::Keys,id,uint32_t(id),0.34f,1);
        k.action = a;
        }
      showControlsOf(0);
      break;
      }
    case 5: {
      // "Zaawansowane" (0x4c6de0, table 0x56eb70): from 0.14625, 0.225 to 0.67; what the focused line does under them
      x0 = 0.14625f*800; x1 = 0.67f*800; y = 0.225f*600;
      auto& tb = line(Option::Choice,0x276d,10093,0.4f,0.65f);
      onOff(tb,"Video","VIDEO_TripleBuf",true,0);
      tb.help = 10112;
      auto& lm = line(Option::Slider,0x2772,10098,0.4f,1);
      slider(lm,"Video","VIDEO_LMBrightness",0,100,10,0);
      lm.help = 10113;
      auto& fps = line(Option::Choice,0x276f,10095,0.4f,0.65f);
      std::vector<std::string> names = {std::string(str(T_NoLimit))};
      std::vector<int32_t>     limits = {0};
      for(int32_t f=35; f<0x47; f+=5) {
        names.push_back(format(str(T_Fps),{std::to_string(f)}));
        limits.push_back(f);
        }
      choice(fps,"Video","FPS_Limit",std::move(names),std::move(limits),0);
      fps.help = 10114;
      break;
      }
    case 6:
      // "Wiadomości" (0x4bf6c0, dlgOPT_CHATMSG.ini): seven lines of 60 letters, 40 apart
      for(int i=0; i<7; ++i) {
        x0 = 115; x1 = 115+418; y = 150+float(i)*40;
        auto& t = line(Option::Text,0x2b5c+i,T_ChatLine+uint32_t(i),0.35f,1);
        t.section = "ChatMessages";
        static const char* const keys[7] = {"Message1","Message2","Message3","Message4","Message5","Message6",
                                            "Message7"};
        t.key    = keys[i];
        t.text   = crs.string("ChatMessages",keys[i]);
        t.maxLen = 60;
        }
      break;
    }
  refreshOptions();
  }

void Menu::refreshOptions() {
  if(optPage==2) {
    // 0x4d7c70: no sound, no quality or volumes; no music, no music's volume or style
    const auto* use = option(0x27d9);
    const auto* bk  = option(0x27df);
    const bool  on  = use==nullptr || use->e.at==1;
    const bool  mus = on && (bk==nullptr || bk->e.at==1);
    for(int id:{0x27dd,0x27db,0x27df})
      if(auto* o = option(id))
        o->e.enabled = on;
    for(int id:{0x27e0,0x27e1})
      if(auto* o = option(id))
        o->e.enabled = mus;
    }
  }

void Menu::showControlsOf(int player) {
  // 0x4c35b0: the keys shown so far go to the player they were of, then the other player's come with the layouts
  // there are (the joystick's only with a joystick)
  if(player!=ctrlPlayer)
    for(auto& o:options)
      if(o.kind==Option::Keys)
        ctrlUser[size_t(ctrlPlayer)][o.action] = o.keys;
  ctrlPlayer = std::clamp(player,0,1);
  if(auto* who = option(0x17d4))
    who->e.at = size_t(ctrlPlayer);
  if(auto* lay = option(0x17d5)) {
    lay->e.values = {std::string(str(T_User)),std::string(str(T_User+1)),std::string(str(T_User+2))};
    lay->stored   = {Controls::User,Controls::Keys1,Controls::Keys2};
    lay->e.at     = 0;
    for(size_t i=0; i<lay->stored.size(); ++i)
      if(lay->stored[i]==ctrlLayout[size_t(ctrlPlayer)])
        lay->e.at = i;
    }
  changeLayout();
  }

void Menu::changeLayout() {
  // 0x4c37c0: the layout's keys shown; the player's own are what was kept of them
  auto* lay = option(0x17d5);
  if(lay==nullptr)
    return;
  const int32_t l = lay->stored[lay->e.at];
  ctrlLayout[size_t(ctrlPlayer)] = l;
  const Controls::Table t = l==Controls::User ? ctrlUser[size_t(ctrlPlayer)] : Controls::preset(l);
  for(auto& o:options)
    if(o.kind==Option::Keys)
      o.keys = t[o.action];
  }

void Menu::bindKey(Option& o, int slot, int32_t code) {
  // 0x4c32e0: the layout becomes the player's own, the key leaves the player's other actions and columns, and what
  // is shown is the player's own from now on
  o.keys[size_t(slot)] = Controls::Slot::keyboard(code);
  if(code!=0)
    for(auto& other:options) {
      if(other.kind!=Option::Keys)
        continue;
      for(size_t k=0; k<2; ++k)
        if((&other!=&o || int(k)!=slot) && other.keys[k].key()==code)
          other.keys[k] = Controls::Slot();
      }
  if(auto* lay = option(0x17d5))
    lay->e.at = 0;
  ctrlLayout[size_t(ctrlPlayer)] = Controls::User;
  for(auto& other:options)
    if(other.kind==Option::Keys)
      ctrlUser[size_t(ctrlPlayer)][other.action] = other.keys;
  playControl(2);
  }

void Menu::saveOptions() {
  if(options.empty() || optCrs.empty())
    return;
  std::vector<uint8_t> data = optCrs;
  for(auto& o:options) {
    switch(o.kind) {
      case Option::Choice:
        if(o.id==0x2760 && o.e.at<o.stored.size()) {
          data = rc::ConfigFile::withInteger(data,"VideoRace","XResolution",o.stored[o.e.at]/0x10000);
          data = rc::ConfigFile::withInteger(data,"VideoRace","YResolution",o.stored[o.e.at]%0x10000);
          }
        else if(o.key!=nullptr && o.e.at<o.stored.size() && !o.unknown)
          data = rc::ConfigFile::withInteger(data,o.section,o.key,o.stored[o.e.at]);
        break;
      case Option::Slider:
        if(o.key!=nullptr)
          data = rc::ConfigFile::withInteger(data,o.section,o.key,o.value);
        break;
      case Option::Text:
        // 0x4bf820: "ChatMessages" with room for 60 letters
        data = rc::ConfigFile::withString(data,o.section,o.key,o.text,60);
        break;
      case Option::Keys:
        break;
      }
    }
  if(optPage==4) {
    // 0x4c32d0 -> 0x4e4f00: both players' own keys and their layouts
    for(auto& o:options)
      if(o.kind==Option::Keys)
        ctrlUser[size_t(ctrlPlayer)][o.action] = o.keys;
    for(int p=0; p<2; ++p)
      data = Controls::write(data,p,ctrlUser[size_t(p)]);
    data = rc::ConfigFile::withInteger(data,"Input","Player1Layout",ctrlLayout[0]);
    data = rc::ConfigFile::withInteger(data,"Input","Player2Layout",ctrlLayout[1]);
    }
  if(auto* m = option(0x27da))
    sounds.setMasterVolume(m->value);
  if(data==optCrs)
    return;
  optCrs = std::move(data);
  const auto path = resources.gameDir()/"RCCars.crs";
  std::ofstream f(path,std::ios::binary|std::ios::trunc);
  f.write(reinterpret_cast<const char*>(optCrs.data()),std::streamsize(optCrs.size()));
  if(!f)
    Log::e("Options: can't write RCCars.crs");
  optionsChanged = true;
  }

void Menu::setOptionValue(Option& o, int32_t v) {
  v = std::clamp(v,o.min,o.max);
  if(v==o.value)
    return;
  o.value = v;
  playControl(2);
  // the system's volume at once (the mixer of 0x4d7869)
  if(o.id==0x27da)
    sounds.setMasterVolume(v);
  }

void Menu::changeOption(Option& o, int step) {
  if(!o.e.enabled)
    return;
  if(o.kind==Option::Slider) {
    setOptionValue(o,o.value+step*o.step);
    (step<0 ? o.e.pushL : o.e.pushR) = 0.5f;
    return;
    }
  if(o.kind!=Option::Choice)
    return;
  changeEnum(o.e,step);
  o.unknown = false;
  if(o.id==0x17d4)
    showControlsOf(int(o.e.at));
  else if(o.id==0x17d5)
    changeLayout();
  refreshOptions();
  }

void Menu::optionsTick(float dt) {
  optFocusT += dt;
  for(auto& o:options)
    o.e.pushL = std::max(0.f,o.e.pushL-dt), o.e.pushR = std::max(0.f,o.e.pushR-dt);
  }

void Menu::optionsHover(float mx, float my) {
  int focus = -1;
  for(size_t i=0; i<options.size(); ++i) {
    auto& o = options[i];
    const Box& b = o.e.box;
    // a line is focused across its whole height and the gap under it
    if(mx>=b.x0-20 && mx<b.x1 && my>=b.y0-RowGap*0.5f && my<b.y1+RowGap*0.5f)
      focus = int(i);
    bool hl = false, hr = false;
    if(o.e.enabled && o.kind==Option::Choice && o.e.values.size()>1) {
      Box l, r;
      enumArrows(o.e,l,r);
      hl = l.contains(mx,my);
      hr = r.contains(mx,my);
      }
    if(o.e.enabled && o.kind==Option::Slider) {
      // the bar's ends: 40 units each side (0x4b9fb0)
      const float bx0 = b.x0+(b.x1-b.x0)*o.e.split, cy = (b.y0+b.y1)*0.5f;
      hl = Box{bx0,cy-20,bx0+40,cy+20}.contains(mx,my);
      hr = Box{b.x1-40,cy-20,b.x1,cy+20}.contains(mx,my);
      }
    if((hl && !o.e.hoverL) || (hr && !o.e.hoverR))
      playControl(1);
    o.e.hoverL = hl;
    o.e.hoverR = hr;
    o.hoverKey = -1;
    if(o.kind==Option::Keys && b.contains(mx,my)) {
      const float w = b.x1-b.x0;
      if(mx>=b.x0+w*0.34f)
        o.hoverKey = mx<b.x0+w*0.7f ? 0 : 1;
      }
    }
  if(focus!=optFocus) {
    optFocus  = focus;
    optFocusT = 0;
    if(focus>=0)
      playControl(0);
    }
  }

void Menu::optionsClick(float mx, float my) {
  optionsHover(mx,my);
  optEdit = -1;
  for(size_t i=0; i<options.size(); ++i) {
    auto& o = options[i];
    if(!o.e.enabled)
      continue;
    if(o.e.hoverL || o.e.hoverR) {
      changeOption(o,o.e.hoverL ? -1 : 1);
      return;
      }
    const Box& b = o.e.box;
    if(o.kind==Option::Slider) {
      // on the track between the ends: there (0x4bb030), to the step
      const float bx0 = b.x0+(b.x1-b.x0)*o.e.split, cy = (b.y0+b.y1)*0.5f;
      const float t0 = bx0+47.5f, t1 = b.x1-47.5f;
      if(mx>=bx0+40 && mx<b.x1-40 && my>=cy-20 && my<cy+20 && t1>t0) {
        const float k = std::clamp((mx-t0)/(t1-t0),0.f,1.f);
        const float v = float(o.min)+k*float(o.max-o.min);
        const int32_t s = std::max(o.step,1);
        setOptionValue(o,o.min+int32_t(std::lround((v-float(o.min))/float(s)))*s);
        return;
        }
      }
    if(o.kind==Option::Keys && o.hoverKey>=0) {
      // the key clicked waits for a key, red (0x4b67f0)
      optWait     = int(i);
      optWaitSlot = o.hoverKey;
      playControl(3);
      return;
      }
    if(o.kind==Option::Text && b.contains(mx,my)) {
      optEdit = int(i);
      playControl(3);
      return;
      }
    }
  }

bool Menu::optionsKey(Event::KeyType key, uint32_t ch) {
  if(optWait>=0 && size_t(optWait)<options.size()) {
    // Esc gives up, Delete clears, any other key is taken
    auto& o = options[size_t(optWait)];
    const int slot = optWaitSlot;
    optWait = -1;
    if(key==Event::K_ESCAPE)
      return true;
    if(key==Event::K_Delete) {
      bindKey(o,slot,0);
      return true;
      }
    const int32_t c = Controls::code(key);
    if(c!=0)
      bindKey(o,slot,c);
    return true;
    }
  if(optEdit>=0 && size_t(optEdit)<options.size()) {
    auto& o = options[size_t(optEdit)];
    if(key==Event::K_Return || key==Event::K_ESCAPE) {
      optEdit = -1;
      return true;
      }
    if(key==Event::K_Back) {
      if(!o.text.empty())
        o.text.pop_back();
      return true;
      }
    const int c = typedChar(ch);
    if(c>=0 && o.text.size()<o.maxLen)
      o.text.push_back(char(c));
    return true;
    }
  if((key==Event::K_Left || key==Event::K_Right) && optFocus>=0 && size_t(optFocus)<options.size()) {
    changeOption(options[size_t(optFocus)],key==Event::K_Left ? -1 : 1);
    return true;
    }
  return false;
  }

void Menu::drawSlider(const Option& o, bool focused) {
  // 0x4b9fb0, 0x4bb030: the label, then the bar of sliderbar.csi (64x256, rows of 32 lines): its ends 80 wide, the
  // right one mirrored, red (lit when focused, grey when not enabled), its middle stretched; the orange filling up
  // to the knob and the dots of the steps on the track 47.5 in from each end
  const Box&  b   = o.e.box;
  const float was = fade;
  if(!o.e.enabled)
    fade *= 0.5f;
  const Color grey = {Grey,Grey,Grey,1};
  const float bx0 = b.x0+(b.x1-b.x0)*o.e.split;
  if(o.e.label!=0) {
    label(font26,str(o.e.label),{b.x0,b.y0,bx0,b.y1-3},0,2,grey,fit(font26,str(o.e.label),bx0-b.x0-4));
    underline(b.x0,bx0,b.y1-2,grey);
    if(focused && arrows) {
      const float cy = (b.y0+b.y1)*0.5f;
      image(arrows,{b.x0-16,cy-5,b.x0-6,cy+5},{0.5f,0,1,0.5f},{1,1,1,1});
      }
    }
  const Tex bar = tex("sliderbar");
  if(!bar) {
    fade = was;
    return;
    }
  const float cy  = (b.y0+b.y1)*0.5f;
  const float y0  = cy-20, y1 = cy+20;
  const float row = !o.e.enabled ? 0.f : (focused ? 0.25f : 0.125f);
  const float r1  = row+0.125f;
  const float t0  = bx0+47.5f, t1 = b.x1-47.5f;
  image(bar,{bx0+57.5f,y0,b.x1-57.5f,y1},{0.75f,row,1,r1},{1,1,1,1});
  image(bar,{bx0,y0,bx0+80,y1},{0,row,1,r1},{1,1,1,1});
  image(bar,{b.x1-80,y0,b.x1,y1},{1,row,0,r1},{1,1,1,1});
  // the filling: its round end (the left of the orange row) to the knob
  const float k  = o.max>o.min ? float(o.value-o.min)/float(o.max-o.min) : 0.f;
  const float fx = t0+(t1-t0)*k;
  if(o.e.enabled) {
    const float cap = std::min(26.25f,std::max(fx-(bx0+33.75f),0.f));
    image(bar,{bx0+33.75f,y0,bx0+33.75f+cap,y1},{0.42f,0.375f,0.42f+cap/80.f,0.5f},{1,1,1,1});
    if(fx>bx0+60)
      image(bar,{bx0+60,y0,fx,y1},{0.75f,0.375f,1,0.5f},{1,1,1,1});
    }
  // the steps' dots, lit up to the value
  if(o.step>0 && o.max>o.min) {
    for(int32_t v=o.min; v<=o.max; v+=o.step) {
      const float x  = t0+(t1-t0)*float(v-o.min)/float(o.max-o.min);
      const float v0 = v<=o.value && o.e.enabled ? 0.625f : 0.5f;
      image(bar,{x-2.5f,cy-3.75f,x+2.5f,cy+3.75f},{0.465f,v0,0.545f,v0+0.125f},{1,1,1,1});
      }
    }
  image(bar,{fx-5,y0,fx+6.25f,y1},{0.435f,0.75f,0.575f,0.875f},{1,1,1,1});
  fade = was;
  }

void Menu::drawKeys(const Option& o, bool focused) {
  // 0x4b67f0, 0x4b75c0: the action on the left (to 0.34), its two keys from 0.34 and 0.7, 20 units in; "Brak" for
  // none, the one waited for red
  const Box&  b = o.e.box;
  const float w = b.x1-b.x0;
  const Color grey = {Grey,Grey,Grey,1};
  label(font26,str(o.e.label),{b.x0,b.y0,b.x0+w*0.34f-4,b.y1-3},0,2,focused ? Color{1,1,1,1} : grey,
        fit(font26,str(o.e.label),w*0.34f-8));
  underline(b.x0,b.x0+w*0.34f-4,b.y1-2,grey);
  const size_t self = size_t(&o-options.data());
  for(int k=0; k<2; ++k) {
    const float x0 = b.x0+w*(k==0 ? 0.34f : 0.7f), x1 = k==0 ? b.x0+w*0.7f : b.x1;
    const bool  wait = optWait>=0 && size_t(optWait)==self && optWaitSlot==k;
    const int32_t c  = o.keys[size_t(k)].key();
    std::string n = c!=0 ? Controls::name(c) : std::string(str(T_None));
    if(c==0 && !o.keys[size_t(k)].empty())
      n = "?";
    const Color col = wait ? Color{1,0.15f,0.15f,1} : (o.hoverKey==k ? Color{1,1,1,1} : grey);
    label(font20,n,{x0+20,b.y0,x1,b.y1},0,1,col);
    }
  }

void Menu::drawText(const Option& o, bool focused) {
  // 0x4b3540: the label grey on its line, the text after it, the caret blinking while it is typed in
  const Box&  b = o.e.box;
  const float lx = b.x0+(b.x1-b.x0)*o.e.split;
  const Color grey = {Grey,Grey,Grey,1};
  label(font26,str(o.e.label),{b.x0,b.y0,lx,b.y1-3},0,2,grey,fit(font26,str(o.e.label),lx-b.x0-4));
  underline(b.x0,lx,b.y1-2,grey);
  if(focused && arrows) {
    const float cy = (b.y0+b.y1)*0.5f;
    image(arrows,{b.x0-16,cy-5,b.x0-6,cy+5},{0.5f,0,1,0.5f},{1,1,1,1});
    }
  const bool editing = optEdit>=0 && &options[size_t(optEdit)]==&o;
  // what fits: its end while it is typed in, its beginning else
  std::string_view shown = o.text;
  const float room = b.x1-lx-14;
  while(!shown.empty() && textWidth(font20,shown,font20.cell)/scale>room)
    shown = editing ? shown.substr(1) : shown.substr(0,shown.size()-1);
  label(font20,shown,{lx+10,b.y0,b.x1,b.y1},0,1,editing ? Color{1,1,1,1} : grey);
  if(editing && std::fmod(time,0.5f)<0.25f) {
    const float x = lx+10+textWidth(font20,shown,font20.cell)/scale;
    fill({x+1,b.y0+3,x+3,b.y1-3},{1,1,1,1});
    }
  }

void Menu::drawOptions() {
  const Color grey = {Grey,Grey,Grey,1};
  for(size_t i=0; i<options.size(); ++i) {
    const auto& o = options[i];
    const bool  focused = int(i)==optFocus;
    switch(o.kind) {
      case Option::Choice: drawEnum(o.e,focused); break;
      case Option::Slider: drawSlider(o,focused); break;
      case Option::Keys:   drawKeys(o,focused);   break;
      case Option::Text:   drawText(o,focused);   break;
      }
    }
  const Option* f = optFocus>=0 && size_t(optFocus)<options.size() ? &options[size_t(optFocus)] : nullptr;
  switch(optPage) {
    case 0: {
      // 0xd48, 0xd49: the device
      const Box head = {0.14375f*800,0.75f*600,0.64125f*800,0.7916667f*600};
      label(font26,str(T_Driver),{head.x0,head.y0,head.x1,head.y1-3},0,2,grey);
      underline(head.x0,head.x1,head.y1-2,grey);
      label(font20,driverName,{head.x0,0.8f*600,head.x1,0.8333333f*600},0,1,grey);
      break;
      }
    case 1: {
      // the preview of the cockpit: the whole, and the parts switched on over it, the focused one lit and blinking
      // (0.25 s, sin * 0.9 + 0.1)
      const Box shot = {CockShot[0],CockShot[1],CockShot[2],CockShot[3]};
      const Uv  uv   = {0,0,1,0.765625f};
      if(auto t = tex("opt_cock_all"))
        image(t,shot,uv,{1,1,1,1});
      static const std::pair<int,const char*> parts[] = {
        {0x2789,"opt_cock_a_"},{0x278a,"opt_cock_m_"},{0x278b,"opt_cock_l_"},{0x2788,"opt_cock_c_"},
        };
      const float ph = std::fmod(optFocusT,0.25f)/0.25f;
      const float blink = std::sin(ph*3.14159265f)*0.9f+0.1f;
      for(auto& [id,name]:parts) {
        const Option* o = nullptr;
        for(auto& c:options)
          if(c.id==id)
            o = &c;
        if(o==nullptr || o->e.at==0)
          continue;
        const bool lit = f==o;
        if(auto t = tex(std::string(name)+(lit ? "1" : "0")))
          image(t,shot,uv,{1,1,1,lit ? blink : 1.f});
        }
      label(font26,str(T_Preview),{CockName[0],CockName[1],CockName[2],CockName[3]-3},0,2,grey);
      underline(CockName[0],CockName[2],CockName[3]-2,grey);
      break;
      }
    case 2: {
      // the line between the music and the system's volume (dlgSOUND.ini line from 0.11875 to 0.6775)
      const float y = 0.2f*600+6*(RowH+RowGap)+RowH*0.5f;
      underline(0.11875f*800,0.6775f*800,y,grey);
      break;
      }
    case 3:
      // "Gracz #1", "Gracz #2" over their lines
      for(int p=0; p<2; ++p) {
        const Box h = {115,78+float(p)*220,115+396,78+25+float(p)*220};
        label(font26,str(T_Player+uint32_t(p)),{h.x0,h.y0,h.x1,h.y1-3},0,2,grey);
        underline(h.x0,h.x1,h.y1-2,grey);
        }
      break;
    case 4: {
      // the lines around the keys and, for the focused line, what its keys clash with of the other player's
      // (0x4c3b80): "Konflikty z graczem #N, klucz <action>" white, or "Brak konfliktów" grey
      underline(KeysLines[0],KeysLines[2],KeysLines[1],grey);
      underline(KeysLines[0],KeysLines[2],KeysLines[3],grey);
      if(f==nullptr || f->kind!=Option::Keys)
        break;
      const int   other = 1-ctrlPlayer;
      const auto& theirs = ctrlLayout[size_t(other)]==Controls::User ? ctrlUser[size_t(other)]
                                                                      : Controls::preset(ctrlLayout[size_t(other)]);
      std::string clash;
      for(auto& o:options) {
        if(o.kind!=Option::Keys || !clash.empty())
          continue;
        for(auto& k:theirs[o.action])
          for(auto& mine:f->keys)
            if(!mine.empty() && mine.key()!=0 && k.key()==mine.key() && clash.empty())
              clash = std::string(str(o.e.label));
        }
      const Box eb = {KeysExplain[0],KeysExplain[1],KeysExplain[2],KeysExplain[1]+25};
      if(clash.empty())
        label(font20,str(T_NoConflict),eb,0,1,grey);
      else
        label(font20,format(str(T_Conflict+uint32_t(other)),{clash}),eb,0,1,{1,1,1,1});
      break;
      }
    case 5:
      // 0x277e, 0x277f: the focused line's name and what it does
      if(f!=nullptr && f->help!=0) {
        const Box h = {0.14375f*800,0.65f*600,0.64125f*800,0.6916667f*600};
        label(font26,str(f->e.label),{h.x0,h.y0,h.x1,h.y1-3},0,2,{1,1,1,1});
        underline(h.x0,h.x1,h.y1-2,grey);
        lines(font20,str(f->help),{h.x0,0.7f*600,h.x1,600},0,22,grey);
        }
      break;
    case 6:
      label(font26,str(T_ChatHead),{ChatHead[0],ChatHead[1],ChatHead[2],ChatHead[3]-3},0,2,grey);
      underline(ChatHead[0],ChatHead[2],ChatHead[3]-2,grey);
      // the frame around the messages
      fill({ChatFrame[0],ChatFrame[1],ChatFrame[2],ChatFrame[1]+1},{Grey,Grey,Grey,0.5f});
      fill({ChatFrame[0],ChatFrame[3]-1,ChatFrame[2],ChatFrame[3]},{Grey,Grey,Grey,0.5f});
      fill({ChatFrame[0],ChatFrame[1],ChatFrame[0]+1,ChatFrame[3]},{Grey,Grey,Grey,0.5f});
      fill({ChatFrame[2]-1,ChatFrame[1],ChatFrame[2],ChatFrame[3]},{Grey,Grey,Grey,0.5f});
      break;
    }
  }

int Menu::typedChar(uint32_t ch) {
  // the Polish letters as Windows-1250 has them
  static const std::pair<uint32_t,uint8_t> polish[] = {
    {0x104,0xa5},{0x105,0xb9},{0x106,0xc6},{0x107,0xe6},{0x118,0xca},{0x119,0xea},{0x141,0xa3},{0x142,0xb3},
    {0x143,0xd1},{0x144,0xf1},{0xd3,0xd3},{0xf3,0xf3},{0x15a,0x8c},{0x15b,0x9c},{0x179,0x8f},{0x17a,0x9f},
    {0x17b,0xaf},{0x17c,0xbf},
    };
  if(ch>=0x20 && ch<0x7f)
    return int(ch);
  for(auto& [u,b]:polish)
    if(ch==u)
      return b;
  return -1;
  }

// "Autorzy" (0x4c3e40): the lines of Language/Game from 50001 on, pages ending with "$"; each page stays 6 s, then
// the next one comes over it in 0.5 s (0x4c4210); the logo in the top right corner

void Menu::buildCredits() {
  // 0x4c4160: "Twórcy", the lines up to the first one missing (of at most 100 letters, at most 200 lines)
  headerText = 43800;
  credits.clear();
  for(uint32_t i=0; i<200; ++i) {
    // the texts' table has no empty entries but those it has: the end is where there is none
    if(texts.all().count(50001+i)==0)
      break;
    credits.emplace_back(str(50001+i).substr(0,99));
    }
  creditsT     = 6.f;
  creditsShown = -1;
  creditsNext  = 0;
  }

bool Menu::creditsTick(float dt) {
  // 0x4c4210
  creditsT += dt;
  if(creditsT<=6.5f)
    return true;
  creditsT -= 6.5f;
  if(creditsNext<0) {
    creditsT = 0;
    return false;
    }
  creditsShown = creditsNext;
  creditsNext  = -1;
  // 0x4c44b0: the line after the page's "$"
  for(size_t i=size_t(creditsShown); i<credits.size(); ++i)
    if(!credits[i].empty() && credits[i][0]=='$') {
      creditsNext = int(i)+1;
      break;
      }
  if(creditsNext>=int(credits.size()))
    creditsNext = -1;
  return true;
  }

void Menu::drawCreditsPage(int first, float alpha) {
  // 0x4c43c0: from (112, 92), one line under another (Smash26, 5 units between); a line beginning with a space is a
  // name, the others are what they did, underlined
  if(first<0)
    return;
  const Color c = {1,1,1,alpha};
  float y = 92;
  for(size_t i=size_t(first); i<credits.size(); ++i) {
    const std::string& l = credits[i];
    if(!l.empty() && l[0]=='$')
      return;
    const float h = 26+1;   // the font's height (+0xcea of Smash26) and 1
    if(!l.empty()) {
      label(font26,l,{112,y,500,y+h},0,0,c);
      if(l[0]!=' ')
        underline(112,500,y+h-2,c);
      }
    y += h+5;
    }
  }

void Menu::drawCredits() {
  // the logo (0x56e980: 0.84 .. 1 of the width, 0 .. 0.213 of the height), by the right edge
  dx = wide;
  if(logo)
    image(logo,{0.84f*800,0,800,0.2133333f*600},{0,0,1,1},{1,1,1,1});
  dx = contentShift();
  dy = tall*0.5f;
  if(creditsT<=6.f) {
    drawCreditsPage(creditsShown,1);
    return;
    }
  const float k = std::clamp((creditsT-6.f)*2.f,0.f,1.f);
  drawCreditsPage(creditsShown,1.f-k);
  drawCreditsPage(creditsNext,k);
  }
