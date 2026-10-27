#include "menu.h"

#include <Tempest/Log>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "formats/settings.h"

using namespace Tempest;

// "Wielu graczy" (RCCars.exe 0x4cd930) and "Dwóch graczy": the race's setup (0x4d8d30 with its "Podsumowanie",
// 0x4d8fe0, and the options' two pages of the controls) and the two players' cars (0x4bfa80). What is chosen is
// kept in RCCars.crs TwoPlayers; "Wyścig" starts mode 9 or 10 on the split screen (2PLEntry).

namespace {

enum : uint32_t {
  T_Back        = 10003, // "Wróć"
  T_MainMenu    = 10006, // "Menu główne"
  T_Race        = 10007, // "Wyścig"
  T_NextSkin    = 10012, // "Następna skóra"
  T_Summary     = 10015, // "Podsumowanie"
  T_Controls    = 10033, // "Sterowanie"
  T_Keys        = 10035, // "Dostosuj sterowanie"
  T_Next        = 10069, // "Dalej"
  T_TwoPlayers  = 10130, // "Dwóch graczy"
  T_Multiplayer = 10131, // "Wielu graczy"
  T_CreateGame  = 10132, // "Utwórz grę"
  T_JoinGame    = 10133, // "Dołącz do gry"
  T_TrackName   = 40000, // + track
  T_TrackInfo   = 40020, // + track: the quick race's description
  T_TrackBlurb  = 40040, // + track: the main menu's one
  T_CarName     = 40100, // + 4*car; + 3 what it is
  T_Car         = 41500, // "Samochód"
  T_RaceMode    = 42800, // "Tryb wyścigu:", "Okrążeń:", "Maks opóźnienie:"
  T_NLaps       = 42801,
  T_MaxDist     = 42802,
  T_ModeLaps    = 42803, // "Okrążenia", "Opóźnienia"
  T_ModeLag     = 42804,
  T_LapsFmt     = 42805, // "%i", "%i m"
  T_MetreFmt    = 42806,
  T_ExplainLaps = 42808, // "Okrążenia:\n Konwencjonalny wyścig", "Opóźnienia\n Zdobądź przewagę"
  T_ExplainLag  = 42809,
  T_Part        = 43400, // "Dopalacz:", "Silnik:", "Opony:"
  T_PartLevel   = 43403, // "dom.", "Poz. 1" .. "Poz. 3"
  };

// the control ids
enum : int {
  M_Two     = 0xedd,                            // "Wielu graczy": "Dwóch graczy"
  P_Next    = 0x1069,                           // "Dwóch graczy": "Dalej", its pages
  P_Summary = 0x106b, P_Controls = 0x106c, P_Keys = 0x106d,
  C_Race    = 0x15e1, C_Skin1 = 0x15e3, C_Skin2 = 0x15e4,   // the cars
  };

constexpr float Grey = 0xd0/255.f;

// 0x4cd930: each picture 4 s (0x5543f0)
constexpr float MultiShot = 4.f;

// "Okrążeń:" (0x56fbd4) and "Maks opóźnienie:" (0x56fbc8)
constexpr int LapsList[]  = {1,2,3,4,5};
constexpr int MetreList[] = {200,400,600};

// dlgTWOPLAYERS_CHOOSE2CARS.ini, dlgCARSCOMM_SPLIT.ini: the second player's 202 lower
constexpr float Shift2 = 202;

const char* const CarKey[2]   = {"car1","car2"};
const char* const PartKey[3][2] = {{"boost1","boost2"},{"reson1","reson2"},{"tires1","tires2"}};
const char* const SkinKey[2]  = {"skin1","skin2"};

std::vector<uint8_t> readCrs(const std::filesystem::path& p) {
  std::ifstream f(p,std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());
  }

}

// Page::Multi

void Menu::buildMulti() {
  // 0x4cd930: the network's two (not ours: greyed), "Dwóch graczy", no race; the player's panel
  headerText = T_Multiplayer;
  auto add = [this](int slot, uint32_t text, Button::Kind kind, int id, Page to, bool enabled) {
    Button b;
    b.slot    = slot;
    b.text    = text;
    b.kind    = kind;
    b.id      = id;
    b.to      = to;
    b.enabled = enabled;
    right.push_back(b);
    };
  add(0,T_CreateGame, Button::Tab, 0xedb,Page::None,false);
  add(1,T_JoinGame,   Button::Tab, 0xedc,Page::None,false);
  add(3,T_TwoPlayers, Button::Tab, M_Two,Page::None,true);
  add(8,T_MainMenu,   Button::Back,0,    Page::Main,true);
  add(-1,T_Race,      Button::Race,0,    Page::None,false);
  }

void Menu::multiTick(float dt) {
  if(current!=Page::Multi)
    return;
  if(multiScroll!=0) {
    const float step = dt/0.3f;
    multiScroll = std::abs(multiScroll)<=step ? 0.f : multiScroll-std::copysign(step,multiScroll);
    }
  multiT += dt;
  if(multiT<=MultiShot)
    return;
  // the next picture moving in; after the track's four the next track, and one time of five, when the pictures
  // were shown, a car in their place (the next car each time the pictures come back)
  multiT      = 0;
  multiAt     = (multiAt+1)%(TrackCount*4);
  multiScroll = std::clamp(multiScroll+1.f,-2.f,2.f);
  if(++multiCount<4)
    return;
  multiCount = 0;
  if(!multiShowCar && std::rand()%5<1) {
    multiShowCar = true;
    carAlpha     = 0;
    return;
    }
  multiShowCar = false;
  multiCar     = (multiCar+1)%3;
  }

void Menu::drawMulti() {
  // dlgMULTIPLAYER: the pictures, or the car (its name and what it is, 0x4ce360) instead; the track's name and
  // the main menu's text of it (0x4ce2d0)
  if(multiShowCar) {
    const uint32_t c = T_CarName+4*uint32_t(multiCar);
    drawShowcase(str(c),str(c+3));
    return;
    }
  drawShotRow(multiAt,multiScroll);
  const uint32_t t = uint32_t(multiAt/4);
  drawShowcase(str(T_TrackName+t),str(T_TrackBlurb+t));
  }

// RCCars.crs TwoPlayers

void Menu::loadTwo() {
  const auto crs = rc::ConfigFile::parse(readCrs(resources.gameDir()/"RCCars.crs"));
  for(int i=0; i<2; ++i) {
    auto& r = choice.riders[size_t(i)];
    r.car       = size_t(std::clamp(crs.integer("TwoPlayers",CarKey[i],0),0,2));
    r.boost     = std::clamp(crs.integer("TwoPlayers",PartKey[0][i],0),0,3);
    r.resonator = std::clamp(crs.integer("TwoPlayers",PartKey[1][i],0),0,3);
    r.tires     = std::clamp(crs.integer("TwoPlayers",PartKey[2][i],0),0,3);
    r.skin      = std::clamp(crs.integer("TwoPlayers",SkinKey[i],0),0,3);
    }
  // 0x4a4ff0: "lagType" 2 the laps ("fixedNLaps"), else the gap ("lagMetre"); 0x4d8d30 mode 9 or 10
  const int32_t lag = crs.integer("TwoPlayers","lagType",2);
  choice.mode     = lag==2 ? Mode::TwoLaps : Mode::TwoLag;
  choice.twoLaps  = crs.integer("TwoPlayers","fixedNLaps",1);
  choice.lagMetre = float(crs.integer("TwoPlayers","lagMetre",200));
  }

void Menu::saveTwo() {
  auto data = readCrs(resources.gameDir()/"RCCars.crs");
  if(data.empty())
    return;
  auto put = [&](const char* key, int32_t v) {
    data = rc::ConfigFile::withInteger(data,"TwoPlayers",key,v);
    };
  for(int i=0; i<2; ++i) {
    const auto& r = choice.riders[size_t(i)];
    put(CarKey[i],int32_t(r.car));
    put(PartKey[0][i],r.boost);
    put(PartKey[1][i],r.resonator);
    put(PartKey[2][i],r.tires);
    put(SkinKey[i],r.skin);
    }
  // 0x4a5060: the one of the race chosen, then its kind
  if(choice.mode==Mode::TwoLag)
    put("lagMetre",int32_t(choice.lagMetre));
  else
    put("fixedNLaps",choice.twoLaps);
  put("lagType",choice.mode==Mode::TwoLag ? 1 : 2);
  // what the options' page shown has of the file goes on with this
  if(!optCrs.empty())
    optCrs = data;
  const auto path = resources.gameDir()/"RCCars.crs";
  std::ofstream f(path,std::ios::binary|std::ios::trunc);
  f.write(reinterpret_cast<const char*>(data.data()),std::streamsize(data.size()));
  if(!f)
    Log::e("Two players: can't write RCCars.crs");
  }

// Page::TwoPlayers

void Menu::buildTwo() {
  // 0x4d8d30: its pages as radio buttons, "Dalej" to the cars, back to "Wielu graczy"
  headerText = T_TwoPlayers;
  auto add = [this](int slot, uint32_t text, Button::Kind kind, int id, Page to) {
    Button b;
    b.slot = slot;
    b.text = text;
    b.kind = kind;
    b.id   = id;
    b.to   = to;
    right.push_back(b);
    return &right.back();
    };
  add(0,T_Summary, Button::Radio,P_Summary, Page::None)->checked = true;
  add(1,T_Controls,Button::Radio,P_Controls,Page::None);
  add(2,T_Keys,    Button::Radio,P_Keys,    Page::None);
  add(8,T_Back,    Button::Back, 0,         Page::Multi);
  add(-1,T_Next,   Button::Race, P_Next,    Page::None);
  twoTab = 0;
  optCrs.clear();
  options.clear();
  loadTwo();

  // dlgTWOPLAYERS_RACESUM.ini: the track as the quick race's, the kind of the race, its laps or its gap
  trackEnum.box   = {71,191,71+273,191+25};
  trackEnum.split = 0;
  trackEnum.values.assign(TrackCount,"");
  trackEnum.at    = choice.track;

  Enum mode;
  mode.box    = {115,267,115+380,267+25};
  mode.split  = 0.52f;
  mode.label  = T_RaceMode;
  mode.values = {std::string(str(T_ModeLaps)),std::string(str(T_ModeLag))};
  mode.at     = choice.mode==Mode::TwoLag ? 1 : 0;
  enums.push_back(mode);

  Enum laps;
  laps.box   = {115,308,115+380,308+25};
  laps.split = 0.52f;
  laps.label = T_NLaps;
  for(int n:LapsList)
    laps.values.push_back(format(str(T_LapsFmt),{std::to_string(n)}));
  laps.at    = 0;
  for(size_t i=0; i<std::size(LapsList); ++i)
    if(LapsList[i]==choice.twoLaps)
      laps.at = i;
  choice.twoLaps = LapsList[laps.at];
  enums.push_back(laps);

  Enum dist  = laps;
  dist.label = T_MaxDist;
  dist.values.clear();
  for(int m:MetreList)
    dist.values.push_back(format(str(T_MetreFmt),{std::to_string(m)}));
  dist.at    = 0;
  for(size_t i=0; i<std::size(MetreList); ++i)
    if(float(MetreList[i])==choice.lagMetre)
      dist.at = i;
  choice.lagMetre = float(MetreList[dist.at]);
  enums.push_back(dist);
  }

void Menu::buildTwoCars() {
  // 0x4bfa80: each player's "Następna skóra" (slots 0 and 5), the race, back to "Dwóch graczy"
  headerText = T_TwoPlayers;
  auto add = [this](int slot, uint32_t text, Button::Kind kind, int id, Page to) {
    Button b;
    b.slot = slot;
    b.text = text;
    b.kind = kind;
    b.id   = id;
    b.to   = to;
    right.push_back(b);
    };
  add(0,T_NextSkin,Button::Action,C_Skin1,Page::None);
  add(5,T_NextSkin,Button::Action,C_Skin2,Page::None);
  add(8,T_Back,    Button::Back,  0,      Page::TwoPlayers);
  add(-1,T_Race,   Button::Race,  C_Race, Page::None);
  loadTwo();
  // enumCar (no label), then enumBoost0, enumReson0, enumTires0 (0x4c0300: "dom.", "Poz. 1" ..)
  for(int i=0; i<2; ++i) {
    const float d = float(i)*Shift2;
    const auto& r = choice.riders[size_t(i)];
    auto& e = twoCars[size_t(i)];
    e[0] = Enum();
    e[0].box   = {315,95+d,315+231,95+25+d};
    e[0].split = 0;
    for(uint32_t c=0; c<3; ++c)
      e[0].values.emplace_back(str(T_CarName+4*c));
    e[0].at    = r.car;
    const int32_t lv[3] = {r.boost,r.resonator,r.tires};
    for(size_t k=0; k<3; ++k) {
      auto& p = e[k+1];
      p = Enum();
      const float y = 148+40*float(k)+d;
      p.box   = {313,y,313+233,y+25};
      p.split = 0.42f;
      p.label = T_Part+uint32_t(k);
      for(uint32_t l=0; l<4; ++l)
        p.values.emplace_back(str(T_PartLevel+l));
      p.at    = size_t(lv[k]);
      }
    }
  }

void Menu::twoAction(int id) {
  switch(id) {
    case M_Two:
      leave(Page::TwoPlayers,Command::None);
      return;
    case P_Summary:
    case P_Controls:
    case P_Keys: {
      // 0x4d8f00: the page shown writes what it has, the next one reads it
      if(optionsOn())
        saveOptions();
      twoTab = id==P_Summary ? 0 : id==P_Controls ? 3 : 4;
      if(twoTab>0)
        buildOptions(twoTab);
      return;
      }
    case P_Next:
      leave(Page::TwoCars,Command::None);
      return;
    case C_Skin1:
    case C_Skin2: {
      // 0x4bfbd0: the next of the four skins
      auto& r = choice.riders[id==C_Skin1 ? 0 : 1];
      r.skin  = (r.skin+1)%4;
      return;
      }
    case C_Race:
      // 0x4bfa80 message 0x26d: the cars kept, the race of the mode chosen (2PLEntry)
      saveTwo();
      command = Command::Race;
      return;
    }
  }

void Menu::changeTwoEnum(Enum& e) {
  if(current==Page::TwoPlayers) {
    // 0x4d95d0, 0x4d94a0
    if(enums.size()<3)
      return;
    if(&e==&enums[0])
      choice.mode = e.at==1 ? Mode::TwoLag : Mode::TwoLaps;
    if(&e==&enums[1])
      choice.twoLaps = LapsList[std::min(e.at,std::size(LapsList)-1)];
    if(&e==&enums[2])
      choice.lagMetre = float(MetreList[std::min(e.at,std::size(MetreList)-1)]);
    return;
    }
  for(size_t i=0; i<2; ++i) {
    auto& r = choice.riders[i];
    auto& c = twoCars[i];
    if(&e==&c[0]) {
      r.car    = e.at;
      carAlpha = 0;
      }
    if(&e==&c[1]) r.boost     = int32_t(e.at);
    if(&e==&c[2]) r.resonator = int32_t(e.at);
    if(&e==&c[3]) r.tires     = int32_t(e.at);
    }
  }

std::vector<Menu::Enum*> Menu::twoEnums() {
  std::vector<Enum*> r;
  if(current==Page::TwoPlayers && twoTab==0 && enums.size()>=3) {
    // the laps' or the gap's, as the kind of the race is (0x4d95d0)
    r = {&trackEnum,&enums[0],&enums[enums[0].at==1 ? 2 : 1]};
    }
  if(current==Page::TwoCars)
    for(auto& p:twoCars)
      for(auto& e:p)
        r.push_back(&e);
  return r;
  }

void Menu::drawTwo() {
  // dlgTWOPLAYERS_RACESUM: the track's picture, "<  >", name and description, as the quick race's (0x4d9710)
  const Color white = {1,1,1,1};
  const Color grey  = {Grey,Grey,Grey,1};
  drawShot(choice.track,0,{110,82,110+196,82+156},1.f);
  drawEnum(trackEnum,false);
  label(font26,str(T_TrackName+uint32_t(choice.track)),{323,70,323+197,70+40},0,2,white);
  underline(323,323+197,70+40,white);
  lines(font20,str(T_TrackInfo+uint32_t(choice.track)),{323,115,323+219,115+80},0,22.5f,white);
  if(enums.size()<3)
    return;
  const bool lag = enums[0].at==1;
  drawEnum(enums[0],true);
  drawEnum(enums[lag ? 2 : 1],true);
  // staticRaceMode and staticExplain (0x4d95d0)
  label(font26,str(lag ? T_ModeLag : T_ModeLaps),{114,364,114+419,364+25-3},0,2,grey);
  underline(114,114+419,364+25-2,grey);
  lines(font20,str(lag ? T_ExplainLag : T_ExplainLaps),{329,391,329+190,391+100},0,22.5f,white);
  }

void Menu::drawTwoCars() {
  // each player's "Samochód" (staticCarNameSplit) over its "<  >"; the cars turn behind them (carPreviews)
  const Color white = {1,1,1,1};
  for(size_t i=0; i<2; ++i) {
    const float d = float(i)*Shift2;
    label(font26,str(T_Car),{115,84+d,115+185,84+25+d-3},0,2,white);
    underline(115,115+185,84+25+d-2,white);
    drawEnum(twoCars[i][0],false);
    for(size_t k=1; k<4; ++k)
      drawEnum(twoCars[i][k],true);
    }
  }
