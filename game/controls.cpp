#include "controls.h"

#include <cstring>

#include "formats/settings.h"

using namespace Tempest;

namespace {

// the keys a Tempest window reports and the codes of RCCars.exe's keyboard (DirectInput's scan codes through the
// table at 0x49ca00: Esc 1, F1 .. F10 2 .., 1 .. 0 0x12 .., Q .. P 0x20 .., A .. L 0x2e .., Z .. M 0x3b ..). The
// right Shift, Ctrl and Alt are the left ones' (the keyboard tells them apart only when asked to)
struct KeyCode {
  Event::KeyType key;
  int32_t        code;
  const char*    name;
  };

const KeyCode KeyCodes[] = {
  {Event::K_ESCAPE,  0x01,"Esc"},
  {Event::K_F1,      0x02,"F1"}, {Event::K_F2, 0x03,"F2"}, {Event::K_F3, 0x04,"F3"}, {Event::K_F4, 0x05,"F4"},
  {Event::K_F5,      0x06,"F5"}, {Event::K_F6, 0x07,"F6"}, {Event::K_F7, 0x08,"F7"}, {Event::K_F8, 0x09,"F8"},
  {Event::K_F9,      0x0a,"F9"}, {Event::K_F10,0x0b,"F10"},{Event::K_F11,0x0c,"F11"},{Event::K_F12,0x0d,"F12"},
  {Event::K_Pause,   0x10,"Pause"},
  {Event::K_1,0x12,"1"},{Event::K_2,0x13,"2"},{Event::K_3,0x14,"3"},{Event::K_4,0x15,"4"},{Event::K_5,0x16,"5"},
  {Event::K_6,0x17,"6"},{Event::K_7,0x18,"7"},{Event::K_8,0x19,"8"},{Event::K_9,0x1a,"9"},{Event::K_0,0x1b,"0"},
  {Event::K_Back,    0x1e,"Backspace"},
  {Event::K_Tab,     0x1f,"Tab"},
  {Event::K_Q,0x20,"Q"},{Event::K_W,0x21,"W"},{Event::K_E,0x22,"E"},{Event::K_R,0x23,"R"},{Event::K_T,0x24,"T"},
  {Event::K_Y,0x25,"Y"},{Event::K_U,0x26,"U"},{Event::K_I,0x27,"I"},{Event::K_O,0x28,"O"},{Event::K_P,0x29,"P"},
  {Event::K_CapsLock,0x2d,"Caps Lock"},
  {Event::K_A,0x2e,"A"},{Event::K_S,0x2f,"S"},{Event::K_D,0x30,"D"},{Event::K_F,0x31,"F"},{Event::K_G,0x32,"G"},
  {Event::K_H,0x33,"H"},{Event::K_J,0x34,"J"},{Event::K_K,0x35,"K"},{Event::K_L,0x36,"L"},
  {Event::K_Return,  0x39,"Enter"},
  {Event::K_LShift,  0x3a,"Shift"}, {Event::K_RShift,0x3a,"Shift"},
  {Event::K_Z,0x3b,"Z"},{Event::K_X,0x3c,"X"},{Event::K_C,0x3d,"C"},{Event::K_V,0x3e,"V"},{Event::K_B,0x3f,"B"},
  {Event::K_N,0x40,"N"},{Event::K_M,0x41,"M"},
  {Event::K_LControl,0x46,"Ctrl"},  {Event::K_RControl,0x46,"Ctrl"},
  {Event::K_LCommand,0x47,"Left Windows"},
  {Event::K_LAlt,    0x48,"Alt"},   {Event::K_RAlt,0x48,"Alt"},
  {Event::K_Space,   0x49,"Space"},
  {Event::K_RCommand,0x4b,"Right Windows"},
  {Event::K_Insert,  0x4e,"Insert"},
  {Event::K_Delete,  0x4f,"Delete"},
  {Event::K_Home,    0x50,"Home"},
  {Event::K_End,     0x51,"End"},
  {Event::K_PageUp,  0x52,"Page Up"},
  {Event::K_PageDown,0x53,"Page Down"},
  {Event::K_Left,    0x54,"Left"},
  {Event::K_Right,   0x55,"Right"},
  {Event::K_Up,      0x56,"Up"},
  {Event::K_Down,    0x57,"Down"},
  };

// the other keys of the original's keyboard, which a Tempest window does not report, by their names
const std::pair<int32_t,const char*> OtherNames[] = {
  {0x0e,"Print Screen"},{0x0f,"Scroll Lock"},{0x11,"`"},{0x1c,"-"},{0x1d,"="},{0x2a,"["},{0x2b,"]"},{0x2c,"\\"},
  {0x37,";"},{0x38,"'"},{0x42,","},{0x43,"."},{0x44,"/"},{0x4c,"Applications"},{0x58,"Num Lock"},
  {0x59,"Num 1"},{0x5a,"Num 2"},{0x5b,"Num 3"},{0x5c,"Num 4"},{0x5d,"Num 5"},{0x5e,"Num 6"},{0x5f,"Num 7"},
  {0x60,"Num 8"},{0x61,"Num 9"},{0x62,"Num 0"},{0x63,"Num Del"},{0x64,"Num Enter"},{0x65,"Num +"},
  {0x66,"Num -"},{0x67,"Num *"},{0x68,"Num /"},
  };

const char* const Records[Controls::Count] = {"Boost","Left","Right","Forw","Back","Jump","Brake","Reset"};

// 0x4e3110: Klaw.1 (0x14aac7c) and Klaw.2 (0x14aac80), one key each
const int32_t Presets[2][Controls::Count] = {
  {0x3a,0x54,0x55,0x56,0x57,0x49,0x3f,0x23},   // Shift, the arrows, Space, B, R
  {0x1f,0x2e,0x30,0x21,0x2f,0x48,0x3b,0x3d},   // Tab, A, D, W, S, Alt, Z, C
  };

int32_t get32(const std::vector<uint8_t>& d, size_t at) {
  int32_t v = 0;
  if(at+4<=d.size())
    std::memcpy(&v,d.data()+at,4);
  return v;
  }

void put32(std::vector<uint8_t>& d, int32_t v) {
  for(int i=0; i<4; ++i)
    d.push_back(uint8_t(uint32_t(v)>>(8*i)));
  }

}

Controls::Slot Controls::Slot::keyboard(int32_t code) {
  Slot s;
  if(code!=0) {
    s.device   = 1;
    s.count    = 1;
    s.codes[0] = code;
    }
  return s;
  }

bool Controls::Slot::operator==(const Slot& o) const {
  return device==o.device && count==o.count && codes==o.codes;
  }

const char* Controls::recordName(Action a) {
  return a<Count ? Records[a] : "";
  }

Controls::Table Controls::preset(int32_t layout) {
  Table t;
  const auto& p = Presets[layout==Keys2 ? 1 : 0];
  for(size_t i=0; i<Count; ++i)
    t[i] = {Slot::keyboard(p[i]),Slot()};
  return t;
  }

Controls::Table Controls::read(const rc::ConfigFile& f, int player) {
  Table t = preset(player==0 ? Keys1 : Keys2);
  const std::string section = player==0 ? "Player1" : "Player2";
  for(size_t i=0; i<Count; ++i) {
    // 0x4e48a0: taken only when it is the 44 bytes and its first word is set
    const auto* r = f.structure(section,Records[i]);
    if(r==nullptr || r->size()!=44 || get32(*r,0)==0)
      continue;
    for(size_t k=0; k<2; ++k) {
      Slot& s = t[i][k];
      const size_t at = 4+k*20;
      s.device = get32(*r,at);
      s.count  = get32(*r,at+4);
      for(size_t c=0; c<3; ++c)
        s.codes[c] = get32(*r,at+8+c*4);
      if(s.device==0)
        s = Slot();
      }
    }
  return t;
  }

std::vector<uint8_t> Controls::record(const Keys& k) {
  std::vector<uint8_t> r;
  put32(r,1);
  for(auto& s:k) {
    put32(r,s.device);
    put32(r,s.count);
    for(auto c:s.codes)
      put32(r,c);
    }
  return r;
  }

std::vector<uint8_t> Controls::write(const std::vector<uint8_t>& data, int player, const Table& t) {
  std::vector<uint8_t> ret = data;
  for(size_t i=0; i<Count; ++i)
    ret = rc::ConfigFile::withStruct(ret,player==0 ? "Player1" : "Player2",Records[i],record(t[i]));
  return ret;
  }

int32_t Controls::code(Event::KeyType k) {
  for(auto& c:KeyCodes)
    if(c.key==k)
      return c.code;
  return 0;
  }

Event::KeyType Controls::keyOf(int32_t code) {
  for(auto& c:KeyCodes)
    if(c.code==code)
      return c.key;
  return Event::K_NoKey;
  }

std::string Controls::name(int32_t code) {
  for(auto& c:KeyCodes)
    if(c.code==code)
      return c.name;
  for(auto& [c,n]:OtherNames)
    if(c==code)
      return n;
  return code!=0 ? "#"+std::to_string(code) : std::string();
  }

void Controls::load(const rc::ConfigFile& f) {
  // the layout of the player: Klaw.1, Klaw.2 or the player's own (a joystick's is not ours: the player's own)
  const int32_t layout = f.integer("Input","Player1Layout",Keys1);
  table = layout==Keys1 || layout==Keys2 ? preset(layout) : read(f,0);
  }

bool Controls::held(Action a, const bool* down) const {
  for(auto& s:table[a]) {
    const auto k = keyOf(s.key());
    if(k==Event::K_NoKey)
      continue;
    if(down[k])
      return true;
    // the right Shift, Ctrl and Alt are the left ones
    if((k==Event::K_LShift && down[Event::K_RShift]) || (k==Event::K_LControl && down[Event::K_RControl]) ||
       (k==Event::K_LAlt && down[Event::K_RAlt]))
      return true;
    }
  return false;
  }

bool Controls::is(Action a, Event::KeyType k) const {
  const int32_t c = code(k);
  if(c==0)
    return false;
  for(auto& s:table[a])
    if(s.key()==c)
      return true;
  return false;
  }
