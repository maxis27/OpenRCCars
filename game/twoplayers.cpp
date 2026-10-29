// The split screen of "Dwóch graczy" (RCCars.exe modes 9 and 10): player 2's car, race, camera and cockpit, and
// the race's end. The scene of both views is drawn by MainWindow::render.
#include "mainwindow.h"

#include <Tempest/Log>

#include <algorithm>
#include <cmath>

using namespace Tempest;

namespace {

const char* const Cars[] = {"Car1", "Car2", "Car3"};

// RCCars.exe 0x4eab80: the race of the gap (mode 10) ends when player 2 has been "lagMetre" or more away from the
// player (ahead or behind) for 7.5 s in all; the time is only set to 0 at the start of the race (0x14b0990)
constexpr float LagHold = 7.5f;

}

void MainWindow::setupTwo(const Menu::Setup* st) {
  device.waitIdle();
  if(two.rider && two.rider->sound)
    two.rider->sound->stop();
  two.rider.reset();
  two.tracker[0].reset();
  two.tracker[1].reset();
  hud->setView(-1);
  if(st==nullptr || !st->two())
    return;
  // 0x4a5320: two player cars (kind 0, ids 8 and 9), the cockpit of each in its view
  auto r = std::make_unique<Rider>();
  const auto& c = st->riders[1];
  r->carId     = std::min<size_t>(c.car,2);
  r->boost     = c.boost;
  r->resonator = c.resonator;
  r->tires     = c.tires;
  r->skin      = c.skin;
  r->hud       = std::make_unique<Hud>(device,resources);
  r->hud->setView(1);
  r->chase     = ChaseCamera(chase.settings());
  r->camera.fov = chase.splitFov();
  r->sound     = std::make_unique<rc::CarSound>(sounds->mixer(),sounds->library(),int(r->carId));
  hud->setView(0);
  two.lag      = st->mode==Menu::Mode::TwoLag;
  two.lagMetre = st->lagMetre;
  two.rider    = std::move(r);
  fitRider();
  }

void MainWindow::fitRider() {
  auto& r = *two.rider;
  ModelMesh::CarLook look;
  look.boost     = r.boost;
  look.resonator = r.resonator;
  look.tires     = r.tires;
  look.skin      = r.skin;
  look.antenna   = !antennaView->has(r.carId);
  r.antenna.reset();
  r.car = std::make_unique<CarModel>(device,resources,carScene,r.carId,look);
  }

void MainWindow::resetRider(bool restartRace, bool again) {
  auto& r = *two.rider;
  if(r.checkpoints==nullptr) {
    r.checkpoints = std::make_unique<rc::Checkpoints>(level->scene(),level->collision());
    r.race        = std::make_unique<rc::Race>(*r.checkpoints,laps);
    r.hud->setTrack(level->name());
    }
  r.race->setLaps(laps);
  r.vehicle.reset();
  const auto geom = rc::carGeometry(carScene,Cars[r.carId]);
  const auto prm  = rc::CarParams::load(resources.pack(),int32_t(r.carId+1),r.boost,r.resonator,r.tires);
  r.vehicle    = std::make_unique<rc::Vehicle>(*physics,geom,prm,r.start);
  r.pose       = r.start;
  r.lastPos    = r.start.translation();
  r.upsideDown = 0;
  r.jump       = false;
  if(restartRace) {
    r.race->restart(r.start,again);
    if(r.sound)
      r.sound->stop();
    r.finish   = -1;
    two.lagTime = 0;
    two.shown   = false;
    two.way[0]  = two.way[1] = 0;
    two.tracker[0].emplace(*checkpoints);
    two.tracker[1].emplace(*r.checkpoints);
    }
  effects->clearSecond(false);
  r.chase.reset(riderChase());
  }

void MainWindow::respawnRider() {
  // as the player's (0x508160): back to the last checkpoint passed
  auto& r = *two.rider;
  r.pose = r.checkpoints->respawn(r.car->full().bboxMin().y);
  r.vehicle->reset(r.pose);
  effects->clearSecond(true);
  sounds->play("Checkpoint","cp_reset");
  if(two.tracker[1])
    two.tracker[1]->relocate();
  r.lastPos    = r.pose.translation();
  r.upsideDown = 0;
  r.chase.reset(riderChase());
  }

ChaseCamera::Car MainWindow::riderChase() const {
  const auto& r = *two.rider;
  ChaseCamera::Car c;
  c.pose     = r.pose;
  c.centre   = r.vehicle ? r.vehicle->drawnCentre() : r.pose.translation();
  c.velocity = r.vehicle ? r.vehicle->velocity() : rc::Vec3{};
  c.spin     = r.vehicle ? r.vehicle->localSpin().length() : 0.f;
  const bool control = r.race && !r.race->holdCar();
  c.boost    = control && controls2.held(Controls::Boost,keys.data()) && controls2.held(Controls::Forw,keys.data());
  return c;
  }

Hud::Info MainWindow::riderHud() const {
  const auto& r = *two.rider;
  Hud::Info info;
  info.speed      = std::abs(r.vehicle->speed())*3.6f;
  info.turbo      = r.vehicle->boostLevel();
  info.turboMax   = r.vehicle->params().boostTank/100.f;
  info.speedMax   = r.vehicle->params().boostTopSpeed*3.6f;
  info.paused     = paused && !menu->active();
  info.carPos     = r.pose.translation();
  info.carForward = r.pose.vector({0,0,1});
  info.showMap    = showMap;
  info.showArrow  = false;
  info.endless    = two.lag;
  // the place by the way along the route (0x4ea2d0), the one finished first ahead
  info.place      = two.way[1]>two.way[0] ? 1 : 2;
  if(r.finish>=0 || playerFinish>=0)
    info.place = (r.finish>=0 && (playerFinish<0 || r.finish<playerFinish)) ? 1 : 2;
  info.opponents.push_back({carPose.translation(),carPose.vector({0,0,1})});
  return info;
  }

Hud::Split MainWindow::splitBar() const {
  // 0x4b09a0: mode 9 each one's part of the race (laps x the lap's length), the notches at the laps' ends; mode 10
  // the gap (0x4ea7b0: player 2's way against the player's) over "lagMetre", the one ahead at the far end
  Hud::Split s;
  s.lag  = two.lag;
  s.time = race ? race->time() : 0.f;
  const float lap = checkpoints ? checkpoints->lapLength() : 0.f;
  if(!two.lag) {
    const float all = lap*float(std::max(1,laps));
    for(int i=0; i<2; ++i)
      s.at[i] = all>0 ? std::clamp(two.way[i]/all,0.f,1.f) : 0.f;
    if(playerFinish>=0)
      s.at[0] = 1;
    if(two.rider->finish>=0)
      s.at[1] = 1;
    s.lo   = std::min(s.at[0],s.at[1]);
    s.hi   = std::max(s.at[0],s.at[1]);
    s.laps = laps;
    }
  else {
    const float f = std::clamp((two.way[1]-two.way[0])/std::max(1.f,two.lagMetre),-1.f,1.f);
    s.lo = 0.5f-std::abs(f)*0.5f;
    s.hi = 0.5f+std::abs(f)*0.5f;
    s.at[0] = f<=0 ? s.hi : s.lo;
    s.at[1] = f<=0 ? s.lo : s.hi;
    }
  return s;
  }

void MainWindow::tickTwo(float dt) {
  auto& r = *two.rider;
  r.pose = r.vehicle->pose();
  const rc::Vec3 at  = r.pose.translation();
  const size_t   was = r.checkpoints->next();
  const auto     ev  = r.race->update(dt,at,r.pose.vector({0,0,1}));
  if(ev==rc::Race::Event::Finish)
    r.finish = r.race->time();
  r.hud->event(ev);
  if(r.checkpoints->next()!=was) {
    sounds->play("Checkpoint","cp");
    r.vehicle->addBoost(rc::CarParams::BoostPerCheckpoint);
    }
  r.lastPos = at;

  // how far each has come (RouteTracker, as the opponents' standings)
  if(two.tracker[0] && two.tracker[1]) {
    two.way[0] = two.tracker[0]->update(carPose.translation(),checkpoints->laps());
    two.way[1] = two.tracker[1]->update(at,r.checkpoints->laps());
    }
  // 0x4eab80: the race of the gap; 0x4ead70 then ranks them by where they were
  if(two.lag && race->phase()==rc::Race::Phase::Racing && r.race->phase()==rc::Race::Phase::Racing) {
    if(std::abs(two.way[1]-two.way[0])>=two.lagMetre)
      two.lagTime += dt;
    if(two.lagTime>LagHold) {
      race->finishNow();
      r.race->finishNow();
      playerFinish = race->time();
      r.finish     = r.race->time();
      hud->event(rc::Race::Event::Finish);
      r.hud->event(rc::Race::Event::Finish);
      Log::i("two players: the gap held ",LagHold," s at ",Hud::formatTime(race->time()));
      }
    }

  // its dust and spray, its keys as the player's
  const bool driving = !r.race->holdCar();
  effects->tickSecond(dt,*r.vehicle,level->collision(),r.camera.position,
                      driving && controls2.held(Controls::Forw,keys.data()),
                      driving && controls2.held(Controls::Back,keys.data()));

  // back on the wheels, back to the last checkpoint: as the player's car
  r.upsideDown = r.pose.vector({0,1,0}).y<0.3f ? r.upsideDown+dt : 0;
  if(r.upsideDown>2.f) {
    r.vehicle->recover();
    r.upsideDown = 0;
    }
  const auto& grid = level->scene().collision;
  const bool  out  = !grid.empty() && (at.x<grid.min.x || at.x>grid.max.x || at.z<grid.min.z || at.z>grid.max.z ||
                                       at.y<grid.min.y-0.1f);
  if(out || r.vehicle->drowned() || r.vehicle->stuck())
    respawnRider();

  tickRiderSound(dt);
  const ChaseCamera::Car c = riderChase();
  r.chase.update(c,dt);
  r.chase.apply(c,&level->collision(),dt,r.camera);
  level->keepAboveWater(r.camera,time);
  }

void MainWindow::tickRiderSound(float dt) {
  auto& r = *two.rider;
  if(r.sound==nullptr)
    return;
  const bool  racing  = r.race->phase()==rc::Race::Phase::Racing;
  const bool  control = !r.race->holdCar();
  const float fwd     = r.vehicle->speed();
  auto held2 = [&](Controls::Action a) { return controls2.held(a,keys.data()); };
  r.airborne = !r.vehicle->grounded() ? r.airborne+dt : 0.f;
  rc::CarSound::Input in;
  in.speed    = std::abs(fwd)*3.6f;
  in.gas      = control && held2(Controls::Forw);
  in.back     = control && held2(Controls::Back);
  in.braking  = (in.back && fwd*3.6f>1.f) || (control && held2(Controls::Brake));
  in.rearSlip = r.vehicle->rearSlip();
  in.airborne = r.airborne>0.05f;
  in.racing   = racing;
  in.boost    = r.vehicle->boosting();
  in.jump     = control && held2(Controls::Jump);
  in.inWater  = r.vehicle->submerged()>0;
  in.ground   = r.vehicle->groundSound();
  in.contact  = {r.vehicle->contact().surface,r.vehicle->contact().hit,r.vehicle->contact().impact};
  in.carBump  = r.vehicle->carBump();
  r.sound->update(in,dt);
  }
