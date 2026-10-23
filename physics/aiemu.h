#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <random>
#include <utility>
#include <vector>

#include "formats/math3d.h"

namespace rc {

class SliderFile;

// RCCars.exe's "AI emulation" (GMAIEMU, 0x4a0e70 .. 0x4a1440): what the computer drivers seem to run into. The
// opponents replay recorded laps and never touch the people, dogs or road cars, so the game makes up the encounters
// for the race's chat console. People (0x5108f0), dogs (0x51a220) and road cars (0x5177d0) each register a trap
// that is checked every `period` seconds while the thing is there (0x517800) against every opponent that is racing
// (0x4a1380: a computer car that has not finished): one within 3.5 m of it whose last such encounter is long enough
// ago gets it with the chance ProbCheck<n> % of Settings\AIEmu.ini, n being the opponent's number (0x4a11a0); the
// trap's callback only writes "<name> zadziera ze złym chłopcem" (people), "<name> pobity przez psa" (dogs) or
// "<name> to kochaś zderzaków" (road cars). The car itself drives on.
// Not ported: the same system also puts unseen opponents back on the track (DistCam m from every camera, half of it
// behind one) near a car_reset volume or deep water, with the chances ProbAccum<n> (0x509770, 0x51d1c0).
class AiEmu {
  public:
    enum class Trap : uint8_t { Person, Dog, RoadCar };
    // a person, dog or road car of the track; `id` tells the same one apart from frame to frame. `there`: the
    // trap is armed (0x517800: people near the player, a road car on its road ...)
    struct Spot {
      Trap     kind = Trap::Person;
      uint32_t id   = 0;
      Vec3     pos;
      bool     there = true;
      };
    // an opponent: its number in the race (0..), where it is, and whether it is still racing
    struct Car {
      Vec3 pos;
      bool racing = false;
      };
    struct Encounter {
      size_t car = 0;
      Trap   kind = Trap::Person;
      };

    static constexpr float Reach = 3.5f;                     // m, 0x554b00 = 12.25 m^2
    static constexpr float Period[3]   = {0.7f, 1.1f, 0.7f}; // s between the checks of one trap
    static constexpr float Cooldown[3] = {20.f, 20.f, 10.f}; // s an opponent waits for its next one of a kind

    AiEmu();
    // Settings\AIEmu.ini; the defaults are the shipped values
    void load(const SliderFile& ini);
    // a new race: every opponent's last encounter at race time 0 (as RCCars.exe zeroes them with the car)
    void restart();
    // `dt` seconds to race time `now`; returns the encounters of this update
    std::vector<Encounter> update(float dt, float now, const std::vector<Spot>& spots, const std::vector<Car>& cars);

  private:
    std::array<float,6>                         chance = {10,15,22,30,40,50};  // ProbCheck1..6, %
    std::map<std::pair<Trap,uint32_t>,float>    timers;  // s to a trap's next check
    std::vector<std::array<float,3>>            last;    // per opponent: race time of its last encounter, by kind
    std::mt19937                                rng{20061014};
  };

}
