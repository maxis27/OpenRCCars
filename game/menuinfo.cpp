#include "menu.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "formats/playerprofile.h"
#include "players.h"

using namespace Tempest;

// the pages "Mapa i informacje" (dlgMAPINFO, 0x4caa20) and "Statystyki toru" (dlgSTAT, 0x4d8060) of the quick
// race's, the ghost race's and the championship's frames (their page tables at 0x56f58c, 0x56e040: the frame's own
// page, then these two)

namespace {

enum : uint32_t {
  T_TrackName    = 40000, // + track
  T_TrackInfo    = 40020, // + track: dlgSTAT's (0x4a8a00 the 4th)
  T_TrackStory   = 40030, // + track: dlgMAPINFO's (0x4a8a00 the 5th)
  T_CarName      = 40100, // + 4*car
  T_StatSort     = 40600, // "Posortuj wyniki według"
  T_StatPlayer   = 40601, // "Gracz"
  T_StatCar      = 40606, // "Samochód"
  T_StatNoCar    = 40611, // "n/d"
  T_StatNoTime   = 40612, // "n/d"
  T_StatClosed   = 40616, // "Nie dostępny"
  T_Shots        = 41200, // "Zdjęcia"
  T_MapClosed    = 41201, // "Zamknięty"
  };
// the kinds of results of enumStatType (0x56fa20): the column's header, the kind's name; 0 the championship's
// (only in its frame, flag 0x100 of 0x1499dd0), 1 the best lap, 2 .. 4 the best race of 3, 5, 7 laps
constexpr uint32_t StatHead[5] = {40614,40602,40603,40604,40605};
constexpr uint32_t StatKind[5] = {40615,40607,40608,40609,40610};

constexpr float Grey     = 0xd0/255.f;
constexpr float ScrollW  = 0.04f*800;
constexpr int   ShotCount = 5;            // shot_<track>_0 .. 4 (0x4a8ea0)
constexpr float ShotTime  = 0.17f;        // 0x4b9e90: the row moving to the next picture

// Settings\dlgMAPINFO.ini
constexpr float MapShot[4]    = {349,387,349+199,387+152};   // shotList: the chosen picture
constexpr float MapShotRow[4] = {25,402,686,478};             // shotListBound L, Up, R, Dn: the others
constexpr float MapShotSpace  = 5;
constexpr float MapShotsText[4] = {119,484,119+180,484+25};   // staticShortListText
constexpr float MapInfo[4]    = {361,174,361+230,174+156};    // staticTrackInfo
constexpr float MapView[4]    = {56,87,56+512,87+512};        // shotTrackView: shot_<track>_L
constexpr float MapClosed[4]  = {206,316,206+100,316+25};     // staticClosedInfo
// Settings\dlgSTAT.ini
constexpr float StatName[4]   = {115,85,115+215,85+25};       // staticTrackName
constexpr float StatInfo[4]   = {114,112,114+220,112+73};     // staticTrackInfo
constexpr float StatShot[4]   = {336,60,336+181,60+136};      // shotTrack
constexpr float StatSortBox[4]= {115,490,115+182,490+25};     // staticShowResult
constexpr float StatTable[4]  = {88,210,88+434,210+273};      // tableStat
constexpr float StatCols[3]   = {0.34f,0.30f,0.36f};          // 0x4d81a0
constexpr float StatClosed[4] = {377,160,377+100,160+25};     // staticClosedInfo

// "Beach_1" -> "shot_beach1"
std::string shotName(size_t track) {
  std::string s(Menu::trackName(track));
  s.erase(std::remove(s.begin(),s.end(),'_'),s.end());
  return "shot_"+s;
  }

}

bool Menu::infoFrame() const {
  return current==Page::QuickRace || current==Page::GhostRace || current==Page::Champ;
  }

bool Menu::trackOpenHere(size_t track) const {
  // 0x4e84a0 of the one playing: by IsOpened but in the championship (flag 0x100), else by the scores
  return players==nullptr || players->trackOpen(track,current==Page::Champ);
  }

void Menu::showInfo(int page) {
  // the radio buttons of the frame: the page shown changes, the buttons stay
  infoPage = std::clamp(page,0,2);
  infoEnums.clear();
  hoverUp = hoverDown = false;
  if(infoPage==0) {
    // back on the frame's own page: as the other pages left the track
    trackEnum.at = choice.track;
    if(current==Page::Champ)
      champTrackFix();
    refreshButtons();
    return;
    }
  if(infoPage==1) {
    // 0x4cabc0, 0x4cb080: the track's "<  >", the pictures' "<  >" (nothing between its arrows)
    Enum track;
    track.box   = {361,135,361+152,135+25};
    track.split = 0;
    for(size_t i=0; i<TrackCount; ++i)
      track.values.emplace_back(str(T_TrackName+uint32_t(i)));
    track.at    = choice.track;
    infoEnums.push_back(track);
    Enum shot;
    shot.box    = {312,492,312+272,492+35};
    shot.split  = 0;
    shot.values.assign(ShotCount,"");
    shot.at     = 0;
    infoEnums.push_back(shot);
    infoShotScroll = 0;
    return;
    }
  // 0x4d81a0: "Posortuj wyniki według" and its "<  >"; the championship's results only in its frame
  Enum kind;
  kind.box   = {319,504,319+166,504+25};
  kind.split = 0;
  for(size_t i=(current==Page::Champ ? 0 : 1); i<5; ++i)
    kind.values.emplace_back(str(StatKind[i]));
  kind.at    = 0;
  infoEnums.push_back(kind);
  fillStats();
  }

int Menu::statKind() const {
  if(infoPage!=2 || infoEnums.empty())
    return 1;
  return int(infoEnums[0].at)+(current==Page::Champ ? 0 : 1);
  }

Menu::Table Menu::statTable() const {
  Table t;
  t.box   = {StatTable[0],StatTable[1],StatTable[2],StatTable[3]};
  t.cols  = StatCols;
  t.ncols = std::size(StatCols);
  t.head  = 0.12f;
  t.row   = 0.1f;
  return t;
  }

const rc::PlayerProfile::Record* Menu::statRecord(const rc::PlayerProfile& p, int kind) const {
  // 0x4d8860: the record of the track at +0x164 the best lap, +0x190 the championship's, +0x240, +0x298, +0x2f0
  // the best races of 3, 5 and 7 laps
  const auto& t = p.tracks[std::min(choice.track,TrackCount-1)];
  switch(kind) {
    case 0:  return &t.champion;
    case 1:  return &t.bestLap;
    case 2:  return &t.bestRace[3];
    case 3:  return &t.bestRace[5];
    default: return &t.bestRace[7];
    }
  }

void Menu::fillStats() {
  // 0x4d86c0, 0x4d8860: every player, sorted by the kind chosen (0x4d8b80): the smaller time first, the more
  // money of the championship first, then by the name; the one playing chosen and in the middle
  statRows.clear();
  statTop = 0;
  if(players==nullptr)
    return;
  for(size_t i=0; i<players->size(); ++i)
    statRows.push_back(i);
  const int kind = statKind();
  auto lowerName = [](const std::string& s) {
    std::string r(s);
    for(auto& c:r)
      c = char(std::tolower(uint8_t(c)));
    return r;
    };
  std::stable_sort(statRows.begin(),statRows.end(),[&](size_t a, size_t b) {
    const auto& pa = (*players)[a];
    const auto& pb = (*players)[b];
    const float ta = statRecord(pa,kind)->time, tb = statRecord(pb,kind)->time;
    if(kind==0) {
      const int ia = int(ta), ib = int(tb);
      if(ia!=ib)
        return ia>ib;
      }
    else if(ta!=tb) {
      return ta<tb;
      }
    return lowerName(pa.name)<lowerName(pb.name);
    });
  const size_t cur = players->currentIndex();
  const auto   it  = std::find(statRows.begin(),statRows.end(),cur);
  if(it!=statRows.end()) {
    // 0x4d86c0: the row chosen, the list from half the rows above it, less one
    const int row = int(it-statRows.begin());
    const int vis = int(tableRows(statTable()));
    const int top = row-vis/2+1;
    statTop = size_t(std::clamp(top,0,std::max(0,int(statRows.size())-vis)));
    }
  }

void Menu::scrollStats(int step) {
  const int    vis = int(tableRows(statTable()));
  const size_t was = statTop;
  statTop = size_t(std::clamp(int(statTop)+step,0,std::max(0,int(statRows.size())-vis)));
  if(statTop!=was)
    playControl(2);
  }

void Menu::changeInfoShot(int step) {
  // 0x4cb4e0 -> 0x4b9e90: the row moves to the picture chosen
  if(infoEnums.size()<2)
    return;
  Enum& e = infoEnums[1];
  e.at = (e.at+ShotCount*4+size_t(step))%ShotCount;
  infoShotScroll = std::clamp(infoShotScroll+float(step),-2.f,2.f);
  }

void Menu::changeInfoEnum(Enum& e, int step) {
  if(infoPage==1 && !infoEnums.empty() && &e==&infoEnums[0]) {
    // 0x4cb2b0: another track, the pictures from its first; the frame's track too (0x4a4c50, 0x279)
    choice.track = e.at;
    infoEnums[1].at = 0;
    infoShotScroll  = 0;
    refreshButtons();
    return;
    }
  if(infoPage==1 && infoEnums.size()>1 && &e==&infoEnums[1]) {
    infoShotScroll = std::clamp(infoShotScroll+float(step),-2.f,2.f);
    return;
    }
  if(infoPage==2)
    fillStats();
  }

void Menu::infoTick(float dt) {
  if(infoShotScroll!=0) {
    const float s = dt/ShotTime;
    infoShotScroll = std::abs(infoShotScroll)<=s ? 0.f : infoShotScroll-std::copysign(s,infoShotScroll);
    }
  }

bool Menu::infoClick(float mx, float my) {
  if(infoPage==1) {
    // a picture beside the chosen one: that one comes to the middle
    const float h    = MapShotRow[3]-MapShotRow[1];
    const float side = h*(MapShot[2]-MapShot[0])/(MapShot[3]-MapShot[1])+MapShotSpace;
    for(int k:{-3,-2,-1,1,2}) {
      const float x0 = k<0 ? MapShot[0]-MapShotSpace+float(k)*side : MapShot[2]+MapShotSpace+float(k-1)*side;
      const Box   b  = {std::max(x0,MapShotRow[0]),MapShotRow[1],std::min(x0+side-MapShotSpace,MapShotRow[2]),
                        MapShotRow[3]};
      if(b.x0<b.x1 && b.contains(mx,my)) {
        playControl(2);
        changeInfoShot(k);
        return true;
        }
      }
    return false;
    }
  if(infoPage==2 && (hoverUp || hoverDown)) {
    scrollStats(hoverUp ? -1 : 1);
    return true;
    }
  return false;
  }

void Menu::infoHover(float mx, float my) {
  if(infoPage!=2) {
    hoverUp = hoverDown = false;
    return;
    }
  const Table t  = statTable();
  const float hy = t.box.y0+(t.box.y1-t.box.y0)*t.head;
  const bool  up   = Box{t.box.x1,hy,t.box.x1+ScrollW,hy+ScrollW*1.1875f}.contains(mx,my);
  const bool  down = Box{t.box.x1,t.box.y1-ScrollW*1.1875f,t.box.x1+ScrollW,t.box.y1}.contains(mx,my);
  if((up && !hoverUp) || (down && !hoverDown))
    playControl(1);
  hoverUp   = up;
  hoverDown = down;
  }

bool Menu::infoKey(Event::KeyType key) {
  if(infoPage==1 && infoEnums.size()>1) {
    if(key==Event::K_Left || key==Event::K_Right)
      changeEnum(infoEnums[1],key==Event::K_Left ? -1 : 1);
    if(key==Event::K_Up || key==Event::K_Down)
      changeEnum(infoEnums[0],key==Event::K_Up ? -1 : 1);
    return true;
    }
  if(infoPage==2 && !infoEnums.empty()) {
    if(key==Event::K_Left || key==Event::K_Right)
      changeEnum(infoEnums[0],key==Event::K_Left ? -1 : 1);
    if(key==Event::K_Up || key==Event::K_Down)
      scrollStats(key==Event::K_Up ? -1 : 1);
    return true;
    }
  return false;
  }

void Menu::drawInfo() {
  if(infoPage==1)
    drawMapInfo();
  if(infoPage==2)
    drawStats();
  }

void Menu::drawMapInfo() {
  const size_t track = std::min(choice.track,TrackCount-1);
  const Color  white = {1,1,1,1}, grey = {Grey,Grey,Grey,1};
  // shotTrackView: the map of the track, the whole texture
  if(const Tex m = tex(shotName(track)+"_L"))
    image(m,{MapView[0],MapView[1],MapView[2],MapView[3]},{0,0,1,1},white);

  // shotList (class SHS, 0x4b8fd0): the chosen picture, the others along a row going round, within its bounds
  const float h    = MapShotRow[3]-MapShotRow[1];
  const float w    = h*(MapShot[2]-MapShot[0])/(MapShot[3]-MapShot[1]);
  const float side = w+MapShotSpace;
  const Box   clip = {MapShotRow[0],0,MapShotRow[2],600};
  const int   at   = infoEnums.size()>1 ? int(infoEnums[1].at) : 0;
  const float sc   = infoShotScroll*side;
  for(int k=-4; k<=3; ++k) {
    if(k==0)
      continue;
    const int   shot = ((at+k)%ShotCount+ShotCount)%ShotCount;
    const float x0   = (k<0 ? MapShot[0]-MapShotSpace+float(k)*side : MapShot[2]+MapShotSpace+float(k-1)*side)+sc;
    drawShot(track,shot,{x0,MapShotRow[1],x0+w,MapShotRow[3]},1.f,&clip);
    }
  const float cross = 1.f-std::min(1.f,std::abs(infoShotScroll));
  drawShot(track,at,{MapShot[0],MapShot[1],MapShot[2],MapShot[3]},std::max(0.25f,cross));

  // staticShortListText "Zdjęcia" on its line, the pictures' "<  >"
  const Box st = {MapShotsText[0],MapShotsText[1],MapShotsText[2],MapShotsText[3]};
  label(font26,str(T_Shots),{st.x0,st.y0,st.x1,st.y1-3},0,2,white);
  underline(st.x0,st.x1,st.y1-2,white);
  for(auto& e:infoEnums)
    drawEnum(e,false);
  // staticTrackInfo: the track's story (grey); staticClosedInfo "Zamknięty" on a track not open (0x4cb2b0)
  lines(font20,str(T_TrackStory+uint32_t(track)),{MapInfo[0],MapInfo[1],MapInfo[2],MapInfo[3]},0,22.5f,grey);
  if(!trackOpenHere(track))
    label(font20,str(T_MapClosed),{MapClosed[0],MapClosed[1],MapClosed[2],MapClosed[3]},1,1,white);
  }

void Menu::drawStats() {
  const size_t track = std::min(choice.track,TrackCount-1);
  const Color  white = {1,1,1,1};
  // the track: its name on its line, what it is, its first picture (0x4d86c0)
  drawShot(track,0,{StatShot[0],StatShot[1],StatShot[2],StatShot[3]},1.f);
  label(font26,str(T_TrackName+uint32_t(track)),{StatName[0],StatName[1],StatName[2],StatName[3]-3},0,2,white);
  underline(StatName[0],StatName[2],StatName[3]-2,white);
  lines(font20,str(T_TrackInfo+uint32_t(track)),{StatInfo[0],StatInfo[1],StatInfo[2],StatInfo[3]},0,22.5f,white);
  if(!trackOpenHere(track))
    label(font20,str(T_StatClosed),{StatClosed[0],StatClosed[1],StatClosed[2],StatClosed[3]},1,1,white);

  // "Posortuj wyniki według" and its "<  >"
  const Box sb = {StatSortBox[0],StatSortBox[1],StatSortBox[2],StatSortBox[3]};
  label(font26,str(T_StatSort),{sb.x0,sb.y0,sb.x1,sb.y1-3},0,2,white);
  underline(sb.x0,sb.x1,sb.y1-2,white);
  for(auto& e:infoEnums)
    drawEnum(e,false);

  // tableStat: the player, the result of the kind, the car it was made with; "n/d" for none (10000 and more)
  if(players==nullptr)
    return;
  const int   kind   = statKind();
  const Table t      = statTable();
  const size_t cur   = players->currentIndex();
  int chosen = -1;
  for(size_t i=0; i<statRows.size(); ++i)
    if(statRows[i]==cur)
      chosen = int(i);
  drawTable(t,statRows.size(),statTop,chosen,[&](int row, size_t col, const Box& cb, const Color& c) {
    if(row<0) {
      const uint32_t head = col==0 ? T_StatPlayer : col==1 ? StatHead[kind] : T_StatCar;
      label(font26,str(head),cb,col==0 ? 0 : 1,1,white);
      return;
      }
    const auto& p = (*players)[statRows[size_t(row)]];
    const auto* r = statRecord(p,kind);
    const bool  none = r->time>=10000.f;
    switch(col) {
      case 0: {
        label(font20,p.name,cb,0,1,c);
        const float ly = cb.y1-((cb.y1-cb.y0)-font20.cell)*0.5f-0.0055f*600;
        underline(cb.x0,cb.x1,ly,c);
        break;
        }
      case 1:
        if(none)
          label(font20,str(T_StatNoTime),cb,1,1,c);
        else
          label(font20,kind==0 ? std::to_string(int(r->time)) : formatTime(r->time),cb,1,1,c);
        break;
      case 2:
        label(font20,none ? str(T_StatNoCar) : str(T_CarName+4*uint32_t(std::clamp(r->car,0,2))),cb,1,1,c);
        break;
      }
    });
  const float hy = t.box.y0+(t.box.y1-t.box.y0)*t.head;
  drawScrollBar(t.box.x1,hy,t.box.y1,statRows.size(),tableRows(t),statTop,hoverUp,hoverDown);
  }
