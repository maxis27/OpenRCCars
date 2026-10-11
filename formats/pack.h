#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace rc {

// RCCars.pack - "CREAT Studio Pack 1.0".
// Directory element 0x13: u32 count, entries 0x20 { 0x21 name, 0x22 size, 0x23 offset, 0x24 flags }.
// Every file is a blob [0x31][0x14][u32][u32] + payload XOR-ed with the CREAT key stream (see decrypt()).
class PackFile {
  public:
    struct Entry {
      std::string name;    // original name, e.g. "Textures.1\\wood_board.csi"
      uint64_t    offset = 0;
      uint32_t    size   = 0; // payload size; an encrypted blob adds a 10 byte header in front
      };

    PackFile() = default;
    explicit PackFile(const std::filesystem::path& path);

    bool                      isOpen()  const { return bool(file); }
    const std::vector<Entry>& entries() const { return list; }
    const Entry*              find(std::string_view name) const;

    std::vector<uint8_t>      read(const Entry& e);
    std::vector<uint8_t>      read(std::string_view name);

    // strips the 10 byte blob header and decrypts the payload; unencrypted data is returned as is
    static std::vector<uint8_t> decodeBlob(const std::vector<uint8_t>& blob);
    static std::string          normalize(std::string_view name);

  private:
    std::ifstream                           file;
    std::mutex                              sync;
    std::vector<Entry>                      list;
    std::unordered_map<std::string,size_t>  index;
  };

}
