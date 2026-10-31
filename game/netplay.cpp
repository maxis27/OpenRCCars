// The network game (RCCars.exe modes 7 and 8): the session, the other players' cars, the start held until everyone
// has loaded the track. Started from the command line for now (-host, -join); the original's screens come later.
#include "mainwindow.h"

#include <Tempest/Log>

#include <algorithm>
#include <cmath>

#include "net/bytes.h"

using namespace Tempest;

namespace {

const char* const Cars[] = {"Car1", "Car2", "Car3"};

// our messages (net::Session::FirstUserType ..)
enum : uint8_t {
  MsgCar   = 0x20,   // fast: net::CarState
  MsgLook  = 0x21,   // [u8 car][u8 boost][u8 resonator][u8 tires][u8 skin]: the sender's car
  MsgStart = 0x22,   // host: [str track][i32 laps]: load it (if needed), the car on the grid, the countdown held
  MsgReady = 0x23,   // to the host: loaded, on the grid
  MsgGo    = 0x24,   // host: the countdown goes on
  };

}

void MainWindow::startNet() {
  using rc::net::Session;
  const auto& o = net.options;
  if(o.role==NetOptions::Role::None)
    return;
  std::string name = o.name;
  if(name.empty() && players && players->current())
    name = players->current()->name;
  if(name.empty())
    name = "Gracz";
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
  net.held = false;
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
  rc::net::Writer w;
  w.u8(uint8_t(carId)).u8(uint8_t(boostLevel)).u8(uint8_t(resonatorLevel)).u8(uint8_t(tiresLevel)).u8(uint8_t(skin));
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
    net.session->send(MsgReady,{},true,rc::net::Session::HostId);
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

void MainWindow::tickNet(float dt) {
  using Kind = rc::net::Session::Event::Kind;
  if(!online())
    return;
  auto& s = *net.session;
  net.clock += dt;
  for(auto& e:s.poll()) {
    switch(e.kind) {
      case Kind::Joined: {
        if(e.player==s.self()) {
          Log::i("network: in \"",s.sessionName(),"\" as player ",int(e.player));
          netLook(rc::net::Session::Everyone);
          break;
          }
        Log::i("network: ",s.player(e.player) ? s.player(e.player)->name : std::string("?")," came");
        net.cars[e.player].name = s.player(e.player) ? s.player(e.player)->name : std::string();
        if(s.ready())
          netLook(e.player);
        // the host: the race again with the newcomer, if it has not started
        if(s.isHost() && race && race->holdCar())
          netRestart();
        break;
        }
      case Kind::Left: {
        auto it = net.cars.find(e.player);
        if(it!=net.cars.end()) {
          Log::i("network: ",it->second.name," left");
          if(it->second.sound)
            it->second.sound->stop();
          net.cars.erase(it);
          }
        net.ready.erase(e.player);
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
          if(!r.ok() || carScene.models.empty())
            break;
          auto& c = net.cars[e.player];
          if(c.shown && c.look==l)
            break;
          c.look  = l;
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
          net.held = true;
          raceRequested = true;
          }
        else if(e.type==MsgReady && s.isHost()) {
          net.ready[e.player] = true;
          netLoaded();
          }
        else if(e.type==MsgGo && !s.isHost()) {
          net.held = false;
          }
        break;
        }
      case Kind::Closed:
        endNet(e.reason);
        if(menu && !menu->active())
          menu->open(Menu::Page::Main);
        return;
      }
    }
  if(!online())
    return;

  // the race is on: no joining now (Reason::Started)
  if(s.isHost())
    s.setOpen(race==nullptr || race->holdCar());

  // the player's car, `rate` times a second
  net.sendIn -= dt;
  if(vehicle && s.ready() && net.sendIn<=0) {
    net.sendIn += 1.f/rc::net::CarState::DefaultRate;
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
