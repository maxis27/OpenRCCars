#include "menu.h"

#include <Tempest/Log>

#include <algorithm>
#include <fstream>
#include <iterator>

#include "formats/cp1250.h"
#include "formats/settings.h"
#include "players.h"

using namespace Tempest;

// The network game's screens: "Utwórz grę" (RCCars.exe 0x4ce3e0), "Dołącz do gry" (0x4cf960) and the waiting room
// "Oczekujący gracze" (0x4d9ab0). Each is a frame whose radio buttons show its pages (the handlers' table 0x56b29c,
// 0x56b374, 0x56ac70); a page builds its controls from a table (0x56f130, 0x56f308, 0x570130 ..) placed by its
// Settings\dlg*.ini, reads RCCars.crs (sections Network and Multiplayer) or the player's file when it comes (message
// 0xe) and writes them when it goes (message 0xf). The session itself is MainWindow's (netplay.cpp): the screens see
// it through NetView and ask through NetRequest.

namespace {

enum : uint32_t {
  T_Yes          = 10000, // "Tak"
  T_Back         = 10003, // "Wróć"
  T_Race         = 10007, // "Wyścig"
  T_No           = 10009, // "Nie"
  T_Summary      = 10015, // "Podsumowanie"
  T_MapInfo      = 10016, // "Mapa i informacje"
  T_Options      = 10044, // "Opcje"
  T_Abort        = 10062, // "Przerwij"
  T_Create       = 10063, // "Utwórz"
  T_Connect      = 10064, // "Połącz"
  T_Ready        = 10066, // "Gotów"
  T_Disconnect   = 10068, // "Odłącz"
  T_CreateGame   = 10132, // "Utwórz grę"
  T_JoinGame     = 10133, // "Dołącz do gry"
  T_NetOptions   = 10135, // "Opcje sieciowe", "Informacje o graczu" (create)
  T_PlayerInfo   = 10136,
  T_JNetOptions  = 10138, // the same of "Dołącz do gry", then "Informacje o serwerze"
  T_JPlayerInfo  = 10139,
  T_ServerInfo   = 10140,
  T_Waiting      = 10141, // "Oczekujący gracze"
  T_ServerRC     = 410,   // "Serwer RC": the session's name when none is typed (0x4cef17)
  T_TrackName    = 40000, // + track
  T_CarName      = 40100, // + 4*car
  T_SrvName      = 42200, // "Nazwa serwera:", "Protokół:", "Port nmb:", "Maks opóźnienie:", "Adres IP:",
  T_Protocol     = 42201, // "Maks graczy:", "BPS:"
  T_Port         = 42202,
  T_MaxLag       = 42203,
  T_IpAddr       = 42204,
  T_MaxPlayers   = 42205,
  T_Bps          = 42206,
  T_Default      = 42207, // "domyślny"
  T_AllThere     = 42208, // "wsz. dostępne"
  T_Link         = 42216, // + bps: "Połączenie: wolny modem" ..
  T_Lag          = 42221, // + i: "10 ms" .. "1000 ms"
  T_BpsLevel     = 42226, // + bps: "Najniższy" .. "Najwyższy"
  T_Nick         = 42300, // "Nick:", "Kolor rozmówek:", "Opcje rozmówek:"
  T_ChatColor    = 42301,
  T_ChatOptions  = 42303,
  T_CantCreate   = 42418, // "Nie można utworzyć GRY"
  T_Connecting   = 42451, // "Łączenie..."
  T_JSrvName     = 42500, // "Nazwa serwera:", "Protokół:", "Port nmb:"
  T_JProtocol    = 42501,
  T_JPort        = 42502,
  T_ColServer    = 42503, // "Serwer", "Trasa", "Graczy", "Okr.", "Opóźnienie"
  T_Refresh      = 42508, // "Odśwież"
  T_TcpIp        = 42509, // "TCP/IP"
  T_Sort         = 42511, // + kind: "Sortuj wg nazwy serwera" .. "Sortuj wg opóźnienia"
  T_JNick        = 42600, // "Nick:", "Kolor rozmówek:"
  T_JChatColor   = 42601,
  T_SiName       = 42700, // "Nazwa serwera:" .. "OPÓŹNIENIE:", "%i ms"
  T_SiMs         = 42707,
  T_CarSetup     = 42900, // "Ustawienia samochodu", "Rozmówki", "Ogranicz. samochodu", "Wykop gracza",
  T_Chat         = 42901, // "Liczba okrążeń", ">", "Mapa", "Nie możesz się sam wykopać"
  T_CarLimits    = 42902,
  T_Kick         = 42903,
  T_NLaps        = 42904,
  T_Prompt       = 42905,
  T_Map          = 42906,
  T_NoSelfKick   = 42907,
  T_ColPlayer    = 42908, // "Gracz", "Samochód", -, "Dopalacz", "Silnik", "Opony", "Stan", "gotowy", "nie gotowy"
  T_IsReady      = 42915,
  T_NotReady     = 42916,
  T_LeaveQ       = 42930, // "Czy na pewno chcesz się odłączyć?"
  T_Colors       = 43102, // + i: "Czerwony" .. "Żółty"
  T_NotAllReady  = 43251, // "Nie wszyscy gracze są gotowi lub\nprzekraczają ograniczenia samochodów"
  T_Alone        = 43252, // "Nie możesz wystartować sam!"
  T_PartLevel    = 43403, // "dom.", "Poz. 1" ..
  };

// the control ids of the original
enum : int {
  C_Create   = 0xfa1, C_CreateOpts = 0xfa3, C_CreatePlayer = 0xfa4,
  C_Connect  = 0x1005, C_JoinOpts = 0x1007, C_JoinPlayer = 0x1008, C_JoinServer = 0x1009,
  C_Refresh  = 0x11ff, C_Sort = 0x1200,
  C_WaitRace = 0x13ed, C_WaitLeave = 0x13ee, C_WaitSummary = 0x13ef,
  C_Kick     = 0x1456,
  // the lines
  L_Name = 0x10cc, L_Protocol = 0x10cd, L_Port = 0x10ce, L_MaxLag = 0x10cf, L_IpAddr = 0x10d0, L_MaxPlayers = 0x10d1,
  L_Bps  = 0x10d6, L_Nick = 0x1130, L_Color = 0x1131, L_Laps = 0x1450, L_Track = 0x1451, L_Chat = 0x1455,
  L_Info = 0x1500,   // ours: the lines of "Informacje o serwerze"
  };

constexpr float Grey = 0xd0/255.f;

// 0x4cec47: "maks. opóźnienie" kept in ms; 0x56f210: the most players each connection may have; 0x4dce76: the laps
constexpr int32_t LagList[5]    = {10,50,100,500,1000};
constexpr int     MaxPerBps[5]  = {2,3,6,6,6};
constexpr int     LapsList[3]   = {3,5,7};
// 0x4cf850: the nick, 0x4dc6ae: a line of the chat
constexpr size_t  NickMax       = 10;
constexpr size_t  ChatMax       = 60;
constexpr size_t  NameMax       = 32;   // ours: the session's name (the original's buffer has 127)

// 0x571710 (0xRRGGBB)
constexpr uint32_t ChatRgb[Menu::ChatColors] = {0xf54545,0xf5b515,0x1cff1c,0x00ffff,0xb5b33b,0x8368b3,0x00b5a1,
                                                 0x4343ff,0xff48ff,0xffff23};

// Settings\dlgNETJOIN_OPTS.ini, dlgWAITPLAYERS_RACESUM.ini: the tables (tableWidth%i in percent, the last one the rest)
constexpr float GamesBox[4] = {87,220,87+440,220+290};
constexpr float GamesCols[5] = {0.22f,0.20f,0.26f,0.13f,0.19f};
constexpr float WaitBox[4]  = {86,174,86+466,174+195};
constexpr float WaitCols[6] = {0.19f,0.26f,0.13f,0.13f,0.13f,0.16f};
constexpr float ChatBox[4]  = {119,386,119+425,386+90};

std::vector<uint8_t> readCrs(const std::filesystem::path& p) {
  std::ifstream f(p,std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());
  }

}

Menu::Color Menu::chatColor(int32_t i) {
  if(i<0 || size_t(i)>=ChatColors)
    return {1,1,1,1};
  const uint32_t c = ChatRgb[i];
  return {float((c>>16)&0xff)/255.f,float((c>>8)&0xff)/255.f,float(c&0xff)/255.f,1};
  }

void Menu::buildNet() {
  auto add = [this](int slot, uint32_t text, Button::Kind kind, int id, Page to, bool enabled = true) {
    Button b;
    b.slot    = slot;
    b.text    = text;
    b.kind    = kind;
    b.id      = id;
    b.to      = to;
    b.enabled = enabled;
    right.push_back(b);
    return &right.back();
    };
  netTab = 0;
  netRow = -1;
  options.clear();
  optCrs.clear();
  switch(current) {
    case Page::NetCreate:
      // 0x4ce3e0: "Opcje sieciowe", "Informacje o graczu"; "Utwórz" (0xfa1), "Wróć" (0xfa2)
      headerText = T_CreateGame;
      add(0,T_NetOptions,Button::Radio,C_CreateOpts,Page::None)->checked = true;
      add(1,T_PlayerInfo,Button::Radio,C_CreatePlayer,Page::None);
      add(8,T_Back,      Button::Back, 0,           Page::Multi);
      add(-1,T_Create,   Button::Race, C_Create,    Page::None);
      break;
    case Page::NetJoin:
      // 0x4cf960: the pages 0x1007 .. 0x1009; "Połącz" (0x1005), "Wróć" (0x1006)
      headerText = T_JoinGame;
      add(0,T_JNetOptions,Button::Radio,C_JoinOpts,  Page::None)->checked = true;
      add(1,T_JPlayerInfo,Button::Radio,C_JoinPlayer,Page::None);
      add(2,T_ServerInfo, Button::Radio,C_JoinServer,Page::None);
      add(8,T_Back,       Button::Back, 0,           Page::Multi);
      add(-1,T_Connect,   Button::Race, C_Connect,   Page::None);
      break;
    case Page::NetWait: {
      // 0x4d9ab0: the pages 0x13ef .. 0x13f4, "Wyścig" for the host and "Gotów" for the others (0x13ed),
      // "Odłącz" (0x13ee). Ours has the summary only yet: the others greyed
      headerText = T_Waiting;
      add(0,T_Summary,  Button::Radio,C_WaitSummary,  Page::None)->checked = true;
      add(1,T_MapInfo,  Button::Radio,C_WaitSummary+1,Page::None,false);
      add(2,T_CarSetup, Button::Radio,C_WaitSummary+2,Page::None,false);
      add(3,T_Chat,     Button::Radio,C_WaitSummary+3,Page::None,false);
      add(4,T_CarLimits,Button::Radio,C_WaitSummary+4,Page::None,false);
      add(5,T_Options,  Button::Radio,C_WaitSummary+5,Page::None,false);
      add(8,T_Disconnect,Button::Back,C_WaitLeave,    Page::None);
      add(-1,netView.host ? T_Race : T_Ready,Button::Race,C_WaitRace,Page::None);
      netReady = false;
      break;
      }
    default:
      return;
    }
  showNetTab(0);
  }

void Menu::showNetTab(int tab) {
  saveNetTab();
  netTab   = tab;
  options.clear();
  optFocus = -1;
  optWait  = -1;
  optEdit  = -1;
  optPage  = -1;   // none of the options' own pages
  options.reserve(12);
  optCrs = readCrs(resources.gameDir()/"RCCars.crs");
  const auto crs = rc::ConfigFile::parse(optCrs);
  rc::PlayerProfile* p = players!=nullptr ? players->current() : nullptr;
  // "Sortuj wg ..." and "Odśwież" belong to the games' page
  right.erase(std::remove_if(right.begin(),right.end(),[](const Button& b){ return b.id==C_Sort || b.id==C_Refresh; }),
              right.end());

  auto line = [&](Option::Kind kind, int id, uint32_t text, float x0, float y, float w, float split) -> Option& {
    Option o;
    o.kind    = kind;
    o.id      = id;
    o.e.box   = {x0,y,x0+w,y+25};
    o.e.label = text;
    o.e.split = split;
    o.e.inner = 0.8f;
    options.push_back(std::move(o));
    return options.back();
    };
  auto text = [&](int id, uint32_t label, float x0, float y, float w, float split, std::string v) -> Option& {
    auto& o = line(Option::Text,id,label,x0,y,w,split);
    o.text = std::move(v);
    return o;
    };
  auto shown = [&](int id, uint32_t label, float y, std::string v) -> Option& {
    auto& o = text(id,label,114,y,400,0.5f,std::move(v));
    o.readOnly = true;
    return o;
    };
  auto port = [&](float y, uint32_t label) {
    const int32_t n = crs.integer("Network","portNum",0);
    auto& o  = text(L_Port,label,114,y,400,0.5f,n>0 ? std::to_string(n) : std::string());
    o.digits = true;
    o.maxLen = 5;
    o.empty  = T_Default;
    };
  auto playerLines = [&](float y, uint32_t nick, uint32_t color, float w, float split) {
    // 0x4cf580, 0x4cf7c0: the nick (the player's name when it has none) and the chat's colour, the player's own
    auto& n = text(L_Nick,nick,114,y,w,split,p!=nullptr ? (p->nick.empty() ? p->name : p->nick) : std::string());
    n.maxLen = NickMax;
    if(n.text.size()>NickMax)
      n.text.resize(NickMax);
    auto& c = line(Option::Choice,L_Color,color,114,y+40,w,split);
    for(uint32_t i=0; i<ChatColors; ++i)
      c.e.values.emplace_back(str(T_Colors+i));
    c.e.at = p!=nullptr ? size_t(std::clamp(p->chatColor,0,int32_t(ChatColors)-1)) : 0;
    };

  if(current==Page::NetCreate && tab==0) {
    // dlgNETCREATE_OPTS (0x4ce9d0): the session's name, the port, the protocol, the lag allowed, the players, this
    // computer's address, the connection
    auto& name = text(L_Name,T_SrvName,114,100,400,0.5f,crs.string("Network","sessionName"));
    name.maxLen = NameMax;
    if(name.text.empty())
      name.text = std::string(str(T_ServerRC));
    if(name.text.size()>NameMax)
      name.text.resize(NameMax);
    port(140,T_Port);
    shown(L_Protocol,T_Protocol,180,std::string(str(T_TcpIp)));
    auto& lag = line(Option::Choice,L_MaxLag,T_MaxLag,114,220,400,0.5f);
    for(uint32_t i=0; i<5; ++i) {
      lag.e.values.emplace_back(str(T_Lag+i));
      lag.stored.push_back(LagList[i]);
      }
    const int32_t ms = crs.integer("Multiplayer","maxLag",1000);
    lag.e.at = 4;
    for(size_t i=0; i<5; ++i)
      if(LagList[i]==ms)
        lag.e.at = i;
    line(Option::Choice,L_MaxPlayers,T_MaxPlayers,114,260,400,0.5f);
    auto& ip = shown(L_IpAddr,T_IpAddr,300,std::string());
    ip.e.split = 0.51f;
    auto& bps = line(Option::Choice,L_Bps,T_Bps,114,380,400,0.5f);
    for(uint32_t i=0; i<5; ++i) {
      bps.e.values.emplace_back(str(T_BpsLevel+i));
      bps.stored.push_back(int32_t(i));
      }
    bps.e.at = size_t(std::clamp(crs.integer("Multiplayer","bpsLevel",4),0,4));
    netChanged(bps);
    }
  if(current==Page::NetJoin && tab==0) {
    // dlgNETJOIN_OPTS (0x4d08b0): the address asked ("wsz. dostępne": everyone on the local network), the protocol,
    // the port; under them the games found
    auto& at = text(L_Name,T_JSrvName,114,100,400,0.5f,crs.string("Network","serverName"));
    at.maxLen = 60;
    at.empty  = T_AllThere;
    shown(L_Protocol,T_JProtocol,140,std::string(str(T_TcpIp)));
    port(180,T_JPort);
    // "Sortuj wg ..." (0x1200) and "Odśwież" (0x11ff) in slots 4 and 5 while this page is shown (0x4d07e3)
    Button sort;
    sort.slot = 4; sort.text = T_Sort+uint32_t(netSort); sort.kind = Button::Action; sort.id = C_Sort;
    Button again;
    again.slot = 5; again.text = T_Refresh; again.kind = Button::Action; again.id = C_Refresh;
    right.push_back(sort);
    right.push_back(again);
    // the search starts anew (0x4d0a10)
    NetRequest r;
    r.kind = NetRequest::Search;
    r.text = rc::cp1250::toUtf8(at.text);
    r.port = uint16_t(crs.integer("Network","portNum",0));
    netRequests.push_back(std::move(r));
    }
  if(current==Page::NetCreate && tab==1)
    playerLines(300,T_Nick,T_ChatColor,360,0.5f);
  if(current==Page::NetJoin && tab==1)
    playerLines(240,T_JNick,T_JChatColor,400,0.51f);
  if(current==Page::NetJoin && tab==2) {
    // dlgNETJOIN_SERVINFO (0x4d1330): what the game chosen told
    const NetView::Game* g = nullptr;
    for(auto& o:netView.games)
      if(o.address+":"+std::to_string(o.port)==netChosen)
        g = &o;
    auto v = [&](auto f) { return g!=nullptr ? f(*g) : std::string(); };
    shown(L_Info+0,T_SiName+0,100,v([](const NetView::Game& g){ return g.name; }));
    shown(L_Info+1,T_SiName+1,140,v([](const NetView::Game& g){ return std::to_string(g.players); }));
    shown(L_Info+2,T_SiName+2,180,v([](const NetView::Game& g){ return std::to_string(g.maxPlayers); }));
    shown(L_Info+3,T_SiName+3,220,v([this](const NetView::Game& g){
      return g.track<TrackCount ? std::string(str(T_TrackName+uint32_t(g.track))) : std::string(); }));
    shown(L_Info+4,T_SiName+4,260,v([](const NetView::Game& g){ return g.laps>0 ? std::to_string(g.laps) : std::string(); }));
    shown(L_Info+5,T_SiName+5,300,v([this](const NetView::Game& g){
      return format(str(T_SiMs),{std::to_string(g.ping)}); }));
    }
  if(current==Page::NetWait && tab==0) {
    // dlgWAITPLAYERS_RACESUM (0x4dc590): the track and the laps (the host's to choose), the players, the chat
    auto& track = line(Option::Choice,L_Track,T_Map,115,87,360,0.47f);
    for(uint32_t i=0; i<TrackCount; ++i)
      track.e.values.emplace_back(str(T_TrackName+i));
    track.e.at      = std::min(netView.track,TrackCount-1);
    track.e.enabled = netView.host;
    auto& laps = line(Option::Choice,L_Laps,T_NLaps,115,124,360,0.47f);
    for(int n:LapsList) {
      laps.e.values.push_back(std::to_string(n));
      laps.stored.push_back(n);
      }
    laps.e.at = 0;
    for(size_t i=0; i<3; ++i)
      if(LapsList[i]==netView.laps)
        laps.e.at = i;
    laps.e.enabled = netView.host;
    // the chat's line: "<nick>>" before it (0x4dc64f)
    auto& chat = text(L_Chat,0,121,490,443,0.24f,std::string());
    chat.maxLen = ChatMax;
    if(p!=nullptr)
      chat.labelText = (p->nick.empty() ? p->name : p->nick)+std::string(str(T_Prompt));
    }
  }

void Menu::saveNetTab() {
  if(!netPage() || options.empty())
    return;
  std::vector<uint8_t> data = optCrs.empty() ? readCrs(resources.gameDir()/"RCCars.crs") : optCrs;
  if(data.empty())
    return;
  const auto before = data;
  rc::PlayerProfile* p = players!=nullptr ? players->current() : nullptr;
  bool profile = false;
  for(auto& o:options) {
    switch(o.id) {
      case L_Name:
        // 0x4ceee0: an empty name is "Serwer RC"; "Dołącz do gry": the address asked (0x4d0cd0)
        if(current==Page::NetCreate)
          data = rc::ConfigFile::withString(data,"Network","sessionName",
                                            o.text.empty() ? std::string(str(T_ServerRC)) : o.text,127);
        else
          data = rc::ConfigFile::withString(data,"Network","serverName",o.text,127);
        break;
      case L_Port:
        data = rc::ConfigFile::withInteger(data,"Network","portNum",std::atoi(o.text.c_str()));
        break;
      case L_MaxLag:
        data = rc::ConfigFile::withInteger(data,"Multiplayer","maxLag",o.stored[o.e.at]);
        break;
      case L_Bps:
        data = rc::ConfigFile::withInteger(data,"Multiplayer","bpsLevel",o.stored[o.e.at]);
        break;
      case L_MaxPlayers:
        if(o.e.at<o.stored.size())
          data = rc::ConfigFile::withInteger(data,"Network","maxPlayersNum",o.stored[o.e.at]);
        break;
      case L_Nick:
        // 0x4cf850: the player's file
        if(p!=nullptr && p->nick!=o.text) {
          p->nick = o.text.empty() ? p->name : o.text;
          profile = true;
          }
        break;
      case L_Color:
        if(p!=nullptr && p->chatColor!=int32_t(o.e.at)) {
          p->chatColor = int32_t(o.e.at);
          profile = true;
          }
        break;
      }
    }
  if(profile)
    players->save(players->currentIndex());
  if(data==before)
    return;
  optCrs = data;
  std::ofstream f(resources.gameDir()/"RCCars.crs",std::ios::binary|std::ios::trunc);
  f.write(reinterpret_cast<const char*>(data.data()),std::streamsize(data.size()));
  if(!f)
    Log::e("network: can't write RCCars.crs");
  }

void Menu::netChanged(Option& o) {
  if(o.id==L_Bps) {
    // 0x4cf0a0: the connection decides how many players may come (one of five "Maks graczy:" shown)
    auto* mp = option(L_MaxPlayers);
    if(mp==nullptr)
      return;
    const int most = MaxPerBps[std::min<size_t>(o.e.at,4)];
    int32_t want = mp->stored.empty() ? int32_t(-1) : mp->stored[mp->e.at];
    if(want<0)
      want = rc::ConfigFile::parse(optCrs).integer("Network","maxPlayersNum",6);
    mp->e.values.clear();
    mp->stored.clear();
    for(int n=2; n<=most; ++n) {
      mp->e.values.push_back(std::to_string(n));
      mp->stored.push_back(n);
      }
    mp->e.at = size_t(std::clamp(want,2,most)-2);
    return;
    }
  if(o.id==L_Track || o.id==L_Laps) {
    // 0x4dc960: the host's choice goes to everyone
    const auto* t = option(L_Track);
    const auto* l = option(L_Laps);
    if(t==nullptr || l==nullptr)
      return;
    NetRequest r;
    r.kind  = NetRequest::Setup;
    r.track = t->e.at;
    r.laps  = l->stored[l->e.at];
    netRequests.push_back(r);
    }
  }

bool Menu::netSubmit(Option& o) {
  if(o.id==L_Chat) {
    // Enter says it (0x4dcb2f); the line stays to be typed in
    if(!o.text.empty()) {
      NetRequest r;
      r.kind = NetRequest::Chat;
      r.text = o.text;
      netRequests.push_back(std::move(r));
      o.text.clear();
      }
    return true;
    }
  if(o.id==L_Name && current==Page::NetJoin) {
    // the address typed: asked at once
    optEdit = -1;
    NetRequest r;
    r.kind = NetRequest::Search;
    r.text = rc::cp1250::toUtf8(o.text);
    const auto* pt = option(L_Port);
    r.port = uint16_t(pt!=nullptr ? std::atoi(pt->text.c_str()) : 0);
    netRequests.push_back(std::move(r));
    saveNetTab();
    return true;
    }
  return false;
  }

std::vector<size_t> Menu::netGameOrder() const {
  // "Sortuj wg ..." (0x4d0070): the server's name, the track, the players (the most first), the laps, the lag
  std::vector<size_t> order(netView.games.size());
  for(size_t i=0; i<order.size(); ++i)
    order[i] = i;
  const auto& g = netView.games;
  std::stable_sort(order.begin(),order.end(),[&](size_t a, size_t b) {
    switch(netSort) {
      case 0:  return g[a].name<g[b].name;
      case 1:  return g[a].track<g[b].track;
      case 2:  return g[a].players>g[b].players;
      case 3:  return g[a].laps<g[b].laps;
      default: return g[a].ping<g[b].ping;
      }
    });
  return order;
  }

Menu::Table Menu::netGameTable() const {
  Table t;
  t.box   = {GamesBox[0],GamesBox[1],GamesBox[2],GamesBox[3]};
  t.cols  = GamesCols;
  t.ncols = std::size(GamesCols);
  t.head  = 0.09f;
  t.row   = 0.10f;
  return t;
  }

Menu::Table Menu::netWaitTable() const {
  Table t;
  t.box    = {WaitBox[0],WaitBox[1],WaitBox[2],WaitBox[3]};
  t.cols   = WaitCols;
  t.ncols  = std::size(WaitCols);
  t.head   = 0.13f;
  t.row    = 0.14f;
  t.greyed = false;
  return t;
  }

void Menu::netAction(int id) {
  switch(id) {
    case C_CreateOpts:
    case C_CreatePlayer:
      showNetTab(id-C_CreateOpts);
      return;
    case C_JoinOpts:
    case C_JoinPlayer:
    case C_JoinServer:
      showNetTab(id-C_JoinOpts);
      return;
    case C_WaitSummary:
      showNetTab(0);
      return;
    case C_Create: {
      // 0x4ce5bd: the page writes what it has, then the game is made (0x492020); in: the waiting room
      saveNetTab();
      NetRequest r;
      r.kind = NetRequest::Host;
      netRequests.push_back(r);
      return;
      }
    case C_Connect: {
      // 0x4cfc54: the game chosen in the list (0x4d0210); "Łączenie..." with "Przerwij" while it answers
      saveNetTab();
      for(auto& g:netView.games)
        if(g.address+":"+std::to_string(g.port)==netChosen) {
          NetRequest r;
          r.kind = NetRequest::Join;
          r.text = g.address;
          r.port = g.port;
          netRequests.push_back(std::move(r));
          showMessage(T_Connecting,Message::NetCancel,{},T_Abort);
          return;
          }
      return;
      }
    case C_Sort:
      netSort = (netSort+1)%5;
      for(auto& b:right)
        if(b.id==C_Sort)
          b.text = T_Sort+uint32_t(netSort);
      return;
    case C_Refresh:
      showNetTab(0);
      return;
    case C_WaitRace: {
      if(!netView.host) {
        // "Gotów": ours goes back and forth
        netReady = !netReady;
        NetRequest r;
        r.kind = NetRequest::Ready;
        r.on   = netReady;
        netRequests.push_back(r);
        return;
        }
      // the host's "Wyścig" (0x4d9d19): not alone, and everyone else ready
      if(netView.rows.size()<2) {
        showMessage(T_Alone);
        return;
        }
      for(auto& r:netView.rows)
        if(!r.self && !r.ready) {
          showMessage(T_NotAllReady);
          return;
          }
      NetRequest r;
      r.kind = NetRequest::Start;
      netRequests.push_back(r);
      return;
      }
    case C_WaitLeave:
      showMessage(T_LeaveQ,Message::NetLeave,{},T_No);
      return;
    case C_Kick: {
      // 0x4dcb60: the player chosen in the table, not oneself
      if(netRow<0 || size_t(netRow)>=netView.rows.size())
        return;
      const auto& row = netView.rows[size_t(netRow)];
      if(row.self) {
        showMessage(T_NoSelfKick);
        return;
        }
      NetRequest r;
      r.kind   = NetRequest::Kick;
      r.player = row.id;
      netRequests.push_back(r);
      netRow = -1;
      return;
      }
    }
  }

void Menu::netIn() {
  if(message.open && message.action==Message::NetCancel)
    message.open = false;
  netReady = false;
  open(Page::NetWait);
  }

void Menu::netOut(uint32_t why) {
  if(message.open && message.action==Message::NetCancel)
    message.open = false;
  netReady = false;
  if((current==Page::NetJoin || current==Page::NetCreate || current==Page::Multi) && slide==0) {
    if(why!=0)
      showMessage(why);
    return;
    }
  netMessage = why;
  open(Page::Multi);
  }

void Menu::netTick(float) {
  if(current!=Page::NetWait || netTab!=0)
    return;
  // the host's track and laps as they are now (the others' greyed lines follow them)
  if(!netView.host) {
    if(auto* t = option(L_Track))
      t->e.at = std::min(netView.track,TrackCount-1);
    if(auto* l = option(L_Laps))
      for(size_t i=0; i<3; ++i)
        if(LapsList[i]==netView.laps)
          l->e.at = i;
    }
  for(auto& r:netView.rows)
    if(r.self)
      netReady = r.ready;
  for(auto& b:right)
    if(b.id==C_WaitRace) {
      b.text = netView.host ? T_Race : T_Ready;
      b.checked = !netView.host && netReady;
      }
  // "Wykop gracza" in slot 7, the host's (0x4dc2e3)
  const bool kick = std::any_of(right.begin(),right.end(),[](const Button& b){ return b.id==C_Kick; });
  if(netView.host && !kick) {
    Button b;
    b.slot = 7; b.text = T_Kick; b.kind = Button::Action; b.id = C_Kick;
    right.push_back(b);
    }
  if(netRow>=int(netView.rows.size()))
    netRow = -1;
  }

bool Menu::netClick(float mx, float my) {
  if(current==Page::NetJoin && netTab==0) {
    const auto order = netGameOrder();
    const int r = tableRow(netGameTable(),mx,my,order.size(),0);
    if(r>=0) {
      const auto& g = netView.games[order[size_t(r)]];
      const std::string key = g.address+":"+std::to_string(g.port);
      if(key!=netChosen)
        playControl(2);
      netChosen = key;
      return true;
      }
    }
  if(current==Page::NetWait && netTab==0) {
    const int r = tableRow(netWaitTable(),mx,my,netView.rows.size(),0);
    if(r>=0) {
      if(r!=netRow)
        playControl(2);
      netRow = r;
      return true;
      }
    }
  return false;
  }

bool Menu::netKey(Event::KeyType key) {
  switch(current) {
    case Page::NetCreate:
      if(key==Event::K_Return)
        netAction(C_Create);
      if(key==Event::K_ESCAPE)
        leave(Page::Multi,Command::None);
      return true;
    case Page::NetJoin:
      if(key==Event::K_Return)
        netAction(C_Connect);
      if(key==Event::K_ESCAPE)
        leave(Page::Multi,Command::None);
      if(netTab==0 && (key==Event::K_Up || key==Event::K_Down)) {
        const auto order = netGameOrder();
        int at = -1;
        for(size_t i=0; i<order.size(); ++i) {
          const auto& g = netView.games[order[i]];
          if(g.address+":"+std::to_string(g.port)==netChosen)
            at = int(i);
          }
        at = std::clamp(at+(key==Event::K_Up ? -1 : 1),0,int(order.size())-1);
        if(at>=0) {
          const auto& g = netView.games[order[size_t(at)]];
          netChosen = g.address+":"+std::to_string(g.port);
          playControl(2);
          }
        }
      return true;
    case Page::NetWait:
      if(key==Event::K_ESCAPE)
        netAction(C_WaitLeave);
      if(key==Event::K_Return) {
        // Enter types into the chat (the race starts with its button)
        for(size_t i=0; i<options.size(); ++i)
          if(options[i].id==L_Chat)
            optEdit = int(i);
        }
      return true;
    default:
      return false;
    }
  }

void Menu::drawNet() {
  const Color white = {1,1,1,1};
  const Color grey  = {Grey,Grey,Grey,1};
  if(current==Page::NetCreate && netTab==0) {
    // staticIPaddr (0x4ced2c): this computer's addresses beside "Adres IP:"; staticBPS: what the connection is
    lines(font20,netView.localAddress,{347,300,347+136,300+71},0,22.5f,grey);
    if(const auto* b = option(L_Bps))
      lines(font20,str(T_Link+uint32_t(b->e.at)),{114,403,114+200,403+120},0,22.5f,grey);
    }
  if(netTab==1 && (current==Page::NetCreate || current==Page::NetJoin)) {
    // staticChatOpts (0x4cf5dd: Smash26, white, underlined), and the player's panel (0x4a7170)
    if(current==Page::NetCreate) {
      label(font26,str(T_ChatOptions),{114,249,114+340,249+25-3},0,2,white);
      underline(114,114+340,249+25-2,white);
      }
    }
  if(current==Page::NetJoin && netTab==0) {
    // the games found (0x4d0550): "Serwer", "Trasa", "Graczy", "Okr.", "Opóźnienie"
    const auto order = netGameOrder();
    int chosen = -1;
    for(size_t i=0; i<order.size(); ++i) {
      const auto& g = netView.games[order[i]];
      if(g.address+":"+std::to_string(g.port)==netChosen)
        chosen = int(i);
      }
    drawTable(netGameTable(),order.size(),0,chosen,[&](int row, size_t col, const Box& b, const Color& c) {
      if(row<0) {
        label(font20,str(T_ColServer+uint32_t(col)),b,0,2,c,fit(font20,str(T_ColServer+uint32_t(col)),b.x1-b.x0-2));
        return;
        }
      const auto& g = netView.games[order[size_t(row)]];
      std::string s;
      switch(col) {
        case 0: s = g.name; break;
        case 1: s = g.track<TrackCount ? std::string(str(T_TrackName+uint32_t(g.track))) : std::string(); break;
        case 2: s = std::to_string(g.players)+"/"+std::to_string(g.maxPlayers); break;
        case 3: s = g.laps>0 ? std::to_string(g.laps) : std::string(); break;
        case 4: s = format(str(T_SiMs),{std::to_string(g.ping)}); break;
        }
      // a game that can't be joined (racing, full, another version) grey
      const bool can = g.open && g.sameVersion && g.players<g.maxPlayers;
      label(font20,s,b,0,1,can ? c : Color{0.5f,0.5f,0.5f,1},fit(font20,s,b.x1-b.x0-2));
      });
    }
  if(current==Page::NetWait && netTab==0) {
    // the players (0x4dceb0): "Gracz", "Samochód", "Dopalacz", "Silnik", "Opony", "Stan"
    drawTable(netWaitTable(),netView.rows.size(),0,netRow,[&](int row, size_t col, const Box& b, const Color& c) {
      static const uint32_t head[6] = {T_ColPlayer,T_ColPlayer+1,T_ColPlayer+3,T_ColPlayer+4,T_ColPlayer+5,
                                       T_ColPlayer+6};
      if(row<0) {
        label(font20,str(head[col]),b,0,2,c,fit(font20,str(head[col]),b.x1-b.x0-2));
        return;
        }
      const auto& r = netView.rows[size_t(row)];
      std::string s;
      switch(col) {
        case 0: s = r.name; break;
        case 1: s = r.known ? std::string(str(T_CarName+4*uint32_t(std::min<size_t>(r.car,2)))) : std::string(); break;
        case 2: s = r.known ? std::string(str(T_PartLevel+uint32_t(std::clamp(r.boost,0,3)))) : std::string(); break;
        case 3: s = r.known ? std::string(str(T_PartLevel+uint32_t(std::clamp(r.resonator,0,3)))) : std::string(); break;
        case 4: s = r.known ? std::string(str(T_PartLevel+uint32_t(std::clamp(r.tires,0,3)))) : std::string(); break;
        case 5: s = std::string(str(r.ready ? T_IsReady : T_NotReady)); break;
        }
      label(font20,s,b,0,1,c,fit(font20,s,b.x1-b.x0-2));
      });
    // the chat (class CHA, 0x4ad960): the newest lines at the bottom, each in its sender's colour
    const float step = 22.5f;
    const size_t room = size_t((ChatBox[3]-ChatBox[1])/step);
    const size_t n    = netView.chat.size();
    const size_t from = n>room ? n-room : 0;
    for(size_t i=from; i<n; ++i) {
      const auto& l = netView.chat[i];
      const float y = ChatBox[1]+float(i-from)*step;
      label(font20,l.text,{ChatBox[0],y,ChatBox[2],y+step},0,1,l.color<0 ? grey : chatColor(l.color),
            fit(font20,l.text,ChatBox[2]-ChatBox[0]));
      }
    }
  }
