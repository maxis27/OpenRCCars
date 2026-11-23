#include "finder.h"

#include "bytes.h"
#include "session.h"

#include <enet/enet.h>

#include <algorithm>
#include <cstring>

namespace rc::net {

Finder::Finder():start(Clock::now()) {
  Session::useEnet();
  const ENetSocket s = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
  if(s==ENET_SOCKET_NULL) {
    Session::dropEnet();
    throw NetError("no network");
    }
  enet_socket_set_option(s,ENET_SOCKOPT_NONBLOCK,1);
  enet_socket_set_option(s,ENET_SOCKOPT_BROADCAST,1);
  ENetAddress any = {};
  any.host = ENET_HOST_ANY;
  any.port = ENET_PORT_ANY;
  enet_socket_bind(s,&any);
  socket = intptr_t(s);
  }

Finder::~Finder() {
  enet_socket_destroy(ENetSocket(socket));
  Session::dropEnet();
  }

uint32_t Finder::nowMs() const {
  return uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-start).count());
  }

void Finder::search(const std::string& address, uint16_t p) {
  list.clear();
  ready = false;
  port  = p;
  if(address.empty()) {
    host  = ENET_HOST_BROADCAST;
    ready = true;
    }
  else {
    ENetAddress at = {};
    if(enet_address_set_host(&at,address.c_str())==0) {
      host  = at.host;
      ready = true;
      }
    }
  askIn = 0;
  }

void Finder::ask() {
  if(!ready)
    return;
  Writer w;
  for(char c:Question)
    w.u8(uint8_t(c));
  w.u16(Session::Version).u32(nowMs());
  ENetAddress to = {};
  to.host = host;
  to.port = port;
  ENetBuffer b;
  b.data       = const_cast<uint8_t*>(w.data().data());
  b.dataLength = w.data().size();
  enet_socket_send(ENetSocket(socket),&to,&b,1);
  }

void Finder::poll(float dt) {
  askIn -= dt;
  if(askIn<=0) {
    askIn = AskEvery;
    ask();
    }
  for(auto& g:list)
    g.seen += dt;
  uint8_t buf[1500];
  for(int n=0; n<64; ++n) {
    ENetAddress from = {};
    ENetBuffer  b;
    b.data       = buf;
    b.dataLength = sizeof(buf);
    const int got = enet_socket_receive(ENetSocket(socket),&from,&b,1);
    if(got<=0)
      break;
    Reader r(buf,size_t(got));
    bool magic = true;
    for(char c:Answer)
      magic = magic && r.u8()==uint8_t(c);
    const uint16_t version = r.u16();
    const uint32_t when    = r.u32();
    Game g;
    g.name        = r.str();
    g.players     = r.u8();
    g.maxPlayers  = r.u8();
    g.open        = r.u8()!=0;
    g.info        = r.rest();
    if(!magic || !r.ok())
      continue;
    char ip[64] = {};
    enet_address_get_host_ip(&from,ip,sizeof(ip));
    g.address     = ip;
    g.port        = from.port;
    g.sameVersion = version==Session::Version;
    g.ping        = nowMs()-when;
    auto it = std::find_if(list.begin(),list.end(),[&](const Game& o){ return o.address==g.address && o.port==g.port; });
    if(it==list.end())
      list.push_back(std::move(g));
    else
      *it = std::move(g);
    }
  list.erase(std::remove_if(list.begin(),list.end(),[](const Game& g){ return g.seen>=Forget; }),list.end());
  }

std::string localAddress() {
  // a datagram socket "connected" to an address outside: the system picks the way out and its address
  try {
    Session::useEnet();
    }
  catch(const NetError&) {
    return {};
    }
  std::string r;
  const ENetSocket s = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
  if(s!=ENET_SOCKET_NULL) {
    ENetAddress out = {};
    out.port = 53;
    enet_address_set_host_ip(&out,"8.8.8.8");
    ENetAddress self = {};
    char ip[64] = {};
    if(enet_socket_connect(s,&out)==0 && enet_socket_get_address(s,&self)==0 && self.host!=0 &&
       enet_address_get_host_ip(&self,ip,sizeof(ip))==0)
      r = ip;
    enet_socket_destroy(s);
    }
  Session::dropEnet();
  return r;
  }

}
