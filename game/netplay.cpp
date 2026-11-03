// The network game (RCCars.exe modes 7 and 8): the session, its waiting room between the races (the menus' screens,
// menunet.cpp), the other players' cars, the start held until everyone has loaded the track. Also started from the
// command line (-host, -join) straight into a race, without the waiting room.
#include "mainwindow.h"

#include <Tempest/Log>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>

#include "formats/cp1250.h"
#include "formats/settings.h"
#include "net/bytes.h"
#include "players.h"

using namespace Tempest;

namespace {

const char* const Cars[] = {"Car1", "Car2", "Car3"};

// our messages (net::Session::FirstUserType ..)
enum : uint8_t {
  MsgCar    = 0x20,  // fast: net::CarState
  MsgLook   = 0x21,  // [u8 car][u8 boost][u8 resonator][u8 tires][u8 skin][u8 states a second]: the sender's car
  MsgStart  = 0x22,  // host: [str track][i32 laps]: load it (if needed), the car on the grid, the countdown held
  MsgLoaded = 0x23,  // to the host: loaded, on the grid
  MsgGo     = 0x24,  // host: the countdown goes on
  MsgLobby  = 0x25,  // host: [u8 track][u8 laps][u8 n] n x [u8 id][u8 "Gotów"]: the waiting room
  MsgReady  = 0x26,  // to the host: [u8 on]: "Gotów"
  MsgChat   = 0x27,  // [u8 colour][str text]: a line of the chat (UTF-8), its sender's name put before it
  };

// the texts of why it is over (Language/Game)
enum : uint32_t {
  T_Joined      = 20200, // ">%s połączył się do serwera", "odłączył się", "wykopany"
  T_LeftNews    = 20201,
  T_KickedNews  = 20202,
  T_TrackNews   = 20214, // ">Mapa zmieniona na: %s", ">Liczba okrążeń zmieniona na: %d"
  T_LapsNews    = 20215,
  T_Lost        = 20233, // "Połączenie utracone", "Nieznany błąd", "Sesja zakończone przez serwer",
  T_Unknown     = 20234, // "Zostałeś wykopany z gry"
  T_HostEnded   = 20235,
  T_KickedOut   = 20236,
  T_TrackName   = 40000,
  T_CantCreate  = 42418, // "Nie można utworzyć GRY"
  T_ConnError   = 42450, // "Błąd połączenia" (and 42452, 42453 the same in Polish)
  T_Refused     = 42452,
  T_TooSlow     = 42453,
  };

std::vector<uint8_t> readCrs(const std::filesystem::path& p) {
  std::ifstream f(p,std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());
  }

// RCCars.crs with `key` of `section` set, written
void writeCrs(const std::filesystem::path& p, const char* section, const char* key, int32_t v) {
  auto data = readCrs(p);
  if(data.empty())
    return;
  const auto changed = rc::ConfigFile::withInteger(data,section,key,v);
  if(changed==data)
    return;
  std::ofstream f(p,std::ios::binary|std::ios::trunc);
  f.write(reinterpret_cast<const char*>(changed.data()),std::streamsize(changed.size()));
  }

// "%s" and "%d" of a news line
std::string fill(std::string_view f, const std::string& v) {
  std::string s(f);
  for(const char* k:{"%s","%d","%i"}) {
    const size_t at = s.find(k);
    if(at!=std::string::npos)
      return s.replace(at,2,v);
    }
  return s;
  }

}

std::string MainWindow::netNick() const {
  auto* p = players ? players->current() : nullptr;
  if(p==nullptr)
    return std::string(menu->gameText(40507));   // "Gracz"
  return p->nick.empty() ? p->name : p->nick;
  }

void MainWindow::startNet() {
  using rc::net::Session;
  const auto& o = net.options;
  if(o.role==NetOptions::Role::None)
    return;
  std::string name = o.name;
  if(name.empty())
    name = rc::cp1250::toUtf8(netNick());
  try {
    if(o.role==NetOptions::Role::Host) {
      Session::HostOptions h;
      h.port        = o.port;
      h.maxPlayers  = Session::DefaultMaxPlayers;
      h.sessionName = level ? level->name() : std::string();
      h.playerName  = name;
      net.session   = Session::host(h);
      Log::i("network: hosting on port ",o.port,", waiting for ",o.players-1," more");
      net.held = true;
      net.ready.clear();
      net.ready[0] = true;
      }
    else {
      net.session = Session::join(o.address,o.port,name);
      Log::i("network: joining ",o.address,":",o.port," as ",name);
      }
    net.lobby  = false;
    net.racing = true;
    }
  catch(const std::exception& e) {
    Log::e("network: ",e.what());
    net.session.reset();
    }
  }

void MainWindow::endNet(rc::net::Session::Reason why) {
  Log::i("network: over (",rc::net::reasonName(why),")");
  for(auto& [id,c]:net.cars)
    if(c.sound)
      c.sound->stop();
  net.cars.clear();
  net.session.reset();
  net.held    = false;
  net.joining = false;
  net.racing  = false;
  net.set.clear();
  net.ready.clear();
  net.chat.clear();
  }

void MainWindow::netOver(uint32_t why) {
  const bool inRace = !menu->active() || menu->page()==Menu::Page::InRace || menu->page()==Menu::Page::Finish;
  if(inRace) {
    // the race left as "Przerwij" leaves it: silent, the track loaded under the menus
    sounds->stopAll();
    if(carSound)
      carSound->stop();
    for(auto& s:aiSounds)
      s.stop();
    paused = false;
    }
  menu->netOut(why);
  }

rc::Mat4 MainWindow::netGrid(uint8_t id) const {
  rc::Mat4 at = level->carStart(car->full().bboxMin().y);
  try {
    rc::Opponents field(*physics,[this](const std::string& f){ return resources.pack().read(f); },carScene,
                        level->name(),*checkpoints,rc::Opponents::Level::Easy);
    field.restart();
    rc::Mat4 grid;
    if(id==0 && field.playerStart(grid))
      return grid;
    if(id>0 && size_t(id-1)<field.size())
      return field[id-1].pose();
    }
  catch(const std::exception& e) {
    Log::e("network: no grid: ",e.what());
    }
  // past the grid: in a row behind the start, 1.5 m apart
  return at*rc::Mat4::translate({-1.5f*float(id%2),0,-3.f*float(id/2)});
  }

void MainWindow::netLook(uint8_t to) {
  // the car the player races with: the one chosen, as the player has it (stock when not bought), like startRace
  const size_t c = std::min<size_t>(menu->setup().car,2);
  int32_t b = 0, r = 0, t = 0, sk = 0;
  if(auto* p = players ? players->current() : nullptr)
    if(p->cars[c].bought) {
      b  = p->cars[c].boost;
      r  = p->cars[c].resonator;
      t  = p->cars[c].tires;
      sk = p->cars[c].skin;
      }
  rc::net::Writer w;
  w.u8(uint8_t(c)).u8(uint8_t(b)).u8(uint8_t(r)).u8(uint8_t(t)).u8(uint8_t(sk)).u8(uint8_t(net.rate));
  net.session->send(MsgLook,w.data(),true,to);
  }

void MainWindow::netRestart() {
  if(!online() || !net.session->isHost() || level==nullptr)
    return;
  net.ready.clear();
  net.held = true;
  rc::net::Writer w;
  w.str(level->name()).i32(laps);
  net.session->send(MsgStart,w.data(),true);
  setupPhysics(true);
  net.ready[0] = true;
  Log::i("network: start on ",level->name(),", ",laps," laps, ",net.session->players().size()," players");
  }

void MainWindow::netLoaded() {
  if(!online())
    return;
  // the car as this race has it (the player's upgrades and skin)
  netLook(rc::net::Session::Everyone);
  if(!net.session->isHost()) {
    net.session->send(MsgLoaded,{},true,rc::net::Session::HostId);
    return;
    }
  // everyone in has loaded, and enough came: go
  const auto& list = net.session->players();
  if(int(list.size())<net.options.players)
    return;
  for(auto& p:list)
    if(!net.ready[p.id])
      return;
  net.session->send(MsgGo,{},true);
  net.held = false;
  Log::i("network: go");
  }

void MainWindow::netLobby() {
  if(!online() || !net.session->isHost())
    return;
  auto& s = *net.session;
  rc::net::Writer w;
  w.u8(uint8_t(net.track)).u8(uint8_t(net.laps)).u8(uint8_t(s.players().size()));
  for(auto& p:s.players())
    w.u8(p.id).u8(net.set[p.id] ? 1 : 0);
  s.send(MsgLobby,w.data(),true);
  // what the search is told (Finder::Game::info): the track and the laps
  s.setInfo({uint8_t(net.track),uint8_t(net.laps)});
  }

void MainWindow::netSay(std::string text, int32_t color) {
  Menu::NetView::Line l;
  l.text  = std::move(text);
  l.color = color;
  net.chat.push_back(std::move(l));
  // the original's chat keeps a few screens of it
  if(net.chat.size()>64)
    net.chat.erase(net.chat.begin());
  }

void MainWindow::netRequest(const Menu::NetRequest& r) {
  using rc::net::Session;
  using K = Menu::NetRequest::Kind;
  switch(r.kind) {
    case K::Host: {
      // 0x4ce5bd -> 0x492020: the session as RCCars.crs has it now
      if(online())
        endNet(Session::Reason::Left);
      const auto crs = rc::ConfigFile::parse(readCrs(resources.gameDir()/"RCCars.crs"));
      Session::HostOptions h;
      const int32_t port = crs.integer("Network","portNum",0);
      h.port        = port>0 && port<65536 ? uint16_t(port) : Session::DefaultPort;
      h.maxPlayers  = crs.integer("Network","maxPlayersNum",Session::DefaultMaxPlayers);
      h.sessionName = rc::cp1250::toUtf8(crs.string("Network","sessionName"));
      h.playerName  = rc::cp1250::toUtf8(netNick());
      h.maxLag      = uint32_t(std::max(0,crs.integer("Multiplayer","maxLag",1000)));
      const int bps = std::clamp(crs.integer("Multiplayer","bpsLevel",4),0,4);
      try {
        net.session = Session::host(h);
        }
      catch(const std::exception& e) {
        Log::e("network: ",e.what());
        menu->netOut(T_CantCreate);
        return;
        }
      Log::i("network: hosting \"",h.sessionName,"\" on port ",h.port,", ",h.maxPlayers," players, lag ",h.maxLag," ms");
      net.options.players = 1;
      net.lobby  = true;
      net.racing = false;
      net.rate   = rc::net::CarState::Rates[bps];
      // the waiting room's track and laps as the last time (Multiplayer mapCode, nLaps: 0x4a8020, 0x4a7ed0)
      const int32_t code = crs.integer("Multiplayer","mapCode",int32_t(menu->setup().track));
      const int32_t n    = crs.integer("Multiplayer","nLaps",3);
      net.track  = code>=0 && size_t(code)<Menu::TrackCount ? size_t(code) : menu->setup().track;
      net.laps   = n==3 || n==5 || n==7 ? n : 3;
      net.set.clear();
      net.chat.clear();
      netLobby();
      menu->netIn();
      return;
      }
    case K::Search: {
      if(net.finder==nullptr) {
        try {
          net.finder = std::make_unique<rc::net::Finder>();
          }
        catch(const std::exception& e) {
          Log::e("network: ",e.what());
          return;
          }
        }
      net.finder->search(r.text,r.port!=0 ? r.port : Session::DefaultPort);
      return;
      }
    case K::Join: {
      if(online())
        endNet(Session::Reason::Left);
      try {
        net.session = Session::join(r.text,r.port,rc::cp1250::toUtf8(netNick()));
        }
      catch(const std::exception& e) {
        Log::e("network: ",e.what());
        menu->netOut(T_ConnError);
        return;
        }
      Log::i("network: joining ",r.text,":",r.port);
      const auto crs = rc::ConfigFile::parse(readCrs(resources.gameDir()/"RCCars.crs"));
      net.rate    = rc::net::CarState::Rates[std::clamp(crs.integer("Multiplayer","bpsLevel",4),0,4)];
      net.lobby   = true;
      net.joining = true;
      net.racing  = false;
      net.set.clear();
      net.chat.clear();
      return;
      }
    case K::Cancel:
      if(online())
        endNet(Session::Reason::Left);
      return;
    case K::Leave:
      // "Odłącz": a client leaves, the host ends the game for everyone
      if(online()) {
        net.session->close();
        endNet(Session::Reason::Left);
        }
      return;
    case K::Ready:
      if(online() && !net.session->isHost()) {
        rc::net::Writer w;
        w.u8(r.on ? 1 : 0);
        net.session->send(MsgReady,w.data(),true,Session::HostId);
        }
      return;
    case K::Start: {
      if(!online() || !net.session->isHost())
        return;
      // 0x4d9d19: the race of everyone in, on the track and laps chosen; "Gotów" again next time
      auto& st = menu->setup();
      st.track = std::min(net.track,Menu::TrackCount-1);
      st.mode  = Menu::Mode::Network;
      st.laps  = net.laps;
      net.options.players = int(net.session->players().size());
      net.ready.clear();
      net.ready[0] = true;
      net.held   = true;
      net.racing = true;
      net.set.clear();
      netLobby();
      rc::net::Writer w;
      w.str(Menu::trackName(st.track)).i32(st.laps);
      net.session->send(MsgStart,w.data(),true);
      Log::i("network: start on ",Menu::trackName(st.track),", ",st.laps," laps, ",net.options.players," players");
      raceRequested = true;
      return;
      }
    case K::Setup: {
      if(!online() || !net.session->isHost())
        return;
      const size_t t = std::min(r.track,Menu::TrackCount-1);
      if(t!=net.track)
        netSay(fill(menu->gameText(T_TrackNews),std::string(menu->gameText(T_TrackName+uint32_t(t)))),-1);
      if(r.laps!=net.laps)
        netSay(fill(menu->gameText(T_LapsNews),std::to_string(r.laps)),-1);
      net.track = t;
      net.laps  = r.laps;
      writeCrs(resources.gameDir()/"RCCars.crs","Multiplayer","mapCode",int32_t(t));
      writeCrs(resources.gameDir()/"RCCars.crs","Multiplayer","nLaps",r.laps);
      netLobby();
      return;
      }
    case K::Chat: {
      if(!online())
        return;
      auto* p = players ? players->current() : nullptr;
      const int32_t color = p!=nullptr ? std::clamp(p->chatColor,0,int32_t(Menu::ChatColors)-1) : 0;
      rc::net::Writer w;
      w.u8(uint8_t(color)).str(rc::cp1250::toUtf8(r.text));
      net.session->send(MsgChat,w.data(),true);
      netSay(netNick()+">"+r.text,color);
      return;
      }
    case K::Kick:
      if(online() && net.session->isHost()) {
        if(auto* p = net.session->player(r.player))
          netSay(fill(menu->gameText(T_KickedNews),rc::cp1250::fromUtf8(p->name)),-1);
        net.session->kick(r.player);
        }
      return;
    }
  }

void MainWindow::netView() {
  // what the network screens show: the games found, the waiting room
  Menu::NetView v;
  if(net.finder)
    for(auto& g:net.finder->games()) {
      Menu::NetView::Game m;
      m.name        = rc::cp1250::fromUtf8(g.name);
      m.address     = g.address;
      m.port        = g.port;
      m.players     = g.players;
      m.maxPlayers  = g.maxPlayers;
      m.ping        = g.ping;
      m.open        = g.open;
      m.sameVersion = g.sameVersion;
      if(g.info.size()>=2) {
        m.track = std::min<size_t>(g.info[0],Menu::TrackCount);
        m.laps  = g.info[1];
        }
      v.games.push_back(std::move(m));
      }
  if(net.localAddress.empty())
    net.localAddress = rc::net::localAddress();
  v.localAddress = net.localAddress;
  if(online() && net.session->ready()) {
    auto& s = *net.session;
    v.online = true;
    v.host   = s.isHost();
    v.track  = net.track;
    v.laps   = net.laps;
    for(auto& p:s.players()) {
      Menu::NetView::Row row;
      row.id    = p.id;
      row.name  = rc::cp1250::fromUtf8(p.name);
      row.self  = p.id==s.self();
      row.ready = p.id==rc::net::Session::HostId || net.set[p.id];
      if(row.self) {
        row.known = true;
        const size_t c = std::min<size_t>(menu->setup().car,2);
        row.car = c;
        if(auto* pr = players ? players->current() : nullptr)
          if(pr->cars[c].bought) {
            row.boost     = pr->cars[c].boost;
            row.resonator = pr->cars[c].resonator;
            row.tires     = pr->cars[c].tires;
            }
        }
      else {
        auto it = net.cars.find(p.id);
        if(it!=net.cars.end() && it->second.known) {
          const auto& l = it->second.look;
          row.known     = true;
          row.car       = size_t(l.car);
          row.boost     = l.boost;
          row.resonator = l.resonator;
          row.tires     = l.tires;
          }
        }
      v.rows.push_back(std::move(row));
      }
    v.chat = net.chat;
    }
  menu->setNet(std::move(v));
  }

void MainWindow::tickNet(float dt) {
  using Kind   = rc::net::Session::Event::Kind;
  using Reason = rc::net::Session::Reason;
  // the games' search while "Dołącz do gry" is shown
  if(net.finder) {
    if(menu->page()!=Menu::Page::NetJoin)
      net.finder.reset();
    else
      net.finder->poll(dt);
    }
  if(!online())
    return;
  auto& s = *net.session;
  net.clock += dt;
  // back in the waiting room: the race is over for this player
  if(net.lobby && net.racing && menu->page()==Menu::Page::NetWait)
    net.racing = false;
  for(auto& e:s.poll()) {
    switch(e.kind) {
      case Kind::Joined: {
        const std::string who = s.player(e.player) ? s.player(e.player)->name : std::string();
        if(e.player==s.self()) {
          Log::i("network: in \"",s.sessionName(),"\" as player ",int(e.player));
          netLook(rc::net::Session::Everyone);
          if(net.lobby && net.joining) {
            net.joining = false;
            menu->netIn();
            }
          break;
          }
        Log::i("network: ",who," came");
        net.cars[e.player].name = who;
        if(s.ready())
          netLook(e.player);
        if(net.lobby && !net.joining)
          netSay(fill(menu->gameText(T_Joined),rc::cp1250::fromUtf8(who)),-1);
        if(s.isHost() && net.lobby)
          netLobby();
        // the host: the race again with the newcomer, if it has not started
        if(s.isHost() && !net.lobby && race && race->holdCar())
          netRestart();
        break;
        }
      case Kind::Left: {
        auto it = net.cars.find(e.player);
        if(it!=net.cars.end()) {
          Log::i("network: ",it->second.name," left");
          netSay(fill(menu->gameText(T_LeftNews),rc::cp1250::fromUtf8(it->second.name)),-1);
          if(it->second.sound)
            it->second.sound->stop();
          net.cars.erase(it);
          }
        net.ready.erase(e.player);
        net.set.erase(e.player);
        if(s.isHost() && net.lobby)
          netLobby();
        if(s.isHost() && net.held)
          netLoaded();
        break;
        }
      case Kind::Message: {
        rc::net::Reader r(e.data);
        if(e.type==MsgCar) {
          rc::CarProfile::Frame f;
          uint8_t flags = 0;
          auto it = net.cars.find(e.player);
          if(it!=net.cars.end() && rc::net::CarState::unpack(e.data,f,flags))
            it->second.states.receive(f,flags,net.clock);
          }
        else if(e.type==MsgLook) {
          rc::Ghosts::Look l;
          l.car       = std::clamp<int32_t>(r.u8(),0,2);
          l.boost     = std::clamp<int32_t>(r.u8(),0,3);
          l.resonator = std::clamp<int32_t>(r.u8(),0,3);
          l.tires     = std::clamp<int32_t>(r.u8(),0,3);
          l.skin      = std::clamp<int32_t>(r.u8(),0,3);
          const float rate = std::clamp<float>(r.u8(),1,60);
          if(!r.ok())
            break;
          auto& c = net.cars[e.player];
          if(!c.known || c.states.delay()!=rc::net::RemoteCar(rate).delay())
            c.states = rc::net::RemoteCar(rate);
          const bool same = c.known && c.look==l;
          c.look  = l;
          c.known = true;
          if((same && c.shown) || carScene.models.empty())
            break;
          c.shown = std::make_unique<rc::Ghosts::Ghost>(rc::carGeometry(carScene,Cars[l.car]),
                                                        rc::CarParams::load(resources.pack(),l.car+1));
          c.shown->look    = l;
          c.shown->visible = true;
          c.shown->running = true;
          if(c.sound==nullptr)
            c.sound = std::make_unique<rc::AiCarSound>(sounds->mixer(),sounds->library());
          carModel(l);
          }
        else if(e.type==MsgStart && !s.isHost()) {
          const std::string track = r.str();
          const int32_t     n     = r.i32();
          const size_t      t     = Menu::trackIndex(track);
          if(!r.ok() || t>=Menu::TrackCount)
            break;
          Log::i("network: the host starts ",track,", ",n," laps");
          auto& st = menu->setup();
          st.track = t;
          st.mode  = Menu::Mode::Network;
          st.laps  = n;
          net.held   = true;
          net.racing = true;
          net.set.clear();
          raceRequested = true;
          }
        else if(e.type==MsgLoaded && s.isHost()) {
          net.ready[e.player] = true;
          netLoaded();
          }
        else if(e.type==MsgGo && !s.isHost()) {
          net.held = false;
          }
        else if(e.type==MsgLobby && !s.isHost()) {
          const size_t t = std::min<size_t>(r.u8(),Menu::TrackCount-1);
          const int    n = r.u8();
          const size_t k = r.u8();
          std::map<uint8_t,bool> set;
          for(size_t i=0; i<k; ++i) {
            const uint8_t id = r.u8();
            set[id] = r.u8()!=0;
            }
          if(!r.ok())
            break;
          // 0x4dc500: what the host changed, as news in the chat
          if(t!=net.track)
            netSay(fill(menu->gameText(T_TrackNews),std::string(menu->gameText(T_TrackName+uint32_t(t)))),-1);
          if(n!=net.laps)
            netSay(fill(menu->gameText(T_LapsNews),std::to_string(n)),-1);
          net.track = t;
          net.laps  = n;
          net.set   = std::move(set);
          }
        else if(e.type==MsgReady && s.isHost()) {
          net.set[e.player] = r.u8()!=0;
          if(r.ok())
            netLobby();
          }
        else if(e.type==MsgChat) {
          const int32_t     color = std::min<int32_t>(r.u8(),int32_t(Menu::ChatColors)-1);
          const std::string text  = r.str();
          if(!r.ok())
            break;
          const auto* p = s.player(e.player);
          netSay(rc::cp1250::fromUtf8((p ? p->name : std::string())+">"+text),color);
          }
        break;
        }
      case Kind::Closed: {
        const bool lobby = net.lobby, joining = net.joining, host = s.isHost();
        endNet(e.reason);
        if(!lobby) {
          if(menu && !menu->active())
            menu->open(Menu::Page::Main);
          return;
          }
        // 0x4d9e0a: why, over "Wielu graczy" (or "Błąd połączenia" over "Dołącz do gry")
        uint32_t why = T_Unknown;
        switch(e.reason) {
          case Reason::None:           why = T_Unknown; break;
          case Reason::Left:           why = host ? 0u : uint32_t(T_HostEnded); break;
          case Reason::ConnectionLost: why = joining ? T_ConnError : T_Lost; break;
          case Reason::NoAnswer:       why = T_ConnError; break;
          case Reason::Full:           why = T_Refused; break;
          case Reason::Lag:            why = T_TooSlow; break;
          case Reason::Version:        why = T_Refused; break;
          case Reason::Started:        why = T_Refused; break;
          case Reason::Kicked:         why = T_KickedOut; break;
          }
        netOver(why);
        return;
        }
      }
    }
  if(!online())
    return;

  // the race is on: no joining now (Reason::Started)
  if(s.isHost())
    s.setOpen(net.lobby ? !net.racing : (race==nullptr || race->holdCar()));

  // the player's car, `rate` times a second, while racing
  net.sendIn -= dt;
  if(vehicle && s.ready() && net.racing && net.sendIn<=0) {
    net.sendIn += 1.f/net.rate;
    if(net.sendIn<0)
      net.sendIn = 0;
    const uint8_t flags = vehicle->boosting() ? rc::net::CarState::Boosting : 0;
    s.send(MsgCar,rc::net::CarState::pack(rc::Ghosts::frameOf(*vehicle,net.clock,0),flags),false);
    }
  // the others' where they are now
  for(auto& [id,c]:net.cars)
    if(c.shown && c.states.ready())
      c.shown->put(c.states.at(net.clock),dt);
  }

void MainWindow::tickNetSounds(const rc::Vec3& ear, const rc::Vec3& right, bool racing) {
  for(auto& [id,c]:net.cars) {
    if(c.sound==nullptr || c.shown==nullptr)
      continue;
    // an engine as an opponent's
    const rc::Vec3 to = c.shown->pose().translation()-ear;
    const float    d  = to.length();
    c.sound->update(std::abs(c.shown->speed())*3.6f,racing && c.states.ready(),d,
                    d>1e-3f ? rc::Vec3::dot(to,right)/d : 0.f);
    }
  }
