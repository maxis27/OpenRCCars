#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rc {

// Settings\*.ini: "slider configuration files" of the original tuning tool, e.g. Settings\engine1.ini
//   Key "speedBaseMax"
//   Values 1 100 33            <- slider min, max, position
//   Mask float                 <- optional: the slider maps linearly onto FValues
//   FValues 0.500000 2.000000
// "Mask spline" keys name a .bspl file instead of a value. The game also ships compiled .crs copies
// (names XOR 0x25), but they hold older values than the .ini files, so the .ini files are what we read.
class SliderFile {
  public:
    struct Slider {
      int32_t min = 0, max = 0, value = 0;
      bool    isFloat = false;
      float   fmin = 0, fmax = 0;
      };

    SliderFile() = default;
    static SliderFile parse(std::string_view text);
    // the compiled form (Settings\*.crs): [u16 tag][u32 end] records, 0x0220 opens a key, 0x4021 its name
    // (bytes XOR 0x25), 0x3023/0x3024/0x3025 min/max/value. Some settings ship only like this (surf_*.crs,
    // water_splash.crs, water_spray.crs, wtr_trace.crs).
    static SliderFile parseCrs(const std::vector<uint8_t>& data);

    const Slider* find(std::string_view key) const;
    // slider position as stored (FValues ignored), or `def` if the key is missing
    int32_t       raw (std::string_view key, int32_t def = 0) const;
    // slider position mapped to FValues for "Mask float" keys, otherwise the raw position
    float         real(std::string_view key, float def = 0) const;
    size_t        size() const { return keys.size(); }

  private:
    std::map<std::string,Slider,std::less<>> keys;
  };

// RCCars.crs in the game's folder: the player's settings ("CREAT Studio configuration", RCCars.exe 0x404770 ..).
// A tree of "CSRC" records, each [u32 length][name]; a section then has [u16 children] and 6 zeros, its children
// follow, a value [u16 type][u16 length][u16 capacity][u16 0], its data and [u32 0]. Types: 1 a 32-bit
// integer, 2 a string, 3 a structure (the keys of the controls).
class ConfigFile {
  public:
    static ConfigFile parse(const std::vector<uint8_t>& data);

    // the integer `key` of `section` ("Video", "VIDEO_RunInWindow"), `def` if it is missing
    int32_t     integer(std::string_view section, std::string_view key, int32_t def = 0) const;
    // the string `key` of `section` ("Player", "Name"), `def` if it is missing
    std::string string(std::string_view section, std::string_view key, std::string_view def = {}) const;

    // `data` with the string `key` of the top section `section` set to `value` the way RCCars.exe keeps it
    // (0x404a30: the length with its zero, the capacity as it was), the key or the section added when missing
    // with `capacity`; nothing else of the file changes
    static std::vector<uint8_t> withString(const std::vector<uint8_t>& data, std::string_view section,
                                           std::string_view key, std::string_view value, uint16_t capacity = 16);

  private:
    std::map<std::string,int32_t,std::less<>>     ints;      // "section/key"
    std::map<std::string,std::string,std::less<>> strings;
  };

// Scripts\upgrades.ini:
//   Section Keys "BOOSTERS"
//    OverkillUpgrades (float[4]) = 0.80, 0.97, 1.11, 1.24
//   End
class KeySections {
  public:
    static KeySections parse(std::string_view text);

    // values of `key` in `section`, empty if missing
    const std::vector<float>& get(std::string_view section, std::string_view key) const;

  private:
    std::map<std::string,std::map<std::string,std::vector<float>,std::less<>>,std::less<>> sections;
  };

// Splines\*.bspl written by the "Spline Editor": [u8 count?] then one 0x5b/0x80 element per control point
// holding three floats; the first is always 0, the other two are the point (x, y). Points are sorted by x.
// Evaluated piecewise linearly here (the original probably smooths them).
class Spline {
  public:
    struct Point { float x = 0, y = 0; };

    Spline() = default;
    explicit Spline(std::vector<Point> pts) : pts(std::move(pts)) {}
    static Spline parse(const std::vector<uint8_t>& data);

    bool                      empty()  const { return pts.empty(); }
    const std::vector<Point>& points() const { return pts; }
    float                     at(float x) const;

  private:
    std::vector<Point> pts;
  };

// Splines\*.spl (text) of the effects (dust_int.spl, dust_speed.spl, wspr_int.spl ...): the number of segments,
// then per segment its start point, two handles and its end point, as "x y" lines. RCCars.exe evaluates such
// curves as Hermite segments between the key points (0x40f830); the slopes are taken here from the handles: the
// start handle gives the slope leaving a key, the end handle the slope arriving at the next one. Outside the keys
// the curve keeps its end values.
class CurveSpline {
  public:
    struct Key { float x = 0, y = 0, in = 0, out = 0; };

    CurveSpline() = default;
    static CurveSpline parse(std::string_view text);

    bool  empty() const { return keys.empty(); }
    float at(float x) const;

  private:
    std::vector<Key> keys;
  };

}
