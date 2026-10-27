#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "championship.h"
#include "formats/playerprofile.h"

class Resources;

// The players of the game folder's Players\ and the one playing (RCCars.exe 0x4e7450 .. 0x4e8c80): who is
// chosen is kept in RCCars.crs (section "Player", "Name"), the faces to choose from are the pictures of
// RCCars.pack's FacesSys\ and the game folder's Faces\ folders, and a race's results go into the player's .scp
// like the original writes them.
class Players {
  public:
    static constexpr size_t None = size_t(-1);

    // `defaultName`: the player made when there is none (Language/Game 40507, "Gracz")
    Players(Resources& res, std::string_view defaultName);

    size_t                    size() const { return list.entries().size(); }
    rc::PlayerProfile&        operator[](size_t i) { return list.entries()[i].profile; }
    // the one playing, None if there is none (the original's after removing the one chosen)
    size_t                    currentIndex() const { return chosen; }
    rc::PlayerProfile*        current();
    // chosen: kept in RCCars.crs (0x404a30)
    void                      select(size_t i);
    size_t                    find(std::string_view name) const;

    enum class Result : uint8_t { Ok, Exists, Failed, NotSaved };
    // a new player (0x4d1ea6): the face is set afterwards with setFace
    Result                    create(std::string_view name, size_t& index);
    // the file deleted (0x4d1dbf); the one chosen is chosen no more
    bool                      remove(size_t i);
    bool                      save(size_t i);

    // the faces, sorted by their file names (0x4e5660)
    size_t                    faceCount() const { return faces.size(); }
    void                      setFace(size_t player, size_t face);
    // the picture a player without one gets: face1.tga, or the first
    size_t                    defaultFace() const;
    // the picture of that file (an AIPlayer<n> Face, 0x4e5a10), face1.tga's if there is none; nullptr if there are
    // no faces at all
    const std::vector<uint8_t>* faceByName(std::string_view file) const;
    // changes whenever a player's face does, for whoever keeps their pictures
    uint32_t                  faceVersion() const { return faceStamp; }

    // the race begins (0x4c9f70): its track, car, laps and skill remembered for the next time
    void                      raceStarted(int32_t track, int32_t car, int32_t laps, int32_t skill);
    // a lap of the player's (0x4c5560); true if it is the player's best on the track
    bool                      lapDone(int32_t track, int32_t car, float time);
    // the player's finish (0x4c56c0): the best race of that many laps, the time and the way driven; true if the
    // race is the best of that many laps
    bool                      raceDone(int32_t track, int32_t car, int32_t laps, float time, float distance);
    // and then in the championship (game mode 5, 0x4c56c0): the prize of the first three places with the bonus
    // to the money and the scores, the best place, the championship record; returns the prize
    int32_t                   championshipDone(int32_t track, int32_t car, int32_t place, int32_t bonus);

    const Championship&       championship() const { return champ; }
    // the player playing may race there (0x4e84a0)
    bool                      trackOpen(size_t track, bool championshipRace) const;
    // the championship race's fee paid (0x4c0b10 -> 0x4e7e70); false without the money
    bool                      payTariff(size_t track);
    // "Nowe mistrzostwa" (0x4e8320) and written
    void                      newChampionship();
    // the garage (0x4d6090, 0x4d6120, 0x4d4508 .., 0x4d62a0), written at once; false if it can't be done
    bool                      buyCar(size_t car);
    bool                      sellCar(size_t car);
    bool                      buyUpgrade(size_t car, Championship::Part part);
    bool                      sellUpgrade(size_t car, Championship::Part part);
    void                      nextSkin(size_t car, int32_t skins);

  private:
    struct Face {
      std::string          name;   // the file's
      std::vector<uint8_t> csi;
      };
    void                      loadFaces(Resources& res);
    void                      writeCurrent();

    Resources&                res;
    rc::PlayerList            list;
    std::vector<Face>         faces;
    size_t                    chosen    = None;
    uint32_t                  faceStamp = 0;
    Championship              champ;
  };
