#include "session.h"

#include "bytes.h"

#include <enet/enet.h>

#include <algorithm>
#include <mutex>

namespace rc::net {

namespace {

// the session's own messages, below Session::FirstUserType
enum : uint8_t {
  MsgHello   = 0x01,   // client -> host: [u16 version][str name]
  MsgWelcome = 0x02,   // host -> client: [u8 your id][str session name][u8 max players]
  MsgPlayers = 0x03,   // host -> everyone: [u8 n] n x [u8 id][str name][u16 ping]
  };

enum : uint8_t { ChannelReliable = 0, ChannelFast = 1, Channels = 2 };

// every packet: [u8 type][u8 from][u8 to] and the message
constexpr size_t Header = 3;

// how long the host measures a joining player's round trip before judging it against maxLag
constexpr auto LagProbe   = std::chrono::milliseconds(2000);
// how often the host sends the players with their pings
constexpr auto ListPeriod = std::chrono::milliseconds(1000);

// ENet's own start-up, once for all sessions
std::mutex enetLock;
int        enetUsers = 0;

void enetUse() {
  std::lock_guard<std::mutex> g(enetLock);
  if(enetUsers==0 && enet_initialize()!=0)
    throw NetError("ENet could not start");
  ++enetUsers;
  }

void enetDrop() {
  std::lock_guard<std::mutex> g(enetLock);
  if(--enetUsers==0)
    enet_deinitialize();
  }

ENetPeer* asPeer(void* p) { return static_cast<ENetPeer*>(p); }

Session::Event event(Session::Event::Kind k, uint8_t player, uint8_t type = 0, std::vector<uint8_t> data = {}) {
  Session::Event e;
  e.kind   = k;
  e.player = player;
  e.type   = type;
  e.data   = std::move(data);
  return e;
  }

// at most Session::MaxName letters, not cutting a UTF-8 letter in two
std::string shortName(const std::string& s) {
  size_t at = 0, letters = 0;
  while(at<s.size() && letters<Session::MaxName) {
    ++at;
    while(at<s.size() && (uint8_t(s[at]) & 0xc0)==0x80)
      ++at;
    ++letters;
    }
  return s.substr(0,at);
  }

}

struct Session::Peer {
  ENetPeer*         peer  = nullptr;
  uint8_t           id    = NoId;   // NoId until let in
  std::string       name;
  bool              hello = false;
  Clock::time_point helloAt;
  };

Session::Session() {
  enetUse();
  }

Session::~Session() {
  if(!over)
    close();
  if(enetHost!=nullptr)
    enet_host_destroy(static_cast<ENetHost*>(enetHost));
  enetDrop();
  }

void Session::open(const char* address, uint16_t port, size_t peerCount) {
  ENetAddress at = {};
  at.host = ENET_HOST_ANY;
  at.port = port;
  if(address!=nullptr && enet_address_set_host(&at,address)!=0)
    throw NetError("unknown address");
  enetHost = enet_host_create(address!=nullptr ? &at : nullptr,peerCount,Channels,0,0);
  if(enetHost==nullptr)
    throw NetError(address!=nullptr ? "the port is taken" : "no network");
  }

std::unique_ptr<Session> Session::host(const HostOptions& o) {
  std::unique_ptr<Session> s(new Session());
  s->hosting  = true;
  s->name     = o.sessionName;
  s->selfName = shortName(o.playerName);
  s->limit    = std::clamp(o.maxPlayers,2,MaxPlayers);
  s->maxLag   = o.maxLag;
  s->selfId   = HostId;
  // everyone may knock, also when it is full: the late ones are told so (Reason::Full) instead of never answered
  s->open("0.0.0.0",o.port,MaxPlayers+4);
  s->list.push_back({HostId,s->selfName,0});
  s->lastList = Clock::now();
  return s;
  }

std::unique_ptr<Session> Session::join(const std::string& address, uint16_t port, const std::string& playerName) {
  std::unique_ptr<Session> s(new Session());
  s->selfName = shortName(playerName);
  s->open(nullptr,0,1);
  ENetAddress at = {};
  at.port = port;
  if(enet_address_set_host(&at,address.c_str())!=0)
    throw NetError("unknown address: "+address);
  s->server = enet_host_connect(static_cast<ENetHost*>(s->enetHost),&at,Channels,Version);
  if(s->server==nullptr)
    throw NetError("no way to connect");
  enet_peer_timeout(asPeer(s->server),32,3000,6000);
  return s;
  }

const Session::Player* Session::player(uint8_t id) const {
  for(auto& p:list)
    if(p.id==id)
      return &p;
  return nullptr;
  }

Session::Peer* Session::peerOf(uint8_t id) {
  for(auto& p:peers)
    if(p->id==id)
      return p.get();
  return nullptr;
  }

uint8_t Session::freeId() const {
  for(uint8_t id=1; id<MaxPlayers; ++id)
    if(player(id)==nullptr)
      return id;
  return NoId;
  }

void Session::raw(void* peer, uint8_t type, uint8_t from, uint8_t to, const std::vector<uint8_t>& body, bool reliable) {
  ENetPacket* pk = enet_packet_create(nullptr,Header+body.size(),reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
  if(pk==nullptr)
    return;
  pk->data[0] = type;
  pk->data[1] = from;
  pk->data[2] = to;
  if(!body.empty())
    std::copy(body.begin(),body.end(),pk->data+Header);
  if(enet_peer_send(asPeer(peer),reliable ? ChannelReliable : ChannelFast,pk)!=0)
    enet_packet_destroy(pk);
  }

void Session::send(uint8_t type, const std::vector<uint8_t>& data, bool reliable, uint8_t to) {
  if(over || !ready() || type<FirstUserType || to==selfId)
    return;
  if(!hosting) {
    raw(server,type,selfId,to,data,reliable);
    return;
    }
  for(auto& p:peers)
    if(p->id!=NoId && (to==Everyone || to==p->id))
      raw(p->peer,type,HostId,to,data,reliable);
  }

void Session::sendPlayers() {
  for(auto& pl:list)
    if(Peer* p = peerOf(pl.id))
      pl.ping = asPeer(p->peer)->roundTripTime;
  Writer w;
  w.u8(uint8_t(list.size()));
  for(auto& pl:list)
    w.u8(pl.id).str(pl.name).u16(uint16_t(std::min<uint32_t>(pl.ping,0xffff)));
  for(auto& p:peers)
    if(p->id!=NoId)
      raw(p->peer,MsgPlayers,HostId,p->id,w.data(),true);
  lastList = Clock::now();
  }

void Session::refuse(Peer& p, Reason r) {
  enet_peer_disconnect_later(p.peer,uint32_t(r));
  }

void Session::kick(uint8_t id) {
  if(!hosting)
    return;
  if(Peer* p = peerOf(id))
    refuse(*p,Reason::Kicked);
  }

void Session::admit(Peer& p, std::vector<Event>& ev) {
  p.id = freeId();
  if(p.id==NoId) {
    refuse(p,Reason::Full);
    return;
    }
  list.push_back({p.id,p.name,asPeer(p.peer)->roundTripTime});
  std::sort(list.begin(),list.end(),[](const Player& a, const Player& b){ return a.id<b.id; });
  raw(p.peer,MsgWelcome,HostId,p.id,Writer().u8(p.id).str(name).u8(uint8_t(limit)).take(),true);
  sendPlayers();
  ev.push_back(event(Event::Kind::Joined,p.id));
  }

void Session::onConnect(void* peer, std::vector<Event>& ev) {
  ENetPeer* e = asPeer(peer);
  // a few seconds without an answer: gone (the original's 20233 "Connection lost")
  enet_peer_timeout(e,32,3000,6000);
  if(!hosting) {
    raw(server,MsgHello,NoId,HostId,Writer().u16(Version).str(selfName).take(),true);
    return;
    }
  auto p  = std::make_unique<Peer>();
  p->peer = e;
  e->data = p.get();
  peers.push_back(std::move(p));
  }

void Session::hostMessage(Peer& from, uint8_t type, const std::vector<uint8_t>& body, std::vector<Event>& ev) {
  if(type==MsgHello) {
    if(from.hello)
      return;
    Reader r(body);
    const uint16_t version = r.u16();
    from.name    = shortName(r.str());
    from.hello   = true;
    from.helloAt = Clock::now();
    if(!r.ok() || version!=Version)
      refuse(from,Reason::Version);
    else if(!accepting)
      refuse(from,Reason::Started);
    else if(int(list.size())>=limit)
      refuse(from,Reason::Full);
    else if(maxLag==0)
      admit(from,ev);
    // else let in by poll() once its round trip is known
    }
  }

void Session::clientMessage(uint8_t type, uint8_t sender, const std::vector<uint8_t>& body, std::vector<Event>& ev) {
  Reader r(body);
  if(type==MsgWelcome) {
    const uint8_t id = r.u8();
    name  = r.str();
    limit = r.u8();
    if(r.ok())
      selfId = id;
    return;
    }
  if(type==MsgPlayers) {
    std::vector<Player> now(r.u8());
    for(auto& p:now) {
      p.id   = r.u8();
      p.name = r.str();
      p.ping = r.u16();
      }
    if(!r.ok() || selfId==NoId)
      return;
    // the ones gone, then the ones new (on the first list everyone there, itself last)
    for(auto& old:list)
      if(std::none_of(now.begin(),now.end(),[&](const Player& p){ return p.id==old.id; }))
        ev.push_back(event(Event::Kind::Left,old.id));
    const bool first = list.empty();
    for(auto& p:now)
      if(p.id!=selfId && std::none_of(list.begin(),list.end(),[&](const Player& o){ return o.id==p.id; }))
        ev.push_back(event(Event::Kind::Joined,p.id));
    if(first)
      ev.push_back(event(Event::Kind::Joined,selfId));
    list = std::move(now);
    return;
    }
  }

void Session::onReceive(void* peer, const uint8_t* data, size_t size, bool reliable, std::vector<Event>& ev) {
  if(size<Header)
    return;
  const uint8_t type = data[0], from = data[1], to = data[2];
  std::vector<uint8_t> body(data+Header,data+size);
  if(!hosting) {
    if(type<FirstUserType)
      clientMessage(type,from,body,ev);
    else if(ready())
      ev.push_back(event(Event::Kind::Message,from,type,std::move(body)));
    return;
    }

  Peer* p = static_cast<Peer*>(asPeer(peer)->data);
  if(p==nullptr)
    return;
  if(type<FirstUserType) {
    hostMessage(*p,type,body,ev);
    return;
    }
  if(p->id==NoId)
    return;
  // passed on as it came, the sender being who the host knows it as, not what the packet says
  if(to==Everyone || to==HostId)
    ev.push_back(event(Event::Kind::Message,p->id,type,body));
  if(to==HostId)
    return;
  for(auto& o:peers)
    if(o.get()!=p && o->id!=NoId && (to==Everyone || to==o->id))
      raw(o->peer,type,p->id,to,body,reliable);
  }

void Session::onDisconnect(void* peer, uint32_t data, std::vector<Event>& ev) {
  if(!hosting) {
    if(over)
      return;
    Reason r = Reason(data);
    if(data==0 || data>uint32_t(Reason::Kicked))
      r = ready() ? Reason::ConnectionLost : Reason::NoAnswer;
    server = nullptr;
    end(r,ev);
    return;
    }
  ENetPeer* e = asPeer(peer);
  Peer*     p = static_cast<Peer*>(e->data);
  e->data = nullptr;
  if(p==nullptr)
    return;
  const uint8_t id = p->id;
  peers.erase(std::remove_if(peers.begin(),peers.end(),[&](const std::unique_ptr<Peer>& q){ return q.get()==p; }),peers.end());
  if(id==NoId)
    return;
  list.erase(std::remove_if(list.begin(),list.end(),[&](const Player& q){ return q.id==id; }),list.end());
  ev.push_back(event(Event::Kind::Left,id));
  sendPlayers();
  }

void Session::end(Reason r, std::vector<Event>& ev) {
  if(over)
    return;
  over    = true;
  overWhy = r;
  Event e = event(Event::Kind::Closed,HostId);
  e.reason = r;
  ev.push_back(std::move(e));
  }

std::vector<Session::Event> Session::poll() {
  std::vector<Event> ev;
  if(closing) {
    closing = false;
    end(Reason::Left,ev);
    return ev;
    }
  if(over || enetHost==nullptr)
    return ev;

  ENetHost* h = static_cast<ENetHost*>(enetHost);
  ENetEvent e;
  while(!over && enet_host_service(h,&e,0)>0) {
    switch(e.type) {
      case ENET_EVENT_TYPE_CONNECT:
        onConnect(e.peer,ev);
        break;
      case ENET_EVENT_TYPE_RECEIVE:
        onReceive(e.peer,e.packet->data,e.packet->dataLength,(e.packet->flags & ENET_PACKET_FLAG_RELIABLE)!=0,ev);
        enet_packet_destroy(e.packet);
        break;
      case ENET_EVENT_TYPE_DISCONNECT:
        onDisconnect(e.peer,e.data,ev);
        break;
      case ENET_EVENT_TYPE_NONE:
        break;
      }
    }
  if(over || !hosting)
    return ev;

  // the joining players whose round trip has been measured long enough
  const auto now = Clock::now();
  for(auto& p:peers) {
    if(!p->hello || p->id!=NoId || now-p->helloAt<LagProbe || asPeer(p->peer)->state!=ENET_PEER_STATE_CONNECTED)
      continue;
    if(asPeer(p->peer)->roundTripTime>maxLag)
      refuse(*p,Reason::Lag);
    else if(int(list.size())>=limit)
      refuse(*p,Reason::Full);
    else
      admit(*p,ev);
    }
  if(now-lastList>=ListPeriod)
    sendPlayers();
  return ev;
  }

void Session::close() {
  if(over || closing)
    return;
  ENetHost* h = static_cast<ENetHost*>(enetHost);
  if(hosting) {
    for(auto& p:peers)
      enet_peer_disconnect(p->peer,uint32_t(Reason::Left));
    }
  else if(server!=nullptr) {
    enet_peer_disconnect(asPeer(server),uint32_t(Reason::Left));
    }
  if(h!=nullptr)
    enet_host_flush(h);
  closing = true;
  }

const char* reasonName(Session::Reason r) {
  switch(r) {
    case Session::Reason::None:           return "none";
    case Session::Reason::Left:           return "left";
    case Session::Reason::ConnectionLost: return "connection lost";
    case Session::Reason::NoAnswer:       return "no answer";
    case Session::Reason::Full:           return "full";
    case Session::Reason::Lag:            return "lag";
    case Session::Reason::Version:        return "version";
    case Session::Reason::Started:        return "started";
    case Session::Reason::Kicked:         return "kicked";
    }
  return "?";
  }

}
