#include "championship.h"

#include <Tempest/Log>

#include <algorithm>
#include <cmath>
#include <string>

#include "formats/pack.h"
#include "formats/playerprofile.h"
#include "formats/settings.h"

using namespace Tempest;

Championship Championship::load(rc::PackFile& pack) {
  Championship c;
  auto* e = pack.find("Scripts\\championship.ini");
  if(e==nullptr) {
    Log::e("Scripts\\championship.ini: not in the pack, the championship has no tracks open");
    return c;
    }
  const auto d   = pack.read(*e);
  const auto ini = rc::KeySections::parse(std::string_view(reinterpret_cast<const char*>(d.data()),d.size()));
  auto value = [&ini](const std::string& section, const char* key, int32_t def) {
    auto& v = ini.get(section,key);
    return v.empty() ? def : int32_t(v[0]);
    };
  c.defaultCash = value("Common","DefaultCash",c.defaultCash);
  for(size_t i=0; i<Tracks; ++i) {
    const std::string s = "Track"+std::to_string(i+1);
    auto& t = c.tracks[i];
    t.opened = value(s,"IsOpened",0)!=0;
    t.access = value(s,"AccessCash",0);
    t.tariff = value(s,"RaceTariff",0);
    for(size_t p=0; p<3; ++p)
      t.prize[p] = value(s,("Place"+std::to_string(p+1)).c_str(),0);
    }
  for(size_t i=0; i<Cars; ++i) {
    const std::string n = std::to_string(i+1);
    auto& car = c.cars[i];
    car.enabled = value("Car"+n,"IsEnabled",i==0 ? 1 : 0)!=0;
    car.price   = value("Car"+n,"AccessCash",0);
    for(size_t l=0; l<3; ++l) {
      const std::string key = "Level"+std::to_string(l+1);
      car.boost[l]     = value("Boost-Car"+n,key.c_str(),0);
      car.resonator[l] = value("Resonator-Car"+n,key.c_str(),0);
      car.tires[l]     = value("Tires-Car"+n,key.c_str(),0);
      }
    }
  return c;
  }

bool Championship::open(const rc::PlayerProfile& p, size_t track, bool championship) const {
  if(track>=Tracks)
    return false;
  if(!championship && tracks[track].opened)
    return true;
  if(track==Tracks-1 && !allWon(p))
    return false;
  return tracks[track].access<=p.scores;
  }

bool Championship::allWon(const rc::PlayerProfile& p) {
  for(size_t i=0; i+1<Tracks; ++i)
    if(p.tracks[i].bestPlace!=0)
      return false;
  return true;
  }

void Championship::restart(rc::PlayerProfile& p) const {
  p.cash   = defaultCash;
  p.scores = 0;
  for(size_t i=0; i<Cars; ++i) {
    p.cars[i] = rc::PlayerProfile::Car();
    p.cars[i].bought = cars[i].enabled;
    }
  p.lastTrack = 0;
  }

int32_t Championship::upgradePrice(size_t car, Part part, int32_t level) const {
  if(car>=Cars || level<1 || level>3)
    return 0;
  // 0x4d6840 and 0x4d7130 both ask 0x4a0990 (Resonator-Car<n>): Tires-Car<n> is read by the chat console only
  const auto& c = cars[car];
  return part==Part::Boost ? c.boost[size_t(level-1)] : c.resonator[size_t(level-1)];
  }

int32_t Championship::sale(int32_t price) {
  // fild, fmul 0.9f, __ftol: $50 sells for $44
  return int32_t(double(price)*double(0.9f));
  }

int32_t Championship::carSale(const rc::PlayerProfile& p, size_t car) const {
  if(car>=Cars)
    return 0;
  const auto& c = p.cars[car];
  int32_t sum = sale(cars[car].price);
  for(int32_t l=1; l<=c.boost; ++l)
    sum += sale(upgradePrice(car,Part::Boost,l));
  for(int32_t l=1; l<=c.resonator; ++l)
    sum += sale(upgradePrice(car,Part::Resonator,l));
  for(int32_t l=1; l<=c.tires; ++l)
    sum += sale(upgradePrice(car,Part::Tires,l));
  return sum;
  }

int32_t Championship::cheapestOther(const rc::PlayerProfile& p, size_t car) const {
  int32_t best = car<Cars ? cars[car].price : 0;
  for(size_t i=0; i<Cars; ++i) {
    if(i==car)
      continue;
    if(p.cars[i].bought)
      return -1;
    best = std::min(best,cars[i].price);
    }
  return best;
  }

Championship::Bonus Championship::bonus(size_t track, int32_t hits, float gap) const {
  Bonus b;
  b.hits      = hits;
  b.gap       = int32_t(gap+0.5f);
  b.hitPoints = b.hits*5;
  b.gapPoints = b.gap*5;
  const int32_t cap = track<Tracks ? tracks[track].prize[2] : 0;
  b.total = std::min(b.hitPoints+b.gapPoints,cap);
  return b;
  }
