#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace rc {

// The game's timed scripts: RCCars.sb "Level Entry" (LevelEntry, RaceEntry, LevelFinish ...) and the tracks'
// "Scene Scripts" (DemoEntry), compiled by RCCars.exe 0x4e1080 and run by 0x4e1c20. A line is
//   <seconds> <command> [arguments]       e.g.  0 SetActive '$CAR' 2     100  LoadMap "Country_3" "DemoEntry"
// run when the script has been going that long ("Block" lines wait for a stage of the map loading). Comments
// start with "//". The arguments are kept as words: quotes ("..." and '...') taken off, "=" left out.
struct GameScript {
  struct Line {
    float                    time = 0;
    std::string              command;
    std::vector<std::string> args;
    };
  std::vector<Line> lines;

  // the first line with `command` (case ignored), nullptr if none
  const Line* find(std::string_view command) const;

  static GameScript parse(std::string_view text);
  };

}
