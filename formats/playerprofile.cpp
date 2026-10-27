#include "playerprofile.h"

#include "chunk.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <ctime>
#include <fstream>
#include <random>

using namespace rc;

namespace {

std::vector<uint8_t> readFile(const std::filesystem::path& p) {
  std::ifstream f(p,std::ios::binary);
  if(!f)
    return {};
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());
  }

// the chunk's 16 bit tag as RCCars.exe compares it: the id in the low byte, the type in the high one
uint16_t tag(const Chunk& c) {
  return uint16_t(c.id | (c.type<<8));
  }

// children of a chunk whose payload starts with `prefix` bytes
Range kids(const Chunk& c, size_t prefix) {
  return c.children(prefix);
  }

std::string text(const Chunk& c, size_t max) {
  std::string s(c.str());
  if(s.size()>max)
    s.resize(max);
  return s;
  }

void readRecord(const Chunk& c, size_t prefix, PlayerProfile::Record& r) {
  for(auto& k:kids(c,prefix)) {
    switch(tag(k)) {
      case 0x5072: r.time      = k.f32(); break;
      case 0x3073: r.car       = k.i32(); break;
      case 0x3074: r.boost     = k.i32(); break;
      case 0x3075: r.resonator = k.i32(); break;
      case 0x3076: r.tires     = k.i32(); break;
      case 0x2477:
      case 0x2478: {
        if(k.size()<12 || k.u32()!=4)
          break;
        auto& dst = tag(k)==0x2477 ? r.date : r.clock;
        for(size_t i=0; i<4; ++i)
          dst[i] = uint16_t(k.data()[4+i*2] | k.data()[5+i*2]<<8);
        break;
        }
      }
    }
  }

// the writer: chunks are opened and their end filled in when closed (tiohbWriteChunk / tiohbUpdateChunk)
class Writer {
  public:
    std::vector<uint8_t> out;

    size_t open(uint16_t t) {
      const size_t at = out.size();
      out.push_back(uint8_t(t&0xff));
      out.push_back(uint8_t(t>>8));
      put32(0);
      return at;
      }
    void close(size_t at) {
      const uint32_t end = uint32_t(out.size());
      std::memcpy(out.data()+at+2,&end,4);
      }
    void put32(uint32_t v) {
      uint8_t b[4];
      std::memcpy(b,&v,4);
      out.insert(out.end(),b,b+4);
      }
    void putF(float v) {
      uint32_t u;
      std::memcpy(&u,&v,4);
      put32(u);
      }
    void dword(uint16_t t, int32_t v) { auto c = open(t); put32(uint32_t(v)); close(c); }
    void real (uint16_t t, float v)   { auto c = open(t); putF(v);            close(c); }
    void str  (uint16_t t, const std::string& s) {
      auto c = open(t);
      out.insert(out.end(),s.begin(),s.end());
      out.push_back(0);
      close(c);
      }
    void blob (uint16_t t, const std::vector<uint8_t>& d) {
      auto c = open(t);
      put32(uint32_t(d.size()));
      out.insert(out.end(),d.begin(),d.end());
      close(c);
      }
    void words(uint16_t t, const std::array<uint16_t,4>& w) {
      auto c = open(t);
      put32(4);
      for(auto v:w) {
        out.push_back(uint8_t(v&0xff));
        out.push_back(uint8_t(v>>8));
        }
      close(c);
      }
    // 0x4e8b50
    void record(uint16_t t, const PlayerProfile::Record& r, int32_t index) {
      auto c = open(t);
      if(index>=0)
        put32(uint32_t(index));
      real (0x5072,r.time);
      dword(0x3073,r.car);
      dword(0x3074,r.boost);
      dword(0x3075,r.resonator);
      dword(0x3076,r.tires);
      words(0x2477,r.date);
      words(0x2478,r.clock);
      close(c);
      }
  };

}

PlayerProfile PlayerProfile::parse(const std::vector<uint8_t>& data) {
  PlayerProfile p;
  if(data.size()<10)
    throw FormatError("scp: too small");
  Range top(data.data(),0,data.size());
  auto root = *top.begin();
  if(tag(root)!=0x5020)
    throw FormatError("scp: not a player file");
  // a fresh player's lists (0x4e8b00 with 1) unless the file has them
  for(auto& t:p.tracks)
    t = Track();

  for(auto& c:kids(root,4)) {
    switch(tag(c)) {
      case 0x4021: p.name       = text(c,15); break;
      case 0x1422: {
        if(c.u32()==160)
          p.faceHeader.assign(c.data()+4,c.data()+4+160);
        break;
        }
      case 0x1423: {
        const uint32_t n = c.u32();
        if(4+size_t(n)<=c.size())
          p.facePixels.assign(c.data()+4,c.data()+4+n);
        break;
        }
      case 0x5024: p.timePlayed = c.f32(); break;
      case 0x3025: p.cash       = c.i32(); break;
      case 0x3026: p.scores     = c.i32(); break;
      case 0x5027: p.distance   = c.f32(); break;
      case 0x3230: {
        const uint32_t i = c.u32();
        if(i>=Cars)
          break;
        Car car;
        for(auto& k:kids(c,4)) {
          switch(tag(k)) {
            case 0x0031: car.bought    = true;    break;
            case 0x3032: car.boost     = k.i32(); break;
            case 0x3033: car.resonator = k.i32(); break;
            case 0x3034: car.tires     = k.i32(); break;
            case 0x3035: car.skin      = k.i32(); break;
            }
          }
        p.cars[i] = car;
        break;
        }
      case 0x3240: {
        const uint32_t i = c.u32();
        if(i>=Tracks)
          break;
        Track& t = p.tracks[i];
        t = Track();
        for(auto& k:kids(c,4)) {
          switch(tag(k)) {
            case 0x0270: readRecord(k,0,t.bestLap);  break;
            case 0x0279: readRecord(k,0,t.champion); break;
            case 0x3271: {
              const uint32_t n = k.u32();
              if(n<RaceLists)
                readRecord(k,4,t.bestRace[n]);
              break;
              }
            case 0x5443: {
              // an older form: only the times of the 20 lists
              for(size_t n=0; n<RaceLists && 4+n*4+4<=k.size(); ++n)
                t.bestRace[n].time = k.f32(4+n*4);
              break;
              }
            case 0x5044: t.raceTime  = k.f32(); break;
            case 0x3045: t.races     = k.i32(); break;
            case 0x3046: t.bestPlace = k.i32(); break;
            case 0x5047: t.distance  = k.f32(); break;
            }
          }
        break;
        }
      case 0x3050: p.lastTrack = c.i32(); break;
      case 0x3051: p.lastCar   = c.i32(); break;
      case 0x3052: p.lastLaps  = c.i32(); break;
      case 0x3053: p.lastSkill = c.i32(); break;
      case 0x4060: p.createdAs = text(c,255); break;
      case 0x3061: p.unknown61 = c.i32(); break;
      }
    }
  return p;
  }

std::vector<uint8_t> PlayerProfile::serialize() const {
  Writer w;
  auto root = w.open(0x5020);
  w.putF(1.f);
  w.str(0x4021,name);
  if(faceHeader.size()==160) {
    w.blob(0x1422,faceHeader);
    w.blob(0x1423,facePixels);
    }
  w.real (0x5024,timePlayed);
  w.dword(0x3025,cash);
  w.dword(0x3026,scores);
  w.real (0x5027,distance);
  for(size_t i=0; i<Cars; ++i) {
    auto c = w.open(0x3230);
    w.put32(uint32_t(i));
    if(cars[i].bought) {
      w.close(w.open(0x0031));
      w.dword(0x3032,cars[i].boost);
      w.dword(0x3033,cars[i].resonator);
      w.dword(0x3034,cars[i].tires);
      w.dword(0x3035,cars[i].skin);
      }
    w.close(c);
    }
  for(size_t i=0; i<Tracks; ++i) {
    const Track& t = tracks[i];
    auto c = w.open(0x3240);
    w.put32(uint32_t(i));
    w.record(0x0270,t.bestLap,-1);
    for(size_t n=0; n<RaceLists; ++n)
      w.record(0x3271,t.bestRace[n],int32_t(n));
    w.record(0x0279,t.champion,-1);
    w.real (0x5044,t.raceTime);
    w.dword(0x3045,t.races);
    w.dword(0x3046,t.bestPlace);
    w.real (0x5047,t.distance);
    w.close(c);
    }
  w.dword(0x3050,lastTrack);
  w.dword(0x3051,lastCar);
  w.dword(0x3052,lastLaps);
  w.dword(0x3053,lastSkill);
  w.str  (0x4060,createdAs);
  w.dword(0x3061,unknown61);
  w.close(root);
  return std::move(w.out);
  }

std::vector<uint8_t> PlayerProfile::faceCsi() const {
  if(faceHeader.size()!=160 || facePixels.empty())
    return {};
  std::vector<uint8_t> ret = faceHeader;
  ret.insert(ret.end(),facePixels.begin(),facePixels.end());
  return ret;
  }

void PlayerProfile::setFace(const std::vector<uint8_t>& csi) {
  faceHeader.clear();
  facePixels.clear();
  if(csi.size()<160)
    return;
  uint32_t hdr = 160;
  std::memcpy(&hdr,csi.data()+8,4);
  if(hdr<160 || hdr>csi.size())
    return;
  // the header as RCCars.exe keeps it (csiCreateImage of 160 bytes), then the top level only
  uint32_t w = 0, h = 0, bpp = 0;
  std::memcpy(&w,  csi.data()+0x10,4);
  std::memcpy(&h,  csi.data()+0x14,4);
  std::memcpy(&bpp,csi.data()+0x18,4);
  const size_t n = size_t(w)*h*bpp;
  if(hdr+n>csi.size())
    return;
  faceHeader.assign(csi.begin(),csi.begin()+160);
  std::memcpy(faceHeader.data()+8,"\xa0\0\0\0",4);
  uint32_t one = 1;
  std::memcpy(faceHeader.data()+0x1c,&one,4);   // one level
  facePixels.assign(csi.begin()+hdr,csi.begin()+hdr+n);
  }

std::vector<uint8_t> PlayerProfile::faceFromTga(const std::vector<uint8_t>& tga) {
  // an uncompressed true colour picture (type 2) of 32 bits; its rows bottom up unless bit 5 of the descriptor
  if(tga.size()<18 || tga[2]!=2 || tga[16]!=32)
    return {};
  const uint32_t w = uint32_t(tga[12] | tga[13]<<8), h = uint32_t(tga[14] | tga[15]<<8);
  const size_t   at = 18+size_t(tga[0]);
  const size_t   n  = size_t(w)*h*4;
  if(w==0 || h==0 || at+n>tga.size())
    return {};
  // the header RCCars.exe's loader makes of it (as kept in a .scp, its pointers left out): "MISC", 160 bytes,
  // the size, 4 bytes a pixel, one level, 4 channels "ARGB" in the bits 24..31, 16..23, 8..15 and 0..7
  std::vector<uint8_t> ret(160,0);
  auto put = [&ret](size_t o, uint32_t v) { std::memcpy(ret.data()+o,&v,4); };
  std::memcpy(ret.data(),"MISC",4);
  put(0x08,160);
  put(0x10,w);
  put(0x14,h);
  put(0x18,4);
  put(0x1c,1);
  put(0x20,4);
  std::memcpy(ret.data()+0x24,"ARGB",4);
  const uint8_t bits[8] = {0x18,0x1f,0x10,0x17,0x08,0x0f,0x00,0x07};
  std::memcpy(ret.data()+0x2c,bits,8);
  // the pixels top down, the bytes as they are (B, G, R, A)
  const bool topDown = (tga[17]&0x20)!=0;
  ret.resize(160+n);
  for(uint32_t y=0; y<h; ++y) {
    const size_t src = at+size_t(topDown ? y : h-1-y)*w*4;
    std::memcpy(ret.data()+160+size_t(y)*w*4,tga.data()+src,size_t(w)*4);
    }
  return ret;
  }

uint32_t PlayerProfile::rankText() const {
  int raced = 0;
  for(auto& t:tracks)
    if(t.races>0)
      ++raced;
  if(scores<50 && cash<50 && raced>1)
    return 40550;
  static const int32_t limits[] = {100,250,500,1000,2000,4000,7000,10000};
  for(uint32_t i=0; i<8; ++i)
    if(scores<limits[i])
      return 40551+i;
  return 40559;
  }

PlayerProfile::Record PlayerProfile::record(float time, int32_t car) const {
  Record r;
  r.time = time;
  r.car  = car;
  if(car>=0 && size_t(car)<Cars) {
    r.boost     = cars[size_t(car)].boost;
    r.resonator = cars[size_t(car)].resonator;
    r.tires     = cars[size_t(car)].tires;
    }
  const std::time_t now = std::time(nullptr);
  std::tm tm = {};
#ifdef _WIN32
  localtime_s(&tm,&now);
#else
  localtime_r(&now,&tm);
#endif
  r.date  = {uint16_t(tm.tm_mday),uint16_t(tm.tm_mon+1),uint16_t(tm.tm_year+1900),0};
  r.clock = {uint16_t(tm.tm_hour),uint16_t(tm.tm_min),uint16_t(tm.tm_sec),0};
  return r;
  }

PlayerList PlayerList::load(const std::filesystem::path& dir) {
  PlayerList ret;
  std::error_code ec;
  std::vector<std::filesystem::path> files;
  for(auto& e:std::filesystem::directory_iterator(dir,ec)) {
    auto ext = e.path().extension().string();
    for(auto& ch:ext)
      ch = char(std::tolower(uint8_t(ch)));
    if(ext==".scp")
      files.push_back(e.path());
    }
  std::sort(files.begin(),files.end());
  for(auto& f:files) {
    Entry e;
    e.file = f;
    try {
      e.profile = PlayerProfile::parse(readFile(f));
      }
    catch(const std::exception&) {
      // 0x4e7740: the broken .scp is deleted and the .bak read in its place
      auto bak = f;
      bak.replace_extension(".bak");
      std::filesystem::remove(f,ec);
      try {
        e.profile = PlayerProfile::parse(readFile(bak));
        }
      catch(const std::exception&) {
        continue;
        }
      }
    if(e.profile.name.empty())
      e.profile.name = f.stem().string().substr(0,15);
    ret.list.push_back(std::move(e));
    }
  return ret;
  }

PlayerList::Entry* PlayerList::find(std::string_view name) {
  auto lower = [](std::string_view s) {
    std::string r(s);
    for(auto& ch:r)
      ch = char(std::tolower(uint8_t(ch)));
    return r;
    };
  const std::string n = lower(name);
  for(auto& e:list)
    if(lower(e.profile.name)==n)
      return &e;
  return nullptr;
  }

std::string PlayerList::fileName(std::string_view name) {
  static std::minstd_rand rnd(std::random_device{}());
  std::string r;
  for(char ch:name) {
    const uint8_t c = uint8_t(ch)&0x7f;
    if(std::isalnum(c))
      r.push_back(char(c));
    else
      r.push_back(char('0'+rnd()%9));
    }
  return r+".scp";
  }

bool PlayerList::save(const Entry& e) {
  std::error_code ec;
  if(std::filesystem::exists(e.file,ec)) {
    auto bak = e.file;
    bak.replace_extension(".bak");
    std::filesystem::copy_file(e.file,bak,std::filesystem::copy_options::overwrite_existing,ec);
    }
  const auto data = e.profile.serialize();
  std::ofstream f(e.file,std::ios::binary|std::ios::trunc);
  if(!f)
    return false;
  f.write(reinterpret_cast<const char*>(data.data()),std::streamsize(data.size()));
  return bool(f);
  }

PlayerList::Entry* PlayerList::create(const std::filesystem::path& dir, std::string_view name,
                                      const std::vector<uint8_t>& faceCsi, int32_t cash,
                                      const std::array<bool,3>& cars) {
  if(list.size()>=MaxPlayers || name.empty())
    return nullptr;
  Entry e;
  e.file = dir/fileName(name);
  e.profile.name      = std::string(name.substr(0,15));
  e.profile.createdAs = e.profile.name;
  e.profile.cash      = cash;
  for(size_t i=0; i<cars.size(); ++i)
    e.profile.cars[i].bought = cars[i];
  e.profile.setFace(faceCsi);
  std::error_code ec;
  std::filesystem::create_directories(dir,ec);
  if(!save(e))
    return nullptr;
  list.push_back(std::move(e));
  return &list.back();
  }

bool PlayerList::remove(size_t index) {
  if(index>=list.size())
    return false;
  std::error_code ec;
  std::filesystem::remove(list[index].file,ec);
  if(ec)
    return false;
  list.erase(list.begin()+std::ptrdiff_t(index));
  return true;
  }
