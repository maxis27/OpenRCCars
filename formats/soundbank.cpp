#include "soundbank.h"

#include <cstring>
#include <map>

#include "chunk.h"

namespace rc {

int SoundBank::Channel::find(std::string_view sound) const {
  for(size_t i=0; i<sounds.size(); ++i)
    if(sounds[i].name==sound)
      return int(i);
  return -1;
  }

const SoundBank::Channel* SoundBank::find(std::string_view channel) const {
  for(auto& c:channels)
    if(c.name==channel)
      return &c;
  return nullptr;
  }

const SoundBank::Channel* SoundBank::ofModel(std::string_view model) const {
  for(auto& c:channels)
    if(!c.model.empty() && c.model==model)
      return &c;
  return nullptr;
  }

static uint32_t descFlags(const Chunk& c) {
  // 0x08 element: 24 bytes, the flags are the 5th dword
  return c.size()>=20 ? c.u32(16) : 0;
  }

static SoundBank::Sound readSound(const Chunk& node) {
  SoundBank::Sound s;
  for(auto& c:node.children()) {
    if(c.id==0x03)
      s.name = std::string(c.str());
    else if(c.id==0x08)
      s.flags = descFlags(c);
    else if(c.id==0xe7 && c.size()>=4)
      s.fadeIn = float(c.u32());
    else if(c.id==0xe6 && c.size()>=4)
      s.tail = float(c.u32());
    }
  return s;
  }

static SoundBank::Channel readChannel(const Chunk& node) {
  SoundBank::Channel ch;
  for(auto& c:node.children()) {
    if(c.id==0x03)
      ch.name = std::string(c.str());
    else if(c.id==0xe0 && c.size()>=4)
      ch.far = c.f32();
    else if(c.id==0xe1 && c.size()>=4)
      ch.near = c.f32();
    else if(c.isNode() && c.is("SOND"))
      ch.sounds.push_back(readSound(c));
    }
  return ch;
  }

static void walk(const Range& range, SoundBank& bank, const std::string& model) {
  for(auto& c:range) {
    if(c.type!=0x92)
      continue;
    if(c.is("SNCH")) {
      bank.channels.push_back(readChannel(c));
      bank.channels.back().model = model;
      }
    else if(c.is("MODL")) {
      // the models of people.sb, AIChars.sb, stone.sb
      std::string name;
      for(auto& ch:c.children())
        if(ch.id==0x03 && ch.type!=0x92)
          name = std::string(ch.str());
      walk(c.children(),bank,name);
      }
    else if(!c.is("MESH"))
      walk(c.children(),bank,model);  // DESC, FOLD
    }
  }

// Sound\snd.dat: "PR\x17\x1a", a float, then per sound a 0x60 struct of 0x61 name, 0x63 volume (percent), 0x64 (1)
static std::map<std::string,float> readVolumes(const std::vector<uint8_t>& dat) {
  std::map<std::string,float> ret;
  if(dat.size()<10)
    return ret;
  for(auto& e:Range(dat.data(),10,dat.size())) {
    if(e.type!=0x02)
      continue;
    std::string name;
    float       vol = 0;
    for(auto& c:e.children()) {
      if(c.id==0x61)
        name = std::string(c.str());
      else if(c.id==0x63 && c.size()>=4)
        vol = float(c.u32())-100.f;
      }
    if(!name.empty())
      ret[name] = vol;
    }
  return ret;
  }

SoundBank SoundBank::load(const std::vector<uint8_t>& sb, const std::vector<uint8_t>& sndDat) {
  SoundBank bank;
  for(auto& c:Range(sb.data(),0,sb.size()))
    if(c.type==0x92 && c.is("DESC"))
      walk(c.children(),bank,std::string());

  std::map<std::string,float> volumes;
  try {
    volumes = readVolumes(sndDat);
    }
  catch(const FormatError&) {
    // volumes are a nicety, the sounds play at 0 dB without them
    }
  for(auto& ch:bank.channels)
    for(auto& s:ch.sounds) {
      auto it = volumes.find(s.name);
      if(it!=volumes.end())
        s.volume = it->second;
      }
  return bank;
  }

}
