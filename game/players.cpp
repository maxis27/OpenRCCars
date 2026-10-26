#include "players.h"

#include <Tempest/Log>

#include <algorithm>
#include <cctype>
#include <fstream>

#include "formats/settings.h"
#include "resources.h"

using namespace Tempest;

namespace {

std::string lower(std::string_view s) {
  std::string r(s);
  for(auto& c:r)
    c = char(std::tolower(uint8_t(c)));
  return r;
  }

std::vector<uint8_t> readFile(const std::filesystem::path& p) {
  std::ifstream f(p,std::ios::binary);
  if(!f)
    return {};
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());
  }

}

Players::Players(Resources& res, std::string_view defaultName) : res(res) {
  // Scripts\championship.ini: the money and the cars of a new player (0x4e8320)
  if(auto* e = res.pack().find("Scripts\\championship.ini")) {
    const auto d   = res.pack().read(*e);
    const auto ini = rc::KeySections::parse(std::string_view(reinterpret_cast<const char*>(d.data()),d.size()));
    if(auto& c = ini.get("Common","DefaultCash"); !c.empty())
      defaultCash = int32_t(c[0]);
    for(size_t i=0; i<defaultCars.size(); ++i)
      if(auto& c = ini.get("Car"+std::to_string(i+1),"IsEnabled"); !c.empty())
        defaultCars[i] = c[0]!=0;
    }
  loadFaces(res);
  list = rc::PlayerList::load(res.gameDir()/"Players");
  // no player at all: the default one made and written (0x4e7450)
  if(list.entries().empty()) {
    size_t at = None;
    if(create(defaultName,at)!=Result::Ok)
      Log::e("Players: can't create the player \"",std::string(defaultName),"\"");
    }
  const auto crs = readFile(res.gameDir()/"RCCars.crs");
  chosen = find(rc::ConfigFile::parse(crs).string("Player","Name"));
  // the one RCCars.crs names is gone: the first, rather than none, so that there is someone to race
  if(chosen==None && size()>0)
    select(0);
  if(auto* p = this->current())
    Log::i("player: ",p->name,", ",size()," in Players");
  }

void Players::loadFaces(Resources& res) {
  for(auto& e:res.pack().entries()) {
    const std::string n = lower(e.name);
    if(n.rfind("facessys\\",0)!=0 || n.size()<4 || n.substr(n.size()-4)!=".tga")
      continue;
    Face f;
    f.name = e.name.substr(9);
    f.csi  = rc::PlayerProfile::faceFromTga(res.pack().read(e));
    if(!f.csi.empty())
      faces.push_back(std::move(f));
    }
  std::error_code ec;
  for(auto& e:std::filesystem::directory_iterator(res.gameDir()/"Faces",ec)) {
    if(lower(e.path().extension().string())!=".tga")
      continue;
    Face f;
    f.name = e.path().filename().string();
    f.csi  = rc::PlayerProfile::faceFromTga(readFile(e.path()));
    if(!f.csi.empty())
      faces.push_back(std::move(f));
    }
  std::stable_sort(faces.begin(),faces.end(),[](const Face& a, const Face& b){ return lower(a.name)<lower(b.name); });
  if(faces.empty())
    Log::e("Players: no faces in FacesSys\\ or Faces\\");
  }

rc::PlayerProfile* Players::current() {
  return chosen<size() ? &list.entries()[chosen].profile : nullptr;
  }

size_t Players::find(std::string_view name) const {
  if(name.empty())
    return None;
  const std::string n = lower(name);
  for(size_t i=0; i<list.entries().size(); ++i)
    if(lower(list.entries()[i].profile.name)==n)
      return i;
  return None;
  }

void Players::select(size_t i) {
  chosen = i<size() ? i : None;
  writeCurrent();
  }

void Players::writeCurrent() {
  // RCCars.crs "Player" / "Name" (0x404a30), the rest of the file as it is
  const auto path = res.gameDir()/"RCCars.crs";
  const auto data = readFile(path);
  if(data.empty())
    return;
  const auto* p   = current();
  const auto  out = rc::ConfigFile::withString(data,"Player","Name",p!=nullptr ? p->name : std::string());
  if(out==data)
    return;
  std::ofstream f(path,std::ios::binary|std::ios::trunc);
  f.write(reinterpret_cast<const char*>(out.data()),std::streamsize(out.size()));
  if(!f)
    Log::e("Players: can't write RCCars.crs");
  }

Players::Result Players::create(std::string_view name, size_t& index) {
  if(find(name)!=None)
    return Result::Exists;
  const size_t face = defaultFace();
  auto* e = list.create(res.gameDir()/"Players",name,face<faces.size() ? faces[face].csi : std::vector<uint8_t>(),
                        defaultCash,defaultCars);
  if(e==nullptr)
    return list.entries().size()>=rc::PlayerList::MaxPlayers ? Result::Failed : Result::NotSaved;
  // 0x4d1edb: a new player starts on the first track and car, 5 laps, the low skill
  e->profile.lastLaps = 5;
  rc::PlayerList::save(*e);
  index = list.entries().size()-1;
  ++faceStamp;
  return Result::Ok;
  }

bool Players::remove(size_t i) {
  if(i>=size())
    return false;
  const std::string playing = chosen<size() ? list.entries()[chosen].profile.name : std::string();
  if(i==chosen)
    chosen = None;
  if(!list.remove(i))
    return false;
  chosen = find(playing);
  ++faceStamp;
  return true;
  }

bool Players::save(size_t i) {
  if(i>=size())
    return false;
  if(!rc::PlayerList::save(list.entries()[i])) {
    Log::e("Players: can't write ",list.entries()[i].file.string());
    return false;
    }
  return true;
  }

size_t Players::defaultFace() const {
  for(size_t i=0; i<faces.size(); ++i)
    if(lower(faces[i].name)=="face1.tga")
      return i;
  return faces.empty() ? None : 0;
  }

void Players::setFace(size_t player, size_t face) {
  if(player>=size() || face>=faces.size())
    return;
  list.entries()[player].profile.setFace(faces[face].csi);
  ++faceStamp;
  save(player);
  }

void Players::raceStarted(int32_t track, int32_t car, int32_t laps, int32_t skill) {
  auto* p = current();
  if(p==nullptr)
    return;
  p->lastTrack = track;
  p->lastCar   = car;
  p->lastLaps  = laps;
  p->lastSkill = skill;
  save(chosen);
  }

bool Players::lapDone(int32_t track, int32_t car, float time) {
  auto* p = current();
  if(p==nullptr || track<0 || size_t(track)>=rc::PlayerProfile::Tracks)
    return false;
  auto& best = p->tracks[size_t(track)].bestLap;
  if(!(time<best.time))
    return false;
  best = p->record(time,car);
  // 0x4c5560 keeps it in memory; the race's end writes the file, ours at once, so that a race left still has it
  save(chosen);
  return true;
  }

bool Players::raceDone(int32_t track, int32_t car, int32_t laps, float time, float distance) {
  auto* p = current();
  if(p==nullptr || track<0 || size_t(track)>=rc::PlayerProfile::Tracks)
    return false;
  auto& t = p->tracks[size_t(track)];
  bool best = false;
  if(laps>=0 && size_t(laps)<rc::PlayerProfile::RaceLists && time<t.bestRace[size_t(laps)].time) {
    t.bestRace[size_t(laps)] = p->record(time,car);
    best = true;
    }
  // 0x4c56c0: the time to the track and to the player's play, a race more, the way driven to both
  t.raceTime    += time;
  p->timePlayed += time;
  t.races       += 1;
  t.distance    += distance;
  p->distance   += distance;
  save(chosen);
  return best;
  }
