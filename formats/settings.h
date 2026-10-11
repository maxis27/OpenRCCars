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

    const Slider* find(std::string_view key) const;
    // slider position as stored (FValues ignored), or `def` if the key is missing
    int32_t       raw (std::string_view key, int32_t def = 0) const;
    // slider position mapped to FValues for "Mask float" keys, otherwise the raw position
    float         real(std::string_view key, float def = 0) const;
    size_t        size() const { return keys.size(); }

  private:
    std::map<std::string,Slider,std::less<>> keys;
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

}
