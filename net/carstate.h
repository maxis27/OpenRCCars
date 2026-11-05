#pragma once

#include <cstdint>
#include <deque>
#include <vector>

#include "formats/carprofile.h"

namespace rc::net {

// A player's car as the others see it. Every player drives its own car and sends what it does a few times a second
// (RCCars.exe kind 0, 35 bytes from 0x4f4a60, 4/6/10/15/25 a second by the connection chosen, 0x571cac); the others
// show it a little behind, between the last two states they got, as a ghost shows its lap (Ghosts::Ghost::show).
//
// Ours is not the original's packet: it is a CarProfile::Frame, the state a recording keeps (RCCars.exe 0x4f5180),
// packed into 55 bytes:
//   f32 time (the sender's race clock, s), 3 x f32 position, 4 x i16 rotation (w x y z, /32767),
//   3 x i16 velocity (cm/s), 3 x i16 angular velocity (rad/s /1000), 6 x u16 spring lengths (m /100000),
//   i16 the wheels' angle (degrees /100), u32 keys, u8 flags
namespace CarState {
  static constexpr size_t  Size        = 55;
  // the connection's rates of the original, the last one (LAN) ours by default
  static constexpr float   Rates[]     = {4,6,10,15,25};
  static constexpr float   DefaultRate = 25;

  enum Flags : uint8_t {
    Boosting = 0x01,   // the turbo flame and sound
    };

  std::vector<uint8_t> pack(const CarProfile::Frame& f, uint8_t flags);
  // false if it is not one
  bool                 unpack(const std::vector<uint8_t>& data, CarProfile::Frame& f, uint8_t& flags);
  }

// The states of one other player's car as they come, and where to show it now. The sender's race clock is mapped to
// ours by the fastest a state ever came (a late packet must not pull the car back); the car is shown `delay` behind
// that, between the two states around it, and moved on along its velocity for at most MaxAhead when the next state
// is late or lost.
class RemoteCar {
  public:
    static constexpr float MaxAhead = 0.25f;   // s
    // how long the states are kept
    static constexpr float Keep     = 1.f;

    // `rate`: states a second the sender sends
    explicit RemoteCar(float rate = CarState::DefaultRate);

    // a state that came at `now` (our clock, s)
    void                     receive(const CarProfile::Frame& f, uint8_t flags, float now);
    // anything to show yet
    bool                     ready() const { return !list.empty(); }
    // the car at `now` (our clock); the last state as it was before any came
    CarProfile::Frame        at(float now) const;
    uint8_t                  flags() const { return lastFlags; }
    // s since the newest state came, a large value before any did
    float                    silence(float now) const { return list.empty() ? 1e9f : now-gotAt; }
    // s behind the sender the car is shown
    float                    delay() const { return lag; }
    // the sender started again (a new race): forget the states
    void                     clear();

    // model -> world of a frame
    static Mat4              pose(const CarProfile::Frame& f);

  private:
    std::deque<CarProfile::Frame> list;
    float                    offset    = 0;    // the sender's clock - ours, at the fastest
    bool                     synced    = false;
    float                    lag       = 0;
    float                    gotAt     = 0;
    uint8_t                  lastFlags = 0;
  };

}
