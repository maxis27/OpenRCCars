// The championship's screens of Menu: the tracks (dlgCHAMP), the race's fee (dlgCHRACE), the garage (dlgCARSCOMM,
// dlgSETCAR, dlgSETDETAIL), the congratulations and the bonus of the finish table, as RCCars.exe has them.
#include "menu.h"

#include <Tempest/Log>

#include <algorithm>
#include <cmath>

#include "formats/carprofile.h"
#include "formats/playerprofile.h"
#include "players.h"

using namespace Tempest;

namespace {

// Language/Game texts (RCCars.exe crsGetString ids)
enum : uint32_t {
  T_Yes          = 10000, // "Tak"
  T_Back         = 10003, // "Wróć"
  T_MainMenu     = 10006, // "Menu główne"
  T_Race         = 10007, // "Wyścig"
  T_No           = 10009, // "Nie"
  T_UpgradeBuy   = 10010, // "Ulepsz"
  T_NextSkin     = 10012, // "Następna skóra"
  T_BuyCar       = 10013, // "Kup samochód"
  T_MapInfo      = 10016, // "Mapa i informacje"
  T_TrackStats   = 10017, // "Statystyki toru"
  T_Championship = 10051, // "Mistrzostwa"
  T_Garage       = 10052, // "Garaż"
  T_NewChamp     = 10053, // "Nowe mistrzostwa"
  T_Training     = 10054, // "Trening"
  T_UpTires      = 10056, // "Ulepsz koła"
  T_UpBoost      = 10057, // "Ulepsz dopalacz"
  T_UpEngine     = 10058, // "Ulepsz silnik"
  T_ChooseCar    = 10059, // "Wybierz samochód"
  T_SellCar      = 10060, // "Sprzedaj samochód"
  T_UpgradeSell  = 10061, // "Sprzedaj"
  T_CantRace     = 10883, // "Nie można rozpocząć wyścigu"
  T_PlayerFileError = 10884,
  T_TrackName    = 40000, // + track
  T_CarName      = 40100, // + 4*car
  T_CarExplain   = 40102, // + 4*car
  T_PartName     = 40700, // "Dopalacz", "Silnik", "Koła"
  T_Price        = 40703, // "Cena"
  T_NotOffered   = 40704, // "Nie dostępne"
  T_Installed    = 40705, // "ZAINSTALOWANE"
  T_SellParts    = 40706, // "Sprzedaj części"
  T_SellFirst    = 40707, // "Musisz sprzedać\npoprzednie ulepszenie!"
  T_CashLeft     = 40709, // "Przypomnienie o forsie" (English "Cash remainder %s")
  T_HaveUpgrade  = 40710, // "Masz już to ulepszenie"
  T_NoCash       = 40712, // "Brak forsy"
  T_NoUpgrade    = 40713, // "Nie masz tego ulepszenia"
  T_Upgrades     = 40800, // "Posiadane ulepszenia"
  T_SellQ        = 40801, // "Chcesz sprzedać samochód lub ulepszenie?"
  T_CantSell     = 40806, // "Nie możesz tego zrobić..."
  T_CanBuy       = 40807, // "Możesz kupić ten samochód"
  T_NotOpen      = 40808, // "Nie dostępny"
  T_BuyPrice     = 40809, // "Cena kupna: %s"
  T_SellPrice    = 40810, // "Cena sprzedaży: %s"
  T_TooPoor      = 40814, // "Nie masz tyle forsy"
  T_BuyQ         = 40815, // "Chcesz kupić samochód?"
  T_InfoLeft     = 40816, // "Dopalacz:\nSilnik:\nOpony:"
  T_InfoRight    = 40817, // "%s\n%s\n%s"
  T_ChampHead    = 40900, // "Tor", "Min. wynik", "Koszt", "Poz.", "Cena"
  T_Closed       = 40905, // "xxx"
  T_NoPlace      = 40906, // "bd"
  T_Place1       = 40912, // "I", "II", "III"
  T_Place4       = 40922, // "IV", "V", "VI"
  T_TrainingFail = 40916, // "Nie można rozpocząć wyścigu"
  T_NoFee        = 40919, // "Brak ci forsy"
  T_NewChampQ    = 40927, // "Ostrzeżenie! Chcesz usunąć dane\n i rozpocząć nowe mistrzostwa?"
  T_NoAccess     = 40928, // "Nie masz dostępu do tej trasy\nZa mało punktów"
  T_NoAccessLast = 40929, // "... Wygraj na wszystkich trasach, aby dostać dostęp"
  T_TrackNeeds   = 40930, // "Wymaga punktów %i; opłata wejściowa %s."
  T_TrackPrizes  = 40931, // "Wygrana: 1sze %s 2gie %s 3cie %s"
  T_NotRaced     = 40932, // "Nie startowano"
  T_LastResult   = 40933, // "Rezultat poprzedniego wyścigu %s"
  T_FewScores    = 40934, // "Za mało punktów"
  T_WinAll       = 40935, // "Wygraj na wszystkich trasach, aby dostać dostęp"
  T_PrizeCash    = 41308, // "Nagroda w gotówce"
  T_RaceScore    = 41309, // "Wynik rajdu"
  T_HitBonus     = 41314, // "Bonus za uderzenia" (English "Hit bonus (%i hits):")
  T_GapBonus     = 41316, // "Bonus za dziurę (%i sek):"
  T_MaxBonus     = 41318, // "Maksymalny bonus" (English "Max bonus allowed: %s")
  T_NoPrize      = 41320, // "Nie ma nagrody pieniężnej"
  T_YouPay       = 41400, // "Płacisz"
  T_Remainder    = 41401, // "Przypomnienie o forsie"
  T_Track        = 41402, // "Tor"
  T_Prize        = 41403, // "Nagroda"
  T_Points       = 41404, // "punktów"
  T_RivalHead    = 41405, // "Gracz", "Samochód", "Silnik", "Dopalacz", "Koła"
  T_Level        = 41410, // "poziom %i"
  T_NoLevel      = 41411, // "brak"
  T_GarageCar    = 41500, // "Samochód"
  T_GarageCash   = 41501, // "Forsa"
  T_StockPart    = 41800, // "Dom." (booster, engine, tires)
  T_Congrats     = 44852, // "Gratulacje, %s"
  T_CongratsText = 44853,
  };

// the control ids of the championship's frame (0x4be150) and the garage (0x4c5f00, 0x4d4690, 0x4d5330)
enum : int {
  C_Race      = 0x76d,
  C_Champ     = 0x76f,
  C_MapInfo   = 0x770,
  C_Stats     = 0x771,
  C_Garage    = 0x772,
  C_NewChamp  = 0x773,
  C_Training  = 0x774,
  G_Car       = 0x9b0,   // the garage's pages: the car, then boost, engine, tires
  G_Skin      = 0x9b8,
  G_BuyCar    = 0x9b9,
  G_SellCar   = 0x9ba,
  G_UpBuy     = 0x9c4,
  G_UpSell    = 0x9c5,
  };

constexpr float Grey     = 0xd0/255.f;
constexpr float Inset    = 0.06f;      // the rows of a table start that much of its width in (0x4bd180)
constexpr float ScrollW  = 0.04f*800;

// dlgCHAMP.ini
constexpr float ChampTable[4] = {88,233,88+440,233+227};
constexpr float ChampCols[5]  = {0.2f,0.2f,0.17f,0.12f,0.31f};
constexpr float ChampMapName[4] = {115,455,115+413,455+40};
constexpr float ChampMapInfo[4] = {114,497,114+413,497+91};
// dlgCHRACE.ini
constexpr float RaceFrame[4]  = {10,18,10+782,18+573};
constexpr float RaceTable[4]  = {79,109,79+604,109+318};
constexpr float RaceCols[6]   = {0.09f,0.19f,0.21f,0.17f,0.17f,0.17f};
constexpr float RaceFaceIn    = 0.008f*800;
// dlgFINISH.ini: the bonus' table (two rows, the second empty), what the bonus may be, the prize, the total
constexpr float BonusTable[4] = {85,399,85+611,399+55};
constexpr float BonusCols[5]  = {0.32f,0.08f,0.13f,0.39f,0.08f};
constexpr float BonusCash[4]  = {173,320,173+456,320+108};

// the upgrades' pictures of the garage: upgr_<part><level>_<car>.csi, the part of them shown (0x4d4690)
const char* const UpgradeTex[3] = {"upgr_boost","upgr_reson","upgr_tires"};

size_t partIndex(Championship::Part p) {
  return p==Championship::Part::Boost ? 0 : p==Championship::Part::Resonator ? 1 : 2;
  }

int32_t levelOf(const rc::PlayerProfile::Car& c, size_t part) {
  return part==0 ? c.boost : part==1 ? c.resonator : c.tires;
  }

}

std::string Menu::format(std::string_view f, std::initializer_list<std::string> args) {
  // sprintf of the language's texts: a %s or %i (with its flags and width, ignored) takes the next value, the
  // values the text has no place for are left out
  std::string r;
  auto next = args.begin();
  for(size_t i=0; i<f.size(); ++i) {
    if(f[i]!='%' || i+1>=f.size()) {
      r.push_back(f[i]);
      continue;
      }
    if(f[i+1]=='%') {
      r.push_back('%');
      ++i;
      continue;
      }
    size_t k = i+1;
    while(k<f.size() && std::string_view("-+ #0123456789.l").find(f[k])!=std::string_view::npos)
      ++k;
    if(k<f.size() && std::string_view("sidu").find(f[k])!=std::string_view::npos) {
      if(next!=args.end())
        r += *next++;
      i = k;
      continue;
      }
    r.push_back(f[i]);
    }
  return r;
  }

// the tables

size_t Menu::tableRows(const Table& t) {
  // 0x4bcc60: the rows drawn while they end inside the table
  return size_t((1.f-t.head)/t.row+1e-4f);
  }

void Menu::drawTable(const Table& t, size_t rows, size_t top, int chosen,
                     const std::function<void(int,size_t,const Box&,const Color&)>& cell) {
  // class LST (0x4bcc60): the header standing on its line, the rows after the 0.06 inset with a marker before
  // them, red for the chosen one, a line under the table
  const Box&  b  = t.box;
  const float h  = b.y1-b.y0;
  const float ix = b.x0+(b.x1-b.x0)*Inset;
  const float iw = b.x1-ix;
  const Color white = {1,1,1,1}, grey = {Grey,Grey,Grey,1};
  auto columns = [&](int row, float y0, float y1, const Color& c) {
    float x = ix;
    for(size_t col=0; col<t.ncols; ++col) {
      const float w = iw*t.cols[col];
      cell(row,col,Box{x,y0,x+w,y1},c);
      x += w;
      }
    };
  const float hy = b.y0+h*t.head;
  columns(-1,b.y0,hy,white);
  underline(b.x0,b.x1,hy-0.0055f*600,grey);
  const size_t vis = tableRows(t);
  for(size_t r=top; r<rows && r<top+vis; ++r) {
    const float y0 = hy+float(r-top)*h*t.row;
    const float y1 = y0+h*t.row;
    const bool  on = int(r)==chosen;
    columns(int(r),y0,y1,on || !t.greyed ? white : grey);
    if(arrows) {
      const float mx = (b.x0+ix)*0.5f, my = (y0+y1)*0.5f+0.006f*600;
      image(arrows,{mx-7.2f,my-6.9f,mx+7.2f,my+6.9f},on ? Uv{0.5f,0,1,0.5f} : Uv{0.5f,0.5f,1,1},white);
      }
    }
  underline(b.x0,b.x1,b.y1-0.0055f*600,grey);
  }

int Menu::tableRow(const Table& t, float mx, float my, size_t rows, size_t top) const {
  const float h  = t.box.y1-t.box.y0;
  const float hy = t.box.y0+h*t.head;
  if(mx<t.box.x0 || mx>=t.box.x1 || my<hy)
    return -1;
  const int r = int((my-hy)/(h*t.row));
  if(r<int(tableRows(t)) && top+size_t(r)<rows)
    return int(top)+r;
  return -1;
  }

// the buttons

bool Menu::quickAllowed() const {
  if(players==nullptr)
    return true;
  auto* p = players->current();
  const size_t car = std::min<size_t>(choice.car,2);
  return p!=nullptr && p->cars[car].bought && players->trackOpen(choice.track,false);
  }

void Menu::refreshButtons() {
  if(players==nullptr || players->current()==nullptr)
    return;
  const auto&  p     = *players->current();
  const size_t car   = std::min<size_t>(choice.car,2);
  const bool   owned = p.cars[car].bought;
  for(auto& b:right) {
    if(b.kind==Button::Race && (current==Page::Main || current==Page::QuickRace))
      b.enabled = quickAllowed();
    if(current==Page::Champ && b.id==C_Training)
      b.enabled = players->trackOpen(choice.track,true);
    if(current!=Page::Garage)
      continue;
    // 0x4d5330 ..: what can be done with the car and the upgrade shown
    switch(b.id) {
      case G_Car+1: case G_Car+2: case G_Car+3:
        b.enabled = owned;
        break;
      case G_Skin:    b.enabled = owned;  break;
      case G_BuyCar:  b.enabled = !owned; break;
      case G_SellCar: b.enabled = owned;  break;
      case G_UpBuy:   b.enabled = partLevel(p)==int32_t(upgradeEnum.at);   break;
      case G_UpSell:  b.enabled = partLevel(p)==int32_t(upgradeEnum.at)+1; break;
      }
    }
  }

void Menu::buttonAction(int id) {
  if(current==Page::Players) {
    playerAction(id);
    return;
    }
  if(id==C_Garage) {
    // "Garaż" of the championship and of the quick race: back to where it was opened
    garageBack = current;
    leave(Page::Garage,Command::None);
    return;
    }
  if(current==Page::Champ)
    champAction(id);
  if(current==Page::Garage)
    garageAction(id);
  }

// Page::Champ

Menu::Table Menu::champTable() const {
  Table t;
  t.box   = {ChampTable[0],ChampTable[1],ChampTable[2],ChampTable[3]};
  t.cols  = ChampCols;
  t.ncols = std::size(ChampCols);
  t.head  = 0.16f;
  t.row   = 0.13f;
  return t;
  }

void Menu::buildChamp() {
  // the frame of 0x4be150 and the tracks' page (0x4be720)
  headerText = T_Championship;
  auto add = [this](int slot, uint32_t text, Button::Kind kind, int id, bool enabled) {
    Button b;
    b.slot    = slot;
    b.text    = text;
    b.kind    = kind;
    b.id      = id;
    b.enabled = enabled;
    right.push_back(b);
    return &right.back();
    };
  add(0,T_Championship,Button::Radio, C_Champ,   true)->checked = true;
  add(1,T_MapInfo,     Button::Radio, C_MapInfo, false);
  add(2,T_TrackStats,  Button::Radio, C_Stats,   false);
  add(3,T_Garage,      Button::Tab,   C_Garage,  players!=nullptr);
  add(4,T_NewChamp,    Button::Action,C_NewChamp,players!=nullptr);
  add(6,T_Training,    Button::Action,C_Training,false);
  add(8,T_MainMenu,    Button::Back,  0,         true)->to = Page::Main;
  add(-1,T_Race,       Button::Race,  C_Race,    players!=nullptr);

  // 0x4be720: a track not open gives way to the last of the open ones from the first
  if(players!=nullptr && !players->trackOpen(choice.track,true)) {
    size_t last = 0;
    while(last+1<TrackCount && players->trackOpen(last+1,true))
      ++last;
    choice.track = last;
    }
  const size_t vis = tableRows(champTable());
  if(choice.track<champTop)
    champTop = choice.track;
  if(choice.track>=champTop+vis)
    champTop = choice.track+1-vis;
  champHover = -1;
  refreshButtons();
  }

void Menu::champAction(int id) {
  if(players==nullptr || players->current()==nullptr)
    return;
  const auto&  p     = *players->current();
  const auto&  ch    = players->championship();
  const size_t track = std::min(choice.track,TrackCount-1);
  const size_t car   = std::min<size_t>(choice.car,2);
  switch(id) {
    case C_Race:
      // 0x4be9e0: open, the fee there, the car bought; then its fee and opponents (dlgCHRACE)
      if(!players->trackOpen(track,true)) {
        showMessage(track==TrackCount-1 && !Championship::allWon(p) ? T_NoAccessLast : T_NoAccess);
        return;
        }
      if(p.cash<ch.tracks[track].tariff) {
        showMessage(T_NoFee);
        return;
        }
      if(!p.cars[car].bought) {
        showMessage(T_CantRace);
        return;
        }
      choice.mode = Mode::Championship;
      leave(Page::ChampRace,Command::None);
      return;
    case C_Training:
      // 0x4be9e0: 3 laps against the easy ones, no fee
      if(!p.cars[car].bought) {
        showMessage(T_TrainingFail);
        return;
        }
      choice.mode = Mode::Training;
      command     = Command::Race;
      return;
    case C_NewChamp:
      showMessage(T_NewChampQ,Message::NewChampionship);
      return;
    }
  }

void Menu::champAnswer(Message::Action a) {
  auto* p = players->current();
  if(p==nullptr)
    return;
  const size_t car = std::min<size_t>(choice.car,2);
  switch(a) {
    case Message::None:
    case Message::RemovePlayer:
      return;
    case Message::NewChampionship: {
      // 0x4e8320, then 0x4beab9: the last of the cars the player has and the last of the tracks open from the
      // first are the ones chosen
      players->newChampionship();
      int32_t last = 0;
      for(int32_t i=0; i<int32_t(Championship::Cars); ++i)
        if(p->cars[size_t(i)].bought)
          last = i;
      size_t track = 0;
      while(track+1<TrackCount && players->trackOpen(track+1,true))
        ++track;
      p->lastCar   = last;
      p->lastTrack = int32_t(track);
      choice.car   = size_t(last);
      choice.track = track;
      champTop     = 0;
      if(!players->save(players->currentIndex()))
        showMessage(T_PlayerFileError);
      refreshButtons();
      return;
      }
    case Message::BuyCar:
      players->buyCar(car);
      buildGarage(garagePage);
      return;
    case Message::SellCar:
      players->sellCar(car);
      buildGarage(garagePage);
      return;
    case Message::BuyUpgrade:
      if(players->buyUpgrade(car,garagePart())) {
        blinkT    = 0;
        blinkPage = garagePage;
        }
      return;
    case Message::SellUpgrade:
      players->sellUpgrade(car,garagePart());
      blinkT = -1;
      return;
    }
  }

void Menu::drawChamp() {
  auto&        p     = *players->current();
  const auto&  ch    = players->championship();
  const Color  white = {1,1,1,1};
  drawPanel(p,true);

  // the tracks (0x4be0a0): the name, the scores it needs, its fee, the best place, the prizes
  auto placeText = [&](size_t track) -> std::string {
    if(!ch.open(p,track,true))
      return std::string(str(T_Closed));
    const auto& t = p.tracks[track];
    if(t.races<=0 || t.bestPlace<0 || t.bestPlace>5)
      return std::string(str(T_NoPlace));
    return std::string(str(t.bestPlace<3 ? T_Place1+uint32_t(t.bestPlace) : T_Place4+uint32_t(t.bestPlace-3)));
    };
  const Table t = champTable();
  drawTable(t,TrackCount,champTop,int(choice.track),[&](int row, size_t col, const Box& cb, const Color& c) {
    if(row<0) {
      label(font26,str(T_ChampHead+uint32_t(col)),cb,col==0 ? 0 : 1,1,white);
      return;
      }
    const size_t track = size_t(row);
    const auto&  info  = ch.tracks[track];
    switch(col) {
      case 0: {
        label(font20,str(T_TrackName+uint32_t(track)),cb,0,1,c);
        const float ly = cb.y1-((cb.y1-cb.y0)-font20.cell)*0.5f-0.0055f*600;
        underline(cb.x0,cb.x1,ly,c);
        break;
        }
      case 1: label(font20,std::to_string(info.access),cb,1,1,c); break;
      case 2: label(font20,money(info.tariff),cb,1,1,c);          break;
      case 3: label(font20,placeText(track),cb,1,1,c);            break;
      case 4:
        label(font20,std::to_string(info.prize[0])+"/"+std::to_string(info.prize[1])+"/"+std::to_string(info.prize[2]),
              cb,1,1,c);
        break;
      }
    });
  const float hy = t.box.y0+(t.box.y1-t.box.y0)*t.head;
  drawScrollBar(t.box.x1,hy,t.box.y1,TrackCount,tableRows(t),champTop,hoverUp,hoverDown);

  // the chosen track (0x4be630): its name on its line, then what it needs, what it pays and how it went
  const size_t track = std::min(choice.track,TrackCount-1);
  const auto&  info  = ch.tracks[track];
  const Box    name  = {ChampMapName[0],ChampMapName[1],ChampMapName[2],ChampMapName[3]};
  label(font26,str(T_TrackName+uint32_t(track)),{name.x0,name.y0,name.x1,name.y1-3},0,2,white);
  underline(name.x0,name.x1,name.y1-2,white);
  std::string last;
  const auto& pt = p.tracks[track];
  if(ch.open(p,track,true)) {
    if(pt.races>0 && pt.bestPlace>=0 && pt.bestPlace<=5)
      last = format(str(T_LastResult),{placeText(track)});
    else
      last = std::string(str(T_NotRaced));
    }
  else {
    last = std::string(str(track==TrackCount-1 && !Championship::allWon(p) ? T_WinAll : T_FewScores));
    }
  const std::string text = format(str(T_TrackNeeds),{std::to_string(info.access),money(info.tariff)})+"\n"+
                           format(str(T_TrackPrizes),{money(info.prize[0]),money(info.prize[1]),money(info.prize[2])})+
                           "\n"+last;
  lines(font20,text,{ChampMapInfo[0],ChampMapInfo[1],ChampMapInfo[2],ChampMapInfo[3]},2,22.5f,white);
  }

// Page::ChampRace

void Menu::fillRivals() {
  // 0x4c0b80: the opponents of the expert races of the track (AIRace<n> AI1 .. AI5 with HARD), their drivers
  rivals.clear();
  rc::AiLayout layout;
  try {
    const auto d = resources.pack().read("Scripts\\ailayouts.ini");
    layout = rc::AiLayout::parse(std::string_view(reinterpret_cast<const char*>(d.data()),d.size()));
    }
  catch(const std::exception& e) {
    Log::e("Scripts\\ailayouts.ini: ",e.what());
    return;
    }
  auto race = layout.races.find(rc::aiRaceNumber(trackName(choice.track)));
  if(race==layout.races.end())
    return;
  for(size_t i=0; i<race->second.opponents.size() && i<5; ++i) {
    const auto& o = race->second.opponents[i];
    if((o.races & rc::AiLayout::Hard)==0 || o.path.empty() || o.car<0 || o.car>2)
      continue;
    Rival r;
    r.car       = o.car;
    r.boost     = o.boost;
    r.resonator = o.reson;
    r.tires     = o.tires;
    r.name      = "AI"+std::to_string(i+1);
    if(auto d = layout.drivers.find(o.driver); d!=layout.drivers.end()) {
      const std::string& f = d->second.face;
      r.name    = f.substr(0,f.rfind('.'));
      if(auto t = str(uint32_t(40499+o.driver)); o.driver>0 && !t.empty())
        r.name = std::string(t);
      r.faceKey = "AIPlayer:"+f;
      r.face    = players!=nullptr ? players->faceByName(f) : nullptr;
      }
    rivals.push_back(std::move(r));
    }
  }

void Menu::champRacePill(size_t i) {
  if(slide!=0)
    return;
  if(i==1) {
    leave(Page::Champ,Command::None);
    return;
    }
  // 0x4c0b10 -> 0x4e7e70: the fee paid and written, then the race
  if(players==nullptr || !players->payTariff(std::min(choice.track,TrackCount-1))) {
    showMessage(T_NoFee);
    return;
    }
  choice.mode = Mode::Championship;
  command     = Command::Race;
  }

void Menu::drawChampRace() {
  // 0x4c1070: the menu darkened (0x5f000000), the frame, the table of who races and what, the fee and the prize
  quad(*white,1,1,{0,0,1,1},0,0,float(scrW),float(scrH),{0,0,0,0x5f/255.f});
  if(players==nullptr || players->current()==nullptr)
    return;
  auto&        p     = *players->current();
  const auto&  ch    = players->championship();
  const size_t track = std::min(choice.track,TrackCount-1);
  const size_t car   = std::min<size_t>(choice.car,2);
  const Color  white = {1,1,1,1};
  dx = wide*0.5f;
  dy = tall*0.5f;
  drawFrame({RaceFrame[0],RaceFrame[1],RaceFrame[2],RaceFrame[3]});

  Table t;
  t.box    = {RaceTable[0],RaceTable[1],RaceTable[2],RaceTable[3]};
  t.cols   = RaceCols;
  t.ncols  = std::size(RaceCols);
  t.head   = 0.1f;
  t.row    = 0.15f;
  t.greyed = false;
  auto level = [this](int32_t l) {
    return l<1 ? std::string(str(T_NoLevel)) : format(str(T_Level),{std::to_string(l)});
    };
  drawTable(t,1+rivals.size(),0,0,[&](int row, size_t col, const Box& cb, const Color& c) {
    if(row<0) {
      // "Gracz" over the pictures, nothing over the names, then "Samochód", "Silnik", "Dopalacz", "Koła"
      if(col==1)
        return;
      label(font26,str(col==0 ? T_RivalHead : T_RivalHead+uint32_t(col-1)),cb,col==0 ? 0 : 1,1,white);
      return;
      }
    const bool   me = row==0;
    const Rival* r  = me ? nullptr : &rivals[size_t(row-1)];
    const auto&  pc = p.cars[car];
    switch(col) {
      case 0:
        drawFaceCell(me ? face(p) : (r->face!=nullptr ? faceTexture(r->faceKey,*r->face,0) : nullptr),cb,RaceFaceIn);
        break;
      case 1: {
        const Font& f = me ? font26 : font20;
        label(f,me ? p.name : r->name,cb,0,1,c);
        const float ly = cb.y1-((cb.y1-cb.y0)-f.cell)*0.5f-0.0055f*600;
        underline(cb.x0,cb.x1,ly,c);
        break;
        }
      case 2: label(font20,str(T_CarName+4*uint32_t(me ? int32_t(car) : r->car)),cb,1,1,c); break;
      case 3: label(font20,level(me ? pc.resonator : r->resonator),cb,1,1,c); break;
      case 4: label(font20,level(me ? pc.boost     : r->boost),    cb,1,1,c); break;
      case 5: label(font20,level(me ? pc.tires     : r->tires),    cb,1,1,c); break;
      }
    });

  const auto& info = ch.tracks[track];
  label(font26,format("%s: \"%s\"",{std::string(str(T_Track)),std::string(str(T_TrackName+uint32_t(track)))}),
        {140,44,140+520,44+60},1,1,white);
  label(font26,format("%s: %s",{std::string(str(T_Prize)),money(info.prize[0])}),{140,48,140+520,48+60},2,2,white);
  label(font26,format("%s: %s",{std::string(str(T_YouPay)),money(info.tariff)}),{140,416,140+520,416+60},2,2,white);
  label(font20,format("(%s: %s)",{std::string(str(T_Remainder)),money(p.cash-info.tariff)}),
        {140,446,140+520,446+60},2,2,white);
  for(auto& pl:pills)
    drawPill(pl);
  dx = 0;
  dy = 0;
  }

// Page::Garage

Championship::Part Menu::garagePart() const {
  return garagePage==1 ? Championship::Part::Boost : garagePage==2 ? Championship::Part::Resonator
                                                                    : Championship::Part::Tires;
  }

int32_t Menu::partLevel(const rc::PlayerProfile& p) const {
  return levelOf(p.cars[std::min<size_t>(choice.car,2)],partIndex(garagePart()));
  }

void Menu::buildGarage(int page) {
  // 0x4c5f00: the car's page and the three upgrades' as radio buttons, the page's own buttons, "Wróć"
  right.clear();
  headerText = T_Garage;
  garagePage = std::clamp(page,0,3);
  auto add = [this](int slot, uint32_t text, Button::Kind kind, int id) {
    Button b;
    b.slot = slot;
    b.text = text;
    b.kind = kind;
    b.id   = id;
    right.push_back(b);
    return &right.back();
    };
  add(0,T_ChooseCar,Button::Radio,G_Car);
  add(1,T_UpBoost,  Button::Radio,G_Car+1);
  add(2,T_UpEngine, Button::Radio,G_Car+2);
  add(3,T_UpTires,  Button::Radio,G_Car+3);
  right[size_t(garagePage)].checked = true;
  if(garagePage==0) {
    add(4,T_NextSkin,Button::Action,G_Skin);
    add(6,T_BuyCar,  Button::Action,G_BuyCar);
    add(7,T_SellCar, Button::Action,G_SellCar);
    }
  else {
    add(6,T_UpgradeBuy, Button::Action,G_UpBuy);
    add(7,T_UpgradeSell,Button::Action,G_UpSell);
    }
  add(8,T_Back,Button::Back,0)->to = garageBack;

  const size_t car = std::min<size_t>(choice.car,2);
  // dlgSETCAR enumCar; dlgSETDETAIL's car (its arrows idle) and the upgrade's "<  >" under its picture
  garageCar.box    = {290,121,290+280,121+25};
  garageCar.split  = 0;
  garageCar.label  = 0;
  garageCar.values.clear();
  for(uint32_t i=0; i<Championship::Cars; ++i)
    garageCar.values.emplace_back(str(T_CarName+4*i));
  garageCar.at     = car;
  upgradeCar.box   = garageCar.box;
  upgradeCar.split = 0;
  upgradeCar.label = 0;
  upgradeCar.values = {garageCar.values[car]};
  upgradeCar.at    = 0;
  upgradeEnum.box    = {310,424,310+277,424+25};
  upgradeEnum.split  = 0;
  upgradeEnum.label  = 0;
  upgradeEnum.values = {"","",""};
  // 0x4d4c90: the next level to buy, the last when all are there
  if(garagePage>0 && players!=nullptr && players->current()!=nullptr)
    upgradeEnum.at = size_t(std::min(partLevel(*players->current()),2));
  refreshButtons();
  }

void Menu::garageAction(int id) {
  if(players==nullptr || players->current()==nullptr)
    return;
  const auto&  p   = *players->current();
  const auto&  ch  = players->championship();
  const size_t car = std::min<size_t>(choice.car,2);
  if(id>=G_Car && id<=G_Car+3) {
    if(id-G_Car!=garagePage) {
      blinkT = -1;
      buildGarage(id-G_Car);
      }
    return;
    }
  switch(id) {
    case G_Skin:
      // 0x4d62a0: the next of the four
      players->nextSkin(car,4);
      return;
    case G_BuyCar:
      // 0x4d6090
      if(p.cash<ch.cars[car].price)
        showMessage(T_TooPoor);
      else
        showMessage(T_BuyQ,Message::BuyCar,{},T_No);
      return;
    case G_SellCar: {
      // 0x4d5e40: not the last car when the money would buy no other
      const int32_t other = ch.cheapestOther(p,car);
      if(other>=0 && p.cash+ch.carSale(p,car)<other)
        showMessage(T_CantSell);
      else
        showMessage(T_SellQ,Message::SellCar,{},T_No);
      return;
      }
    case G_UpBuy:
    case G_UpSell:
      upgradeAction(id);
      return;
    }
  }

void Menu::upgradeAction(int id) {
  // 0x4d4d40 and its two others: buying the level shown, selling the one the car has
  const auto&   p     = *players->current();
  const auto&   ch    = players->championship();
  const size_t  car   = std::min<size_t>(choice.car,2);
  const auto    part  = garagePart();
  const int32_t view  = int32_t(upgradeEnum.at);
  const int32_t level = partLevel(p);
  if(id==G_UpBuy) {
    const int32_t price = ch.upgradePrice(car,part,view+1);
    if(level==view+1)
      showMessage(T_HaveUpgrade);
    else if(p.cash<price)
      showMessage(T_NoCash);
    else
      showMessage(T_CashLeft,Message::BuyUpgrade,format(str(T_CashLeft),{money(p.cash-price)}),T_No);
    return;
    }
  // 0x4d4d40 (boost) and the engine's go on to the question after "Nie masz tego ulepszenia", the tires' does not;
  // without any level there is nothing to sell
  if(level!=view+1) {
    showMessage(T_NoUpgrade);
    if(part==Championship::Part::Tires || level<1)
      return;
    }
  const int32_t sale = Championship::sale(ch.upgradePrice(car,part,level));
  showMessage(T_CashLeft,Message::SellUpgrade,format(str(T_CashLeft),{money(p.cash+sale)}),T_No);
  }

void Menu::drawGarage() {
  auto&        p     = *players->current();
  const auto&  ch    = players->championship();
  const size_t car   = std::min<size_t>(choice.car,2);
  const auto&  pc    = p.cars[car];
  const Color  white = {1,1,1,1}, grey = {Grey,Grey,Grey,1};

  // dlgCARSCOMM: the money, the car's label and its description
  label(font26,str(T_GarageCash),{115,75,115+185,75+25-3},0,2,white);
  underline(115,115+185,75+25-2,white);
  label(font26,money(p.cash),{315,80,315+120,80+25},0,2,white);
  label(font26,str(T_GarageCar),{115,108,115+185,108+25-3},0,2,white);
  underline(115,115+185,108+25-2,white);
  lines(font20,str(T_CarExplain+4*uint32_t(car)),{339,198,339+205,198+240},2,22.5f,grey);

  // the long names of the upgrades (0x49fd80, 0x4a01f0, 0x4a05e0): "Dom." without one
  auto longName = [&](size_t part, int32_t l) -> std::string {
    if(l<1)
      return std::string(str(T_StockPart+uint32_t(part)));
    const uint32_t base = part==0 ? 40112 : part==1 ? 40140 : 40170;
    return std::string(str(base+2*(3*uint32_t(car)+uint32_t(std::min(l,3)-1))));
    };
  auto description = [&](size_t part, int32_t l) -> std::string {
    const uint32_t base = part==0 ? 40112 : part==1 ? 40140 : 40170;
    return std::string(str(base+2*(3*uint32_t(car)+uint32_t(std::clamp(l,1,3)-1))+1));
    };

  if(garagePage==0) {
    // dlgSETCAR (0x4d54f0): the car, its upgrades, what it costs or brings, whether it can be bought
    drawEnum(garageCar,false);
    label(font26,str(T_Upgrades),{114,410,114+426,410+25-3},0,2,grey);
    underline(114,114+426,410+25-2,grey);
    lines(font20,str(T_InfoLeft),{241,439,241+320,439+100},0,22.5f,white);
    lines(font20,format(str(T_InfoRight),{longName(0,pc.boost),longName(1,pc.resonator),longName(2,pc.tires)}),
          {364,439,364+320,439+100},0,22.5f,white);
    const std::string price = pc.bought ? format(str(T_SellPrice),{money(ch.carSale(p,car))})
                                        : format(str(T_BuyPrice),{money(ch.cars[car].price)});
    label(font20,price,{342,336,342+200,336+50},2,2,white);
    if(!pc.bought)
      label(font26,str(p.cash>=ch.cars[car].price ? T_CanBuy : T_NotOpen),{85,350,85+260,350+50},1,1,white);
    return;
    }

  // dlgSETDETAIL (0x4d4f50): the upgrade shown, its picture and what can be done with it
  const size_t  part  = partIndex(garagePart());
  const int32_t view  = int32_t(upgradeEnum.at);
  const int32_t level = levelOf(pc,part);
  drawEnum(upgradeCar,false);
  const Box shot = {350,412,350+197,412+149};
  if(auto t = tex(std::string(UpgradeTex[part])+std::to_string(view+1)+"_"+std::to_string(car+1)))
    image(t,shot,{0.109375f,0.2109375f,0.88671875f,0.79296875f},white);
  drawEnum(upgradeEnum,false);
  label(font26,str(T_PartName+uint32_t(part)),{115,410,115+185,410+25-3},0,2,grey);
  underline(115,115+185,410+25-2,grey);
  std::string info = longName(part,view+1)+"\n"+description(part,view+1);
  std::string top, bottom;
  float topAlpha = 1;
  if(level<view) {
    top      = std::string(str(T_NotOffered));
    topAlpha = 0.5f;
    }
  else if(level==view) {
    info += "\n"+format("%s: %s",{std::string(str(T_Price)),money(ch.upgradePrice(car,garagePart(),view+1))});
    }
  else if(level==view+1) {
    top    = std::string(str(T_Installed));
    bottom = format(part==0 ? "%s %s" : "\n\n%s %s",
                    {std::string(str(T_SellParts)),money(Championship::sale(ch.upgradePrice(car,garagePart(),view+1)))});
    }
  else {
    bottom = std::string(str(T_SellFirst));
    }
  lines(font20,info,{105,437,105+200,437+125},2,22.5f,grey);
  label(font26,top,shot,1,0,{1,1,1,topAlpha});
  if(!bottom.empty()) {
    // from the bottom up
    const float step = 22.5f;
    const float n    = float(std::count(bottom.begin(),bottom.end(),'\n')+1);
    lines(font20,bottom,{shot.x0,shot.y1-n*step,shot.x1,shot.y1},1,step,white);
    }
  }

// Page::ChampWin

void Menu::drawChampWin() {
  // 0x4c1790: the player, "Gratulacje, <name>" on its line and the words under it, the car turning beside them
  auto&       p    = *players->current();
  const Color grey = {Grey,Grey,Grey,1};
  drawPanel(p,true);
  label(font26,format(str(T_Congrats),{p.name}),{112,253,522,286-3},0,2,grey);
  underline(112,522,286-2,grey);
  lines(font20,str(T_CongratsText),{112,340,330,515},0,22.5f,grey);
  }

Menu::Preview Menu::carLook(size_t car) const {
  // 0x4aca10: the player's car with its upgrades and skin; stock when the player has not got it (0x50bb60)
  Preview p;
  p.car = std::min<size_t>(car,2);
  if(players!=nullptr)
    if(auto* pl = players->current())
      if(pl->cars[p.car].bought) {
        const auto& c = pl->cars[p.car];
        p.boost     = c.boost;
        p.resonator = c.resonator;
        p.tires     = c.tires;
        p.skin      = c.skin;
        }
  return p;
  }

// the finish table in the championship

int32_t Menu::finishPlace() const {
  for(size_t i=0; i<finishRows.size(); ++i)
    if(finishRows[i].player)
      return int32_t(i);
  return -1;
  }

int32_t Menu::finishBonus() const {
  // 0x4c47e0 (0x14a3fa4): among the first three and not the last
  const int32_t place = finishPlace();
  if(choice.mode!=Mode::Championship || players==nullptr || place<0 || place>=3 ||
     size_t(place)+1>=finishRows.size())
    return 0;
  const float gap = finishRows[size_t(place)+1].time-finishRows[size_t(place)].time;
  return players->championship().bonus(std::min(choice.track,TrackCount-1),finishHits,gap).total;
  }

Menu::Page Menu::afterRace() const {
  // 0x4e07c0: the quick race's screen, the championship's, its congratulations after the last track
  if(choice.mode==Mode::Quick)
    return Page::QuickRace;
  if(choice.mode==Mode::Championship && championWon)
    return Page::ChampWin;
  return Page::Champ;
  }

void Menu::drawFinishBonus() {
  if(players==nullptr)
    return;
  const Color   white = {1,1,1,1};
  const auto&   ch    = players->championship();
  const size_t  track = std::min(choice.track,TrackCount-1);
  const auto&   prize = ch.tracks[track].prize;
  const int32_t place = finishPlace();
  const Box     cash  = {BonusCash[0],BonusCash[1],BonusCash[2],BonusCash[3]};
  if(place<0 || place>=3 || size_t(place)+1>=finishRows.size()) {
    label(font26,str(T_NoPrize),cash,1,1,white);
    return;
    }
  const float gap = finishRows[size_t(place)+1].time-finishRows[size_t(place)].time;
  const auto  b   = ch.bonus(track,finishHits,gap);

  // the bonus' row: the hits and their points, the gap and its points
  const Box   tb = {BonusTable[0],BonusTable[1],BonusTable[2],BonusTable[1]+(BonusTable[3]-BonusTable[1])*0.5f};
  const float ix = tb.x0+(tb.x1-tb.x0)*Inset;
  const float iw = tb.x1-ix;
  float x = ix;
  for(size_t c=0; c<std::size(BonusCols); ++c) {
    const float w  = iw*BonusCols[c];
    const Box   cb = {x,tb.y0,x+w,tb.y1};
    switch(c) {
      case 0:
        label(font20,format(str(T_HitBonus),{std::to_string(b.hits)}),{cb.x0,cb.y0,cb.x1,cb.y1-3},0,2,white);
        underline(cb.x0,cb.x1,cb.y1-2,white);
        break;
      case 1: label(font20,std::to_string(b.hitPoints),cb,1,2,white); break;
      case 3:
        label(font20,format(str(T_GapBonus),{std::to_string(b.gap)}),{cb.x0,cb.y0,cb.x1,cb.y1-3},0,2,white);
        underline(cb.x0,cb.x1,cb.y1-2,white);
        break;
      case 4: label(font20,std::to_string(b.gapPoints),cb,1,2,white); break;
      }
    x += w;
    }
  // what the bonus may be, the prize of the place, and the race's score: the number in the cockpit's font between
  // its words, the whole in the middle
  label(font26,format(str(T_MaxBonus),{money(prize[2])}),{5,445,5+788,485},1,1,white);
  label(font26,format("%s: %s",{std::string(str(T_PrizeCash)),money(prize[size_t(place)])}),cash,1,1,white);
  const std::string total = std::to_string(prize[size_t(place)]+b.total);
  const float       half  = textWidth(numbers.img.loaded ? numbers : font26,total,
                                      numbers.img.loaded ? numbers.cell : font26.cell)/scale*0.5f;
  label(font26,format("%s:",{std::string(str(T_RaceScore))}),{0,470,400-10-half,515},2,2,white);
  if(numbers.img.loaded)
    label(numbers,total,{400-half,480,400+half,525},1,1,white);
  else
    label(font26,total,{400-half,480,400+half,525},1,1,white);
  label(font20,format("$/%s",{std::string(str(T_Points))}),{400+10+half,470,800,515},0,2,white);
  }
