#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace rc { class PackFile; struct PlayerProfile; }

// The championship's rules as RCCars.exe keeps them (Scripts\championship.ini, read into 0x149a670): what the tracks
// cost and pay, which of them are open, what the cars and their upgrades cost, and what the player's file gets from
// a championship race (0x4c56c0). The garage's and the championship screens' sums are here too, so that the
// menus only show them.
class Championship {
  public:
    static constexpr size_t Tracks = 10;
    static constexpr size_t Cars   = 3;
    enum class Part : uint8_t { Boost, Resonator, Tires };

    struct Track {
      bool                   opened = false;   // IsOpened: open in a quick race whatever the scores
      int32_t                access = 0;       // AccessCash: the scores it needs
      int32_t                tariff = 0;       // RaceTariff: what a race costs
      std::array<int32_t,3>  prize  = {};      // Place1..3
      };
    struct Car {
      bool                   enabled = false;  // IsEnabled: a new player has it
      int32_t                price   = 0;      // AccessCash
      std::array<int32_t,3>  boost   = {};     // Boost-Car<n> Level1..3
      std::array<int32_t,3>  resonator = {};   // Resonator-Car<n> Level1..3
      std::array<int32_t,3>  tires   = {};     // Tires-Car<n> Level1..3 (read, but the garage does not use it)
      };

    static Championship load(rc::PackFile& pack);

    int32_t      defaultCash = 100;
    std::array<Track,Tracks> tracks;
    std::array<Car,Cars>     cars;

    // 0x4e84a0: open by IsOpened (not in the championship, flag 0x100 of 0x1499dd0), else by the player's scores;
    // the last track also needs every other track won
    bool         open(const rc::PlayerProfile& p, size_t track, bool championship) const;
    // 0x4e8580: the best place of every track but the last is the first
    static bool  allWon(const rc::PlayerProfile& p);
    // "Nowe mistrzostwa" (0x4e8320): DefaultCash, no scores, the cars of IsEnabled without upgrades, the first
    // track; the tracks' places and records stay
    void         restart(rc::PlayerProfile& p) const;

    // what an upgrade level 1..3 costs (0x4a0130, 0x4a0990): the garage prices the tires as the resonator, so does
    // this
    int32_t      upgradePrice(size_t car, Part part, int32_t level) const;
    // what the garage pays for something of that price (0x49fc40, 0x4a01b0, 0x4a05a0): 0.9 of it, cut
    static int32_t sale(int32_t price);
    // the car and its upgrades sold together (0x4d63f0)
    int32_t      carSale(const rc::PlayerProfile& p, size_t car) const;
    // the cheapest of the cars the player has not got but `car` (0x4d5e40), -1 if the player has another car
    int32_t      cheapestOther(const rc::PlayerProfile& p, size_t car) const;

    // the bonus of a championship finish (0x4c47e0): 5 a hit that filled the turbo, 5 a second of the gap to the
    // car behind, together no more than Place3
    struct Bonus {
      int32_t hits = 0, gap = 0;       // the hits and the seconds of the gap (rounded)
      int32_t hitPoints = 0, gapPoints = 0;
      int32_t total = 0;               // capped
      };
    Bonus        bonus(size_t track, int32_t hits, float gap) const;
  };
