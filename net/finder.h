#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace rc::net {

// The games to join ("Dołącz do gry", dlgNETJOIN_OPTS): RCCars.exe asks DirectPlay to enumerate the hosts at the
// address typed ("wsz. dostępne", every one on the local network, when none is; 0x4d03a0, 0x4d01b0) and lists those
// that answer, each with its name, track, players, laps and lag (42503 ..); one that has not answered for 5 s goes
// from the list (0x4d0440).
//
// Ours asks with a small UDP packet sent to the game's port, to the local network's broadcast address or to the
// address typed; the host's Session answers on its own port beside its ENet traffic (ENetHost::intercept):
//   question: "RCC?" [u16 Session::Version][u32 when, ms of the asker's clock]
//   answer:   "RCC!" [u16 version][u32 when, back][str session name][u8 players][u8 max players][u8 open]
//             and the game's own info (Session::setInfo) to the end
class Finder {
  public:
    static constexpr char     Question[4] = {'R','C','C','?'};
    static constexpr char     Answer[4]   = {'R','C','C','!'};
    // how often it asks again, and when a game that stopped answering goes from the list (0x4d0440: 5 s)
    static constexpr float    AskEvery    = 1.f;
    static constexpr float    Forget      = 5.f;

    struct Game {
      std::string          address;     // where it answered from, as "a.b.c.d"
      uint16_t             port = 0;
      std::string          name;        // the session's (UTF-8)
      int                  players = 0, maxPlayers = 0;
      bool                 open    = true;    // not racing: may be joined
      bool                 sameVersion = true;
      uint32_t             ping    = 0;       // ms, the answer's round trip
      std::vector<uint8_t> info;              // the game's own (Session::setInfo)
      float                seen    = 0;       // s since it last answered
      };

    // throws NetError when there is no network
    Finder();
    ~Finder();
    Finder(const Finder&) = delete;
    Finder& operator=(const Finder&) = delete;

    // from now on asks `address` (empty: everyone on the local network) at `port`; the list starts anew
    void                     search(const std::string& address, uint16_t port);
    // asks again when it is time, takes the answers, forgets the silent ones
    void                     poll(float dt);
    const std::vector<Game>& games() const { return list; }

  private:
    using Clock = std::chrono::steady_clock;
    void                     ask();
    uint32_t                 nowMs() const;

    intptr_t                 socket = -1;
    bool                     ready  = false;   // an address to ask
    uint32_t                 host   = 0;       // ENetAddress::host of it
    uint16_t                 port   = 0;
    float                    askIn  = 0;
    Clock::time_point        start;
    std::vector<Game>        list;
  };

// this computer's address on the network the way out goes through ("a.b.c.d"), empty when there is none; nothing is
// sent to find it
std::string localAddress();

}
