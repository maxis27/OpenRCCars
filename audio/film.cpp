#include "film.h"

#include <algorithm>
#include <cstring>
#include <deque>
#include <mutex>
#include <stdexcept>

#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"

namespace rc {

// the decoded sound waiting for the mixing thread
struct Film::Sound {
  std::mutex         sync;
  std::deque<float>  samples;   // stereo, interleaved
  };

namespace {

class FilmStream : public Stream {
  public:
    explicit FilmStream(std::shared_ptr<void> owner, std::mutex& sync, std::deque<float>& samples)
      : owner(std::move(owner)), sync(sync), samples(samples) {}

    size_t mix(float* out, size_t frames, float gain) override {
      std::lock_guard<std::mutex> guard(sync);
      const size_t n = std::min(frames*2,samples.size());
      for(size_t i=0; i<n; ++i)
        out[i] += samples[i]*gain;
      samples.erase(samples.begin(),samples.begin()+ptrdiff_t(n));
      return frames;
      }

  private:
    std::shared_ptr<void> owner;
    std::mutex&           sync;
    std::deque<float>&    samples;
  };

}

struct Film::Impl {
  std::vector<uint8_t> file;
  plm_t*               plm = nullptr;
  Film*                owner = nullptr;

  ~Impl() {
    if(plm!=nullptr)
      plm_destroy(plm);
    }

  static void onVideo(plm_t*, plm_frame_t* frame, void* user) {
    auto& self = *static_cast<Impl*>(user);
    auto& rgba = self.owner->rgba;
    rgba.resize(size_t(frame->width)*frame->height*4);
    plm_frame_to_rgba(frame,rgba.data(),int(frame->width*4));
    // opaque
    for(size_t i=3; i<rgba.size(); i+=4)
      rgba[i] = 255;
    self.owner->fresh = true;
    }

  static void onAudio(plm_t*, plm_samples_t* s, void* user) {
    auto& self = *static_cast<Impl*>(user);
    auto& q    = *self.owner->queue;
    std::lock_guard<std::mutex> guard(q.sync);
    q.samples.insert(q.samples.end(),s->interleaved,s->interleaved+s->count*2);
    }
  };

Film::Film(std::vector<uint8_t> data) : impl(std::make_unique<Impl>()), queue(std::make_shared<Sound>()) {
  impl->file  = std::move(data);
  impl->owner = this;
  impl->plm   = plm_create_with_memory(impl->file.data(),impl->file.size(),0);
  if(impl->plm==nullptr || !plm_has_headers(impl->plm) || plm_get_num_video_streams(impl->plm)<1)
    throw std::runtime_error("not an MPEG-1 film");
  plm_set_video_decode_callback(impl->plm,&Impl::onVideo,impl.get());
  plm_set_audio_decode_callback(impl->plm,&Impl::onAudio,impl.get());
  plm_set_loop(impl->plm,0);
  if(plm_get_num_audio_streams(impl->plm)<1 || plm_get_samplerate(impl->plm)!=int(Mixer::Rate))
    plm_set_audio_enabled(impl->plm,0);
  }

Film::~Film() = default;

int    Film::width()    const { return plm_get_width(impl->plm); }
int    Film::height()   const { return plm_get_height(impl->plm); }
double Film::duration() const { return plm_get_duration(impl->plm); }
double Film::time()     const { return plm_get_time(impl->plm); }
bool   Film::ended()    const { return plm_has_ended(impl->plm)!=0; }

void Film::seek(double t) {
  {
    std::lock_guard<std::mutex> guard(queue->sync);
    queue->samples.clear();
  }
  plm_seek(impl->plm,t,1);
  }

bool Film::advance(double dt) {
  fresh = false;
  plm_decode(impl->plm,dt);
  return fresh;
  }

std::unique_ptr<Stream> Film::sound() {
  if(!plm_get_audio_enabled(impl->plm))
    return nullptr;
  return std::make_unique<FilmStream>(queue,queue->sync,queue->samples);
  }

}
