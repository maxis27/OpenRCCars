#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace rc::cp1250 {

// The game's texts, fonts and player names are Windows-1250 (the Polish letters of 0x8c .. 0xfe); what goes over
// the network is UTF-8, so that the other side need not guess.

// a Windows-1250 letter as Unicode (0 for the few bytes that are none)
uint32_t    toUnicode(uint8_t c);
// Unicode as Windows-1250, -1 if it has not got it
int         fromUnicode(uint32_t u);

std::string toUtf8(std::string_view s);
// a letter Windows-1250 has not got becomes '?'; broken UTF-8 too
std::string fromUtf8(std::string_view s);

}
