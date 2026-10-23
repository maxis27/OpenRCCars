#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace rc {

// The game's texts: Language\Game\<language>.tbl and Language\System\<language>.tbl, loose files next to
// RCCars.exe. As RCCars.exe reads them (0x401920) they are chunks of a 4-byte tag and a 4-byte size that counts
// the 8 header bytes:
//   HSRC  a version dword; with a high word other than 1 every entry has one more dword after its id
//   CSRC  two dwords: the key's start and its step
//   LSRC  the language's name
//   DSRC  the count, then each text: id (dword), length (word), the bytes with their terminating zero
// The bytes of LSRC and of the texts, in file order, are XORed with the low byte of a key that starts at CSRC's
// first dword and grows by the second after every byte. The texts are Windows-1250 (Polish) or ASCII and are kept
// so, which the Smash fonts index directly. RCCars.exe looks a text up by its id (0x401800 -> 0x4015f0).
class TextTable {
  public:
    TextTable() = default;
    // throws FormatError
    static TextTable parse(const uint8_t* data, size_t size);

    std::string_view language() const { return lang; }
    // empty if there is no such text
    std::string_view get(uint32_t id) const;
    size_t           size() const { return texts.size(); }
    const std::map<uint32_t,std::string>& all() const { return texts; }

  private:
    std::string                     lang;
    std::map<uint32_t,std::string>  texts;
  };

}
