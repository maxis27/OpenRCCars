#include "music.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>

#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include "dr_mp3.h"

namespace rc {

struct Mp3Stream::Impl {
  std::vector<uint8_t> file;
  drmp3                mp3 = {};
  std::vector<float>   pcm;          // decoded, interleaved, source rate and channels
  size_t               avail = 0;    // frames in pcm
  double               pos   = 0;    // read position in pcm, source frames
  bool                 eof   = false;

  ~Impl() { drmp3_uninit(&mp3); }

  // makes sure frames [pos, pos+2) are in pcm; false at the end of the file
  bool fill() {
    if(size_t(pos)+1<avail)
      return true;
    if(eof)
      return size_t(pos)<avail;
    const uint32_t ch   = mp3.channels;
    const size_t   keep = std::min(avail,size_t(2));
    const size_t   base = avail-keep;
    std::copy(pcm.begin()+ptrdiff_t(base*ch),pcm.begin()+ptrdiff_t(avail*ch),pcm.begin());
    pos  -= double(base);
    avail = keep;
    const size_t want = pcm.size()/ch-avail;
    const auto   got  = drmp3_read_pcm_frames_f32(&mp3,want,pcm.data()+avail*ch);
    avail += size_t(got);
    if(got<want)
      eof = true;
    return size_t(pos)<avail;
    }
  };

Mp3Stream::Mp3Stream(std::vector<uint8_t> f) : impl(std::make_unique<Impl>()) {
  impl->file = std::move(f);
  if(!drmp3_init_memory(&impl->mp3,impl->file.data(),impl->file.size(),nullptr))
    throw std::runtime_error("not an MP3 file");
  if(impl->mp3.channels<1 || impl->mp3.sampleRate==0) {
    drmp3_uninit(&impl->mp3);
    impl->mp3 = {};
    throw std::runtime_error("unsupported MP3 file");
    }
  impl->pcm.resize(size_t(8192)*impl->mp3.channels);
  }

Mp3Stream::~Mp3Stream() = default;

size_t Mp3Stream::mix(float* out, size_t frames, float gain) {
  auto&          s    = *impl;
  const uint32_t ch   = s.mp3.channels;
  const double   step = double(s.mp3.sampleRate)/Mixer::Rate;
  for(size_t i=0; i<frames; ++i) {
    if(!s.fill())
      return i;
    const size_t a = size_t(s.pos);
    const size_t b = std::min(a+1,s.avail-1);
    const float  t = float(s.pos-double(a));
    const float  l = s.pcm[a*ch]+(s.pcm[b*ch]-s.pcm[a*ch])*t;
    const float  r = ch>1 ? s.pcm[a*ch+1]+(s.pcm[b*ch+1]-s.pcm[a*ch+1])*t : l;
    out[i*2+0] += l*gain;
    out[i*2+1] += r*gain;
    s.pos += step;
    }
  return frames;
  }

// TrackMP  map   "Dead_Track01.mp3" cycle 1
MusicList MusicList::parseAutoexec(std::string_view text) {
  MusicList          ret;
  std::istringstream in{std::string(text)};
  std::string        line;
  while(std::getline(in,line)) {
    std::istringstream ls(line);
    std::string        cmd, map;
    if(!(ls>>cmd>>map) || cmd!="TrackMP" || map!="map")
      continue;
    const auto q0 = line.find('"');
    const auto q1 = q0==std::string::npos ? q0 : line.find('"',q0+1);
    if(q1==std::string::npos)
      continue;
    Track t;
    t.file = line.substr(q0+1,q1-q0-1);
    std::istringstream rest(line.substr(q1+1));
    std::string        key;
    if(rest>>key && key=="cycle")
      rest>>t.style;
    ret.list.push_back(t);
    }
  ret.played.assign(ret.list.size(),false);
  return ret;
  }

const MusicList::Track* MusicList::next() {
  if(list.empty())
    return nullptr;
  int style;
  if(lastStyle==-2)
    style = int(rnd()&1);
  else
    style = (lastStyle+1)&1;
  lastStyle = style;

  std::vector<size_t> fresh, all;
  for(size_t i=0; i<list.size(); ++i)
    if(list[i].style==style) {
      all.push_back(i);
      if(!played[i])
        fresh.push_back(i);
      }
  if(all.empty()) {
    // only one style listed
    for(size_t i=0; i<list.size(); ++i) {
      all.push_back(i);
      if(!played[i])
        fresh.push_back(i);
      }
    }
  if(fresh.empty()) {
    for(auto i:all)
      played[i] = false;
    fresh = all;
    }
  const size_t pick = fresh[rnd()%fresh.size()];
  played[pick] = true;
  return &list[pick];
  }

}
