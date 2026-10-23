#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rc {

// The sound channels of RCCars.sb (MOD_SNDCHANNEL nodes "SNCH", each a list of MOD_SOUND nodes "SOND"), e.g. channel
// "Checkpoint" = cp, cp_beside, cp_start, cp_reset, cp_wrongway. Other .sb files keep them inside their models (MODL),
// e.g. people.sb "Man" = man_voice, AIChars.sb "Dog" = dog_attack, dog_pain. A channel plays one of its sounds at a time; the game
// picks the sound by its index in the channel. The sounds are Sound\<name>.wav in RCCars.pack.
//
// What RCCars.exe makes of the fields (sound loader 0x4955f0, channel play 0x494400, volume 0x494d70):
//   SOND flags (0x08 element, bytes 16..19): 0x20 loops; 0x100 only starts in a silent channel (asked for while
//     anything plays, itself too, it is left out); 0x400 does not restart when asked for while it plays; 0x800 waits
//     for the sound playing in the channel to end instead of cutting it.
//   SOND 0xe7: fade-in, ms (from -20 dB); 0xe6: "tail", ms: a sound cut by the next one plays this much longer, fading
//     out, before the next one starts (motor_decel*: 1000).
//   SNCH 0xe0 / 0xe1: distance in metres where a positioned sound is down to -20 dB / still at full volume (0 = not
//     positioned: the player's own car, the checkpoint voice).
//   Sound\snd.dat (pack): a volume per sound name, in percent, played at (percent - 100) dB, e.g. motor_b_idle 86.
struct SoundBank {
  struct Sound {
    std::string name;            // file name without Sound\ and .wav
    uint32_t    flags    = 0;
    float       fadeIn   = 0;    // ms
    float       tail     = 0;    // ms
    float       volume   = 0;    // dB, from snd.dat

    bool        loop()      const { return (flags&0x20)!=0;  }
    bool        idleOnly()  const { return (flags&0x100)!=0; }
    bool        noRestart() const { return (flags&0x400)!=0; }
    bool        queue()     const { return (flags&0x800)!=0; }
    };
  struct Channel {
    std::string        name;
    std::string        model;    // the MODL it lies in (stone.sb "Whiskey2" has "Whiskey3"), empty if none
    float              far  = 0; // m, 0 = not positioned
    float              near = 0;
    std::vector<Sound> sounds;

    // index of the sound in this channel, -1 if missing
    int                find(std::string_view sound) const;
    };

  std::vector<Channel> channels;

  // RCCars.sb; `sndDat`: Sound\snd.dat from the pack (may be empty)
  static SoundBank load(const std::vector<uint8_t>& sb, const std::vector<uint8_t>& sndDat = {});

  const Channel* find(std::string_view channel) const;
  // the channel of a model, nullptr if it has none
  const Channel* ofModel(std::string_view model) const;
  };

}
