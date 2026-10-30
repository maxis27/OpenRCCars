#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace rc::net {

class NetError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
  };

// A network game: one player creates it ("Utwórz grę", RCCars.exe mode 7), the others join it ("Dołącz do gry",
// mode 8). The original sends through DirectPlay 8 (CLSID_DirectPlay8Peer, TCP/IP); this is our own, on ENet over UDP,
// and does not talk to RCCars.exe.
//
// Unlike DirectPlay's peer mesh every player talks only to the host, who passes the others' messages on: one port to
// open on one computer, and the host sees everything first (it already rules the props in the original, 0x52db29).
// The host is player 0; the others get 1 .. as they come and keep their number until they leave.
//
// Two kinds of sending, as the original: reliable, in order (the lobby, chat, collisions, the finish) and fast
// (the cars' state, a few times a second, where a lost packet is just replaced by the next one).
//
// Nothing blocks: poll() once a frame sends what was queued and hands over what came.
class Session {
  public:
    // the game's own messages start here, below are the session's
    static constexpr uint8_t  FirstUserType = 0x20;
    static constexpr uint8_t  Everyone      = 0xff;
    static constexpr uint8_t  HostId        = 0;
    // RCCars.crs Network maxPlayersNum
    static constexpr int      DefaultMaxPlayers = 6;
    static constexpr int      MaxPlayers        = 8;
    // DirectPlay 8's own port (DPNSVR uses 6073, the sessions 2302..2400): ours when RCCars.crs has none
    static constexpr uint16_t DefaultPort       = 2302;
    // a player's name: Players\<name>.scp, at most 15 letters (0x4021)
    static constexpr size_t   MaxName           = 15;
    // changes when the messages change: older and newer copies of the game are turned away
    static constexpr uint16_t Version           = 1;

    // why a session ended, or why the host turned a player away (shown as the original's texts)
    enum class Reason : uint8_t {
      None,
      Left,          // closed on purpose
      ConnectionLost,// 20233 "Connection lost": no answer for a few seconds
      NoAnswer,      // nobody at that address
      Full,          // the game has its maxPlayers already
      Lag,           // 42453 "Lag time more than server access": slower than the host allows
      Version,       // another version of OpenRCCars
      Started,       // the race is on, no joining now
      Kicked,
      };

    struct HostOptions {
      uint16_t    port        = DefaultPort;
      int         maxPlayers  = DefaultMaxPlayers;  // the host included
      std::string sessionName;
      std::string playerName;
      // the round trip a joining player may have, ms (dlgNETCREATE_OPTS "maks. opóźnienie" 10..1000); 0: any
      uint32_t    maxLag      = 0;
      };

    struct Player {
      uint8_t     id   = 0;
      std::string name;
      uint32_t    ping = 0;   // the round trip to the host, ms (0 for the host itself)
      };

    struct Event {
      enum class Kind {
        Joined,    // `player` came in (for a client: also each player already there when it was let in, itself last)
        Left,      // `player` went
        Message,   // `type` with `data` from `player`
        Closed,    // the session is over: `reason`
        };
      Kind                 kind   = Kind::Message;
      uint8_t              player = 0;
      uint8_t              type   = 0;
      std::vector<uint8_t> data;
      Reason               reason = Reason::None;
      };

    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // throws NetError when the port can't be opened
    static std::unique_ptr<Session> host(const HostOptions& o);
    // `address`: a name or an IPv4 address; the answer comes later through poll(): Joined for itself, or Closed
    static std::unique_ptr<Session> join(const std::string& address, uint16_t port, const std::string& playerName);

    bool               isHost()  const { return hosting; }
    // a client: let in by the host; the host: always
    bool               ready()   const { return hosting || selfId!=NoId; }
    bool               closed()  const { return over; }
    uint8_t            self()    const { return selfId; }
    const std::string& sessionName() const { return name; }
    int                maxPlayers()  const { return limit; }
    // everyone in, the host first, by id
    const std::vector<Player>& players() const { return list; }
    const Player*      player(uint8_t id) const;

    // `type` >= FirstUserType; `to` a player or Everyone (but the sender). Nothing is sent before ready().
    void               send(uint8_t type, const std::vector<uint8_t>& data, bool reliable, uint8_t to = Everyone);
    // the host: no more joining (Reason::Started), or again
    void               setOpen(bool open) { accepting = open; }
    // the host: sends `id` away
    void               kick(uint8_t id);

    std::vector<Event> poll();
    // leaves (a client) or ends the game for everyone (the host); poll() gives Closed(Left) after it
    void               close();

  private:
    struct Peer;
    static constexpr uint8_t NoId = 0xfe;
    using Clock = std::chrono::steady_clock;

    Session();
    void   open(const char* address, uint16_t port, size_t peers);
    void   onConnect(void* peer, std::vector<Event>& ev);
    void   onReceive(void* peer, const uint8_t* data, size_t size, bool reliable, std::vector<Event>& ev);
    void   onDisconnect(void* peer, uint32_t data, std::vector<Event>& ev);
    void   hostMessage(Peer& from, uint8_t type, const std::vector<uint8_t>& body, std::vector<Event>& ev);
    void   clientMessage(uint8_t type, uint8_t sender, const std::vector<uint8_t>& body, std::vector<Event>& ev);
    void   admit(Peer& p, std::vector<Event>& ev);
    void   refuse(Peer& p, Reason r);
    void   sendPlayers();
    void   raw(void* peer, uint8_t type, uint8_t from, uint8_t to, const std::vector<uint8_t>& body, bool reliable);
    Peer*  peerOf(uint8_t id);
    uint8_t freeId() const;
    void   end(Reason r, std::vector<Event>& ev);

    void*                              enetHost = nullptr;
    void*                              server   = nullptr;   // a client's way to the host
    std::vector<std::unique_ptr<Peer>> peers;                // the host's: everyone connected, let in or not
    bool                               hosting   = false;
    bool                               accepting = true;
    bool                               over      = false;
    bool                               closing   = false;
    Reason                             overWhy   = Reason::None;
    uint8_t                            selfId    = NoId;
    std::string                        name;
    std::string                        selfName;
    int                                limit     = DefaultMaxPlayers;
    uint32_t                           maxLag    = 0;
    std::vector<Player>                list;
    Clock::time_point                  lastList;
  };

const char* reasonName(Session::Reason r);

}
