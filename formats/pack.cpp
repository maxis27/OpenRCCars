#include "pack.h"

#include "chunk.h"

#include <algorithm>
#include <array>
#include <cctype>

using namespace rc;

namespace {

// Key stream: k = "CREATSTUDIOGames"; each round emits k up to its first NUL byte (C-string),
// then every byte becomes 3*k+1 (mod 256). After 128 rounds the key repeats -> period of 1976 bytes.
struct KeyStream {
  std::vector<uint8_t> data;

  KeyStream() {
    std::array<uint8_t,16> k;
    std::memcpy(k.data(),"CREATSTUDIOGames",16);
    for(int round=0; round<128; ++round) {
      for(auto c:k) {
        if(c==0)
          break;
        data.push_back(c);
        }
      for(auto& c:k)
        c = uint8_t(3*c+1);
      }
    }
  };

const KeyStream& keyStream() {
  static KeyStream ks;
  return ks;
  }

}

PackFile::PackFile(const std::filesystem::path& path) {
  file.open(path,std::ios::binary);
  if(!file)
    throw FormatError("unable to open pack: "+path.string());

  file.seekg(0,std::ios::end);
  const uint64_t fileSize = uint64_t(file.tellg());

  // find directory element (id 0x13) among the top level elements
  uint64_t pos = 10;
  std::vector<uint8_t> dir;
  uint64_t dirPos = 0;
  while(pos+6<=fileSize) {
    uint8_t hdr[6];
    file.seekg(std::streamoff(pos));
    file.read(reinterpret_cast<char*>(hdr),6);
    uint32_t end;
    std::memcpy(&end,hdr+2,4);
    if(end<=pos || end>fileSize)
      throw FormatError("pack: corrupted header");
    if(hdr[0]==0x13) {
      dirPos = pos;
      dir.resize(size_t(end));                 // keep absolute offsets valid for Chunk
      file.seekg(std::streamoff(pos));
      file.read(reinterpret_cast<char*>(dir.data()+pos),std::streamsize(end-pos));
      break;
      }
    pos = end;
    }
  if(dir.empty())
    throw FormatError("pack: directory not found");

  Chunk root;
  root.file = dir.data();
  root.pos  = size_t(dirPos);
  root.end  = dir.size();
  root.id   = dir[dirPos];
  root.type = dir[dirPos+1];

  list.reserve(root.u32(0));          // u32 count, then entries
  for(auto& e:root.children(4)) {
    if(e.id!=0x20)
      continue;
    Entry ent;
    for(auto& f:e.children(0)) {
      switch(f.id) {
        case 0x21: ent.name   = std::string(f.str()); break;
        case 0x22: ent.size   = f.u32();              break;
        case 0x23: ent.offset = f.u32();              break;
        }
      }
    if(ent.offset+ent.size>fileSize)
      throw FormatError("pack: entry out of range: "+ent.name);
    index[normalize(ent.name)] = list.size();
    list.push_back(std::move(ent));
    }
  }

std::string PackFile::normalize(std::string_view name) {
  std::string s(name);
  for(auto& c:s) {
    if(c=='\\')
      c = '/';
    c = char(std::tolower(uint8_t(c)));
    }
  return s;
  }

const PackFile::Entry* PackFile::find(std::string_view name) const {
  auto it = index.find(normalize(name));
  if(it==index.end())
    return nullptr;
  return &list[it->second];
  }

std::vector<uint8_t> PackFile::read(std::string_view name) {
  auto e = find(name);
  if(e==nullptr)
    throw FormatError("pack: no such file: "+std::string(name));
  return read(*e);
  }

std::vector<uint8_t> PackFile::read(const Entry& e) {
  // An encrypted blob is [31 14][u32 end][u32 payload size][payload]; the directory size is the payload size,
  // so the blob is 10 bytes longer than the directory says. Reading only `size` bytes drops the file's tail.
  std::vector<uint8_t> blob(e.size);
  {
    std::lock_guard<std::mutex> guard(sync);
    file.seekg(std::streamoff(e.offset));
    file.read(reinterpret_cast<char*>(blob.data()),std::streamsize(blob.size()));
    if(!file)
      throw FormatError("pack: read error: "+e.name);
    if(blob.size()>=10 && blob[0]==0x31 && blob[1]==0x14) {
      uint32_t payload = 0;
      std::memcpy(&payload,blob.data()+6,4);
      if(payload+10u>blob.size() && payload<=e.size+10u) {
        const size_t have = blob.size();
        blob.resize(payload+10u);
        file.read(reinterpret_cast<char*>(blob.data()+have),std::streamsize(blob.size()-have));
        if(!file)
          throw FormatError("pack: read error: "+e.name);
        }
      }
  }
  return decodeBlob(blob);
  }

std::vector<uint8_t> PackFile::decodeBlob(const std::vector<uint8_t>& blob) {
  if(blob.size()<10 || blob[0]!=0x31 || blob[1]!=0x14)
    return blob; // not encrypted - return as is
  auto& ks = keyStream().data;
  std::vector<uint8_t> out(blob.begin()+10,blob.end());
  for(size_t i=0; i<out.size(); ++i)
    out[i] ^= ks[i%ks.size()];
  return out;
  }
