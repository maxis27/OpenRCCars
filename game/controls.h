#pragma once

#include <Tempest/Event>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace rc { class ConfigFile; }

// The keys of the car as RCCars.crs keeps them and "Dostosuj sterowanie" sets them (RCCars.exe 0x4e3110 ..,
// 0x4e48a0, 0x4c2f20 ..). Each of the 8 actions has two keys (the two columns of the page). The game's own key
// codes (its keyboard device, 0x49ca00) are not DirectInput's; the table here gives those of the keys a Tempest
// window reports, with the names the original shows.
class Controls {
  public:
    // the actions in the order of the records (0x4e4180: Player1 Boost .. Reset)
    enum Action : uint8_t { Boost, Left, Right, Forw, Back, Jump, Brake, Reset, Count };
    // "Input" / "Player<n>Layout": Klaw.1, Klaw.2, the joystick's three, the player's own
    enum Layout : int32_t { Keys1 = 0, Keys2 = 1, User = 5 };

    // one key of an action: device 1 the keyboard (0 none), its codes (one for a key; the joystick's take more)
    struct Slot {
      int32_t                device = 0;
      int32_t                count  = 0;
      std::array<int32_t,3>  codes  = {};
      bool    empty() const { return device==0 || count<=0; }
      // the keyboard's key, 0 if it is none (or not the keyboard's)
      int32_t key()   const { return device==1 && count>0 ? codes[0] : 0; }
      static Slot keyboard(int32_t code);
      bool    operator==(const Slot& o) const;
      };
    using Keys  = std::array<Slot,2>;
    using Table = std::array<Keys,Count>;

    // the names of the records ("Boost", "Left", ...)
    static const char* recordName(Action a);
    // Klaw.1 and Klaw.2 (0x4e3110: the arrows with Shift, Space, B, R; Tab, W A S D, Alt, Z, C)
    static Table preset(int32_t layout);
    // "Player1" / "Player2" of RCCars.crs, the preset Klaw.1 / Klaw.2 where a record is missing
    static Table read(const rc::ConfigFile& f, int player);
    // the 44 bytes of a record (0x4e48a0): [u32 1], then the two keys as [device][count][3 codes]
    static std::vector<uint8_t> record(const Keys& k);
    // `data` (RCCars.crs) with `player`'s records set to `t`
    static std::vector<uint8_t> write(const std::vector<uint8_t>& data, int player, const Table& t);

    // the game's key code of a key (0 none) and back
    static int32_t                  code(Tempest::Event::KeyType k);
    static Tempest::Event::KeyType  keyOf(int32_t code);
    // "Shift", "Space", "A" ...: what the original's keyboard calls a key
    static std::string              name(int32_t code);

    // the keys raced with: the layout chosen in RCCars.crs for player 1
    void load(const rc::ConfigFile& f);
    // `held`: Tempest's keys down, by KeyType
    bool held(Action a, const bool* held) const;
    bool is(Action a, Tempest::Event::KeyType k) const;

  private:
    Table table = preset(Keys1);
  };
