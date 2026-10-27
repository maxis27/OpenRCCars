#pragma once

#include <array>
#include <climits>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace rc {

// Players\<name>.scp: a player of the original game (RCCars.exe 0x4e7740 reads it, 0x4e7e70 writes it; the old
// file is kept as <name>.bak first, and a .scp that does not read is deleted and the .bak read instead).
// The chunks of the scenes ([u8 id][u8 type][u32 end]), the type in the high byte of the tags below:
//   0x5020 root [f32 1.0]
//     0x4021 name (at most 15 letters)       0x1422 [u32 160] the face's .csi header, 0x1423 [u32 n] its pixels
//     0x5024 time played, s                  0x3025 cash          0x3026 scores        0x5027 distance driven, m
//     0x3230 [u32 car 0..2] car: 0x0031 bought, 0x3032 boost, 0x3033 resonator, 0x3034 tires, 0x3035 skin
//     0x3240 [u32 track 0..9] track: 0x0270 best lap, 0x0279 the best championship race (its "time" is the
//            money it won, 0x4c56c0), 0x3271 [u32 i] the best race of i laps (0..19), 0x5044 time raced, s,
//            0x3045 races, 0x3046 best place (0 = first), 0x5047 distance, m
//     0x3050 last track, 0x3051 last car, 0x3052 last laps, 0x3053 last skill  (quick and ghost race)
//     0x4060 a name (the player's own when created), 0x3061 ?
// A record (0x0270 ...): 0x5072 time, 0x3073 car, 0x3074 boost, 0x3075 resonator, 0x3076 tires,
// 0x2477 [u32 4] day, month, year, 0 and 0x2478 [u32 4] hour, minute, second, ms of when it was made.
struct PlayerProfile {
  static constexpr float   NoTime    = 2000000.f;   // a record not made yet (0x49f42400)
  static constexpr int32_t NoPlace   = INT32_MAX;
  static constexpr size_t  Cars      = 3;
  static constexpr size_t  Tracks    = 10;
  static constexpr size_t  RaceLists = 20;

  struct Car {
    bool    bought    = false;
    int32_t boost     = 0;   // upgrade levels 0..3
    int32_t resonator = 0;
    int32_t tires     = 0;
    int32_t skin      = 0;
    };
  struct Record {
    float    time  = NoTime;
    int32_t  car   = 0;
    int32_t  boost = 0, resonator = 0, tires = 0;
    std::array<uint16_t,4> date  = {};   // day, month, year, 0
    std::array<uint16_t,4> clock = {};   // hour, minute, second, ms
    bool     made() const { return time<NoTime; }
    };
  struct Track {
    Record                       bestLap;
    Record                       champion;  // 0x0279: `time` the money won
    std::array<Record,RaceLists> bestRace;   // by the number of laps
    float                        raceTime  = 0;
    int32_t                      races     = 0;
    int32_t                      bestPlace = NoPlace;
    float                        distance  = 0;
    };

  std::string            name;
  std::vector<uint8_t>   faceHeader;   // 160 bytes, a .csi header ("MISC"), empty: no face
  std::vector<uint8_t>   facePixels;
  float                  timePlayed = 0;
  int32_t                cash       = 0;
  int32_t                scores     = 0;
  float                  distance   = 0;
  std::array<Car,Cars>   cars;
  std::array<Track,Tracks> tracks;
  int32_t                lastTrack  = 0;
  int32_t                lastCar    = 0;
  int32_t                lastLaps   = 3;
  int32_t                lastSkill  = 0;
  std::string            createdAs;
  int32_t                unknown61  = 0;

  // throws FormatError
  static PlayerProfile   parse(const std::vector<uint8_t>& data);
  std::vector<uint8_t>   serialize() const;

  // the face as a .csi (header + pixels), empty if there is none
  std::vector<uint8_t>   faceCsi() const;
  void                   setFace(const std::vector<uint8_t>& csi);
  // a face picture of FacesSys\ or Faces\ (an uncompressed 32 bit .tga) as the .csi a .scp keeps; empty if it
  // is not such a picture
  static std::vector<uint8_t> faceFromTga(const std::vector<uint8_t>& tga);

  // the ranks of 0x4e85d0 (Language/Game 40550 ..): scores, and how many tracks were raced
  uint32_t               rankText() const;
  // a record stamped with the local time now (0x4c5560)
  Record                 record(float time, int32_t car) const;
  };

// the players of the game folder's Players\ (0x4e7450): every *.scp, in the order of the folder
class PlayerList {
  public:
    struct Entry {
      std::filesystem::path file;
      PlayerProfile         profile;
      };

    // the .scp files of `dir`; one that does not read is replaced by its .bak like the original does
    static PlayerList     load(const std::filesystem::path& dir);

    std::vector<Entry>&       entries()       { return list; }
    const std::vector<Entry>& entries() const { return list; }
    Entry*                find(std::string_view name);
    // the file a new player of that name gets (0x4e7c60: letters only 7 bits, '?' and the like as digits)
    static std::string    fileName(std::string_view name);
    // writes the player, the old file kept as .bak (0x4e7e70); false if it could not be written
    static bool           save(const Entry& e);
    // a new player in `dir` (0x4e7c60): `cash` to start with, the cars championship.ini opens; nullptr if
    // there are 20 already or the file could not be written
    Entry*                create(const std::filesystem::path& dir, std::string_view name,
                                 const std::vector<uint8_t>& faceCsi, int32_t cash, const std::array<bool,3>& cars);
    // the player's .scp deleted and the player left out (0x4e8250), its .bak kept
    bool                  remove(size_t index);

    static constexpr size_t MaxPlayers = 20;

  private:
    std::vector<Entry>    list;
  };

}
