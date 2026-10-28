#pragma once

#include <Tempest/Event>

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "canvas.h"
#include "championship.h"
#include "formats/settings.h"
#include "formats/texttable.h"
#include "resources.h"

class Players;
class Sounds;
#include "formats/playerprofile.h"

// The game's front end as RCCars.exe draws it (its dialog framework, 0x43f250 and the window classes of
// 0x4aa2e0 ..): the main menu, the quick race, the championship with its garage, the choice of the player, the
// loading screen and the menus over a race (Esc, the finish). Laid out in
// units of a 800x600 screen like the original, whose dialogs are fractions of the screen (it always shows its menus
// in 800x600, RCCars.exe 0x4a6840); on wide screens the background is stretched to the whole window and the
// controls keep their shape, by the edge or the part of the background they belong to.
class Menu : public Canvas {
  public:
    enum class Page : uint8_t {
      None,
      Main,       // "Menu główne" (RCCars.exe 0x4c9500)
      QuickRace,  // "Szybki wyścig": dlgRACESUM
      InRace,     // Esc during a race: dlgINRACE
      Players,    // "Wybierz gracza": dlgPLRSCOMM (0x4d1c43)
      Finish,     // the table after the player's finish, over the race: dlgFINISH (0x4c4f90)
      Champ,      // "Mistrzostwa": the championship's frame (0x4be150) with its tracks, dlgCHAMP (0x4be720)
      ChampRace,  // the championship race's fee and opponents before it: dlgCHRACE (0x4c03c0)
      Garage,     // "Garaż" (0x4c5f00): the cars (dlgSETCAR, 0x4d5330) and their upgrades (dlgSETDETAIL, 0x4d43e0 ..)
      ChampWin,   // the last track won: the congratulations (0x4c13a0, 0x4c1790)
      GhostRace,  // "Wyścig duchów" (0x4d3000): dlgRACESUM with dlgRACESUM_GHOST's "Typ wyścigu" for the laps
      };
    // what the player chose, for MainWindow
    enum class Command : uint8_t {
      None,
      Race,       // "Wyścig": the track, car, laps and opponents of setup()
      Quit,       // "Wyjdź"
      Resume,     // "Zastosuj" or Esc in the race menu
      BackOnTrack,// "Wróć na tor": the car back to the last checkpoint
      RaceAgain,  // "Jeszcze raz?"
      Abort,      // "Przerwij": the race left for the main menu
      Done,       // "Wyjdź" of the finish table: the race recorded (0x4c56c0) and left for the main menu
      };
    // the game modes of 0x4a4ca0 that the menus start
    enum class Mode : uint8_t {
      Quick        = 1,  // "Szybki wyścig": the laps and the skill chosen
      Ghost        = 2,  // "Wyścig duchów", "Zwykły": against the own ghost of this race (flag 0x40)
      MultiGhost   = 3,  // "Multi": against the ghosts of GhostRecords\ (flag 0x80)
      Training     = 4,  // "Trening" of the championship: 3 laps, the easy opponents, no fee and no prize
      Championship = 5,  // 5 laps, the expert opponents, the fee paid, the prize won (0x4e03b0)
      };
    struct Setup {
      size_t  track   = 0;   // 0..9 in the game's order (Language/Game 40050..: Beach_1, Country_3 ...)
      size_t  car     = 0;   // 0..2: Car1..Car3
      int     laps    = 3;   // 3, 5 or 7 in a quick race
      int     skill   = 1;   // 0..2: "Niskie", "Norma", "Ekspert"
      Mode    mode    = Mode::Quick;
      bool    ghost() const { return mode==Mode::Ghost || mode==Mode::MultiGhost; }
      };
    // the turning car of the quick race (AnimPreview, 0x4a9e70), in window pixels
    struct Preview {
      float x = 0, y = 0, w = 0, h = 0;
      float yaw   = 0;   // degrees, 60 a second
      float alpha = 1;   // fading in
      // the car and how it looks: the player's, or stock when the player has not got it
      size_t  car = 0;
      int32_t boost = 0, resonator = 0, tires = 0, skin = 0;
      };

    // a car of the finish table (0x4c47e0), in the order of the race
    struct FinishRow {
      std::string                 name;
      std::string                 faceKey;          // whose picture `face` is, for keeping its texture
      const std::vector<uint8_t>* face = nullptr;   // a .csi, nullptr none
      float                       time    = 0;      // the finish, or the race's clock while still racing
      float                       bestLap = -1;     // < 0 none yet
      float                       speed   = 0;      // km/h on average
      bool                        player  = false;
      };

    static constexpr size_t TrackCount = 10;

    Menu(Tempest::Device& device, Resources& res, Sounds& sounds);

    // the players whose panel the main menu shows and who are chosen on Page::Players
    void    setPlayers(Players* p) { players = p; }
    // the cars of Page::Finish, every frame while it is open; `hits` the player's car's (Vehicle::hits)
    void    setFinish(std::vector<FinishRow> rows, int32_t hits = 0) { finishRows = std::move(rows); finishHits = hits; }
    // the championship's bonus the finish table shows (0x4c47e0, 0x14a3fa4): 0 but in a championship race won
    // among the first three, and not last
    int32_t finishBonus() const;
    // the player's place in the finish table, from 0; -1 none
    int32_t finishPlace() const;
    // the last track won in the championship (0x14a3a88): the congratulations instead of the championship next
    void    setChampionWon() { championWon = true; }
    // where a race's menus go back to (0x4e07c0): the quick race, the championship, its congratulations
    Page    afterRace() const;
    // a text of Language/Game (Windows-1250), empty if there is none
    std::string_view gameText(uint32_t id) const { return str(id); }

    // the code of the track (e.g. "beach_1") at `index` in the game's order
    static std::string trackName(size_t index);
    // the index of a track code, TrackCount if it is none of them
    static size_t      trackIndex(std::string_view name);

    Page    page() const { return current; }
    bool    active() const { return current!=Page::None; }
    // to `p` with the original's change of screen: the buttons going out, then the new ones coming in
    void    open(Page p);
    void    close();
    Setup&  setup() { return choice; }
    // the next command, Command::None if there is none
    Command take();

    void    tick(float dt);
    // window pixels
    void    mouseMove(int x, int y);
    void    mouseDown(int x, int y);
    void    mouseUp(int x, int y);
    // `ch`: the character typed (UTF-16), for the name of a new player
    void    keyDown(Tempest::Event::KeyType key, uint32_t ch = 0);
    void    mouseWheel(int delta);

    std::optional<Preview> carPreview() const;

    // the menu over the framebuffer set on `enc`; `preview` draws the car of carPreview() over the background
    void    draw(Tempest::Encoder<Tempest::CommandBuffer>& enc, uint8_t frame, int w, int h,
                 const std::function<void()>& preview);
    // the loading screen of a track (0x4dfaa0): its pictures, its name and how far the loading is (0..1)
    void    drawLoading(Tempest::Encoder<Tempest::CommandBuffer>& enc, uint8_t frame, int w, int h, size_t track,
                        float progress);

  private:
    struct Box {
      float x0 = 0, y0 = 0, x1 = 0, y1 = 0;   // 800x600 units
      bool  contains(float x, float y) const { return x>=x0 && x<x1 && y>=y0 && y<y1; }
      };
    // a texture of RCCars.pack (Textures.*), drawn by its 0..1 coordinates
    struct Tex {
      const Tempest::Texture2d* tex = nullptr;
      int                       w = 0, h = 0;
      explicit operator bool() const { return tex!=nullptr; }
      };
    struct Uv {
      float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
      };

    // a button (class BTN, 0x4aa2e0): on the right of the screen in one of the 8 slots of 0x570468, or the
    // race (green) or back (orange, slot 8) one at the bottom
    struct Button {
      enum Kind : uint8_t {
        Tab,      // red, an arrow: another screen
        Action,   // orange
        Radio,    // red, a dot: one of the pages of a screen, the one shown with a white dot
        Back,     // orange, bottom right (slot 8)
        Race,     // green, bottom left
        };
      Kind     kind    = Tab;
      int      slot    = 0;
      uint32_t text    = 0;
      Command  command = Command::None;
      Page     to      = Page::None;
      int      id      = 0;      // the screen's own (the original's control id), handled by the screen
      bool     enabled = true;
      bool     checked = false;  // a Radio of the page shown
      // the focus: +0xa0 bits 1 (coming) and 2 (going) with +0x98 the time of it
      bool     hover   = false;
      int      anim    = 0;
      float    animT   = 0;
      float    pressT  = 0;      // the press (+0xa0 bit 4), 0.4 s
      };
    // a button of the race's menu (class MBB, 0x4cbd20): a pill of messagebox.csi, red or orange
    struct Pill {
      Box      box;
      uint32_t text    = 0;
      bool     red     = false;
      Command  command = Command::None;
      bool     hover   = false;
      bool     enabled = true;   // greyed and deaf when not (0x43f5a0)
      };
    // the "<  value  >" choice (class ENM, 0x4b4630): a label on the left of `box`, the arrows and the value
    struct Enum {
      Box                      box;
      float                    split = 0;   // the label's share of the width (dlg*.ini <name>SE)
      uint32_t                 label = 0;
      std::vector<std::string> values;
      size_t                   at    = 0;
      bool                     hoverL = false, hoverR = false;
      float                    pushL = 0, pushR = 0;  // s since an arrow was pressed (0x5c8, 0x5c9)
      };

    // the message over a screen (0x4cb830): a text, "Tak" alone or with "Anuluj"; `action` what "Tak" does
    struct Message {
      enum Action : uint8_t { None, RemovePlayer, NewChampionship, BuyCar, SellCar, BuyUpgrade, SellUpgrade };
      bool     open   = false;
      uint32_t text   = 0;
      std::string formatted;   // the text with its values, when there is one
      Action   action = None;
      float    t      = 0;    // s since it came; it fades in over 0.2 s
      std::vector<Pill> pills;
      };
    // the name of a new player (0x4d2910): a line to type in with "Tak" and "Anuluj"
    struct NameEntry {
      bool        open = false;
      std::string text;       // Windows-1250
      float       t    = 0;
      std::vector<Pill> pills;
      };
    // a player's picture made into a texture, again when it changes
    struct FaceTex {
      uint32_t           version = uint32_t(-1);
      Resources::Texture tex;
      };

    void   build(Page p);
    void   leave(Page to, Command cmd);
    void   pressButton(Button& b);
    void   changeShot(int step);
    void   changeEnum(Enum& e, int step);
    void   playControl(int sound);   // "Control sounds" of Interface.sb: 0 ButtonFocus, 1 ArrowFocus ..
    void   playScreen(int sound);    // "Dialog sounds": 0 ChangeScreen, 1 ChangeScreenLeft

    std::string_view str(uint32_t id) const;
    Tex    tex(const std::string& name);
    Box    slotBox(int slot) const;
    // where the button is: the right ones stretched to the screen's edge on wide screens
    Box    buttonBox(const Button& b) const;
    // the scale and the width of the window for this frame (after begin())
    void   layout(int w, int h);
    // how many times the background is stretched sideways
    float  stretch() const { return (800.f+wide)/800.f; }
    // and up and down, on a window narrower than 4:3
    float  stretchY() const { return (600.f+tall)/600.f; }
    // where the screen's own controls go on a wide screen: under the middle of the background's circle
    float  contentShift() const { return wide*0.4f; }

    // drawing in 800x600 units
    void   image(const Tex& t, const Box& b, const Uv& uv, const Color& c);
    void   imageOpaque(const Tex& t, const Box& b, const Uv& uv);
    void   fill(const Box& b, const Color& c);
    // RCCars.exe 0x470e30 flags: `ax` 0 left, 1 centred, 2 right; `ay` 0 top, 1 centred, 2 bottom
    // `k`: the font enlarged that many times
    void   label(const Font& f, std::string_view s, const Box& b, int ax, int ay, const Color& c, float k = 1);
    // several lines, one under another from the top of `b`
    void   lines(const Font& f, std::string_view s, const Box& b, int ax, float step, const Color& c);
    // the line under a label (0x4ac6e0)
    void   underline(float x0, float x1, float y, const Color& c);

    void   drawBackground();
    void   drawHeader();
    void   drawButton(const Button& b);
    void   drawPill(const Pill& p);
    void   drawEnum(const Enum& e, bool marker);
    // shot_<track>_<shot> in `b`, its frame included, cut at `clip`'s left and right
    void   drawShot(size_t track, int shot, const Box& b, float alpha, const Box* clip = nullptr);
    void   drawMain();
    // the quick race's frame, or the ghost race's (the same with the ghosts' kind for the laps and the skill)
    bool   raceFrame() const { return current==Page::QuickRace || current==Page::GhostRace; }
    // and its own page shown
    bool   raceSetup() const { return raceFrame() && infoPage==0; }
    // the championship's frame with its tracks' page shown
    bool   champPage() const { return current==Page::Champ && infoPage==0; }
    void   drawQuickRace();
    // the player's picture, name, rank, car, time, scores and money (0x4a7170), the main menu's or another's
    void   drawPanel(rc::PlayerProfile& p, bool main);
    const Tempest::Texture2d* face(rc::PlayerProfile& p);
    // a picture (.csi) as a texture kept by `key`, made again when `version` changes
    const Tempest::Texture2d* faceTexture(const std::string& key, const std::vector<uint8_t>& csi, uint32_t version);

    // Page::Finish
    void   drawFinish();
    // a pill of Page::Finish pressed: the table fades out over 0.5 s, then its command (0x4c5500)
    void   finishPill(Command cmd);
    // where the pills of the page are drawn from (the race's menu along the bottom, the finish table in the middle)
    float  pillShiftY() const { return current==Page::InRace ? tall : tall*0.5f; }

    // Page::Players (0x4d2400 ..)
    void   fillPlayers();
    void   selectRow(size_t row);
    void   playerAction(int id);
    size_t selectedPlayer() const;
    size_t visibleRows() const;
    void   scrollRows(int step);
    void   drawPlayers();
    // `cancel`: the second pill's text when there is one ("Anuluj", the garage's "Nie")
    void   showMessage(uint32_t text, Message::Action action = Message::None, std::string formatted = {},
                       uint32_t cancel = 10001);
    void   messageAnswer(bool ok);
    void   nameAnswer(bool ok);
    void   drawMessage();
    void   drawNameEntry();
    // "$1 506" (0x4a98f0, Currency 0)
    static std::string money(int32_t v);
    // a text of Language/Game with its %s, %i (and %%) given in order, as sprintf would
    static std::string format(std::string_view f, std::initializer_list<std::string> args);

    // the frame of the race's dialogs (0x4acb20): messagebox.csi, 32 units at the corners, the middle stretched
    void   drawFrame(const Box& f);
    // a table (class LST, 0x4bcc60) `cols` wide (fractions after its 0.06 inset), the header `head` and the rows
    // `row` of its height; `cell` draws a cell (row -1 the header) and says whether the row is the chosen one
    struct Table {
      Box          box;
      const float* cols   = nullptr;
      size_t       ncols  = 0;
      float        head   = 0.1f, row = 0.1f;
      bool         greyed = true;   // the rows but the chosen one grey
      };
    void   drawTable(const Table& t, size_t rows, size_t top, int chosen,
                     const std::function<void(int,size_t,const Box&,const Color&)>& cell);
    // the row of `t` under the mouse (in the units the table is drawn in), -1 none
    int    tableRow(const Table& t, float mx, float my, size_t rows, size_t top) const;
    // the rows of `t` there is room for
    static size_t tableRows(const Table& t);
    // the scroll bar beside a list (class VSB, 0x4bdb20) from `y0` to `y1` at `x0`; `vis` of `rows` shown from `top`
    void   drawScrollBar(float x0, float y0, float y1, size_t rows, size_t vis, size_t top, bool up, bool down);
    // a player's picture in a table's cell (0x4c59f0), `inset` short of the cell's right (tableMordaShift)
    void   drawFaceCell(const Tempest::Texture2d* t, const Box& cb, float inset);
    // a screen's own button pressed (the original's control ids)
    void   buttonAction(int id);
    // the quick race may start: the car bought and the track open (0x4c9e70)
    bool   quickAllowed() const;
    // the buttons whose state follows the choices, every frame
    void   refreshButtons();

    // Page::Champ (0x4be720 ..)
    void   buildChamp();
    void   champAction(int id);
    // "Tak" of a message of the championship or the garage
    void   champAnswer(Message::Action a);
    Table  champTable() const;
    void   drawChamp();
    // the pages of the frames' radio buttons (menuinfo.cpp): 0 the frame's own, 1 "Mapa i informacje" (dlgMAPINFO,
    // 0x4caa20), 2 "Statystyki toru" (dlgSTAT, 0x4d8060)
    bool   infoFrame() const;
    void   showInfo(int page);
    // the player playing may race on `track` in this frame (0x4e84a0)
    bool   trackOpenHere(size_t track) const;
    // 0 the championship's results, 1 the best lap, 2 .. 4 the best race of 3, 5, 7 laps (0x56fa20)
    int    statKind() const;
    Table  statTable() const;
    const rc::PlayerProfile::Record* statRecord(const rc::PlayerProfile& p, int kind) const;
    void   fillStats();
    void   scrollStats(int step);
    void   changeInfoShot(int step);
    // after one of infoEnums has changed by `step`
    void   changeInfoEnum(Enum& e, int step);
    void   infoTick(float dt);
    // in the units the page is drawn in; true if it was taken
    bool   infoClick(float mx, float my);
    void   infoHover(float mx, float my);
    bool   infoKey(Tempest::Event::KeyType key);
    void   drawInfo();
    void   drawMapInfo();
    void   drawStats();
    // a track not open shown on another page of the championship's frame: back to the last open one (0x4be720)
    void   champTrackFix();

    // Page::ChampRace (0x4c0610, 0x4c0b80)
    void   fillRivals();
    void   champRacePill(size_t i);
    void   drawChampRace();
    // Page::Garage (0x4c5f00): its pages, 0 the cars, 1 .. 3 boost, engine, tires
    void   buildGarage(int page);
    void   garageAction(int id);
    void   drawGarage();
    Championship::Part garagePart() const;
    int32_t  partLevel(const rc::PlayerProfile& p) const;
    void     upgradeAction(int id);
    // Page::ChampWin (0x4c1790)
    void   drawChampWin();
    // the bonus part of the finish table in the championship (0x4c47e0, dlgFINISH.ini)
    void   drawFinishBonus();
    // the car the player chose, as the player has it (0x4aca10), stock when not bought (0x50bb60)
    Preview carLook(size_t car) const;
    // "1:23:38" (0x49ec00, form 2)
    static std::string clock(float seconds);
    void   drawCursor();

    Sounds&                 sounds;
    rc::TextTable           texts;
    Font                    font20, font26;   // Smash20 (0x1499d54), Smash26 (0x1499d58)
    Font                    numbers;          // cock_num (0x1499d50): the championship bonus' total

    std::array<Tex,4>       podl;        // Podl_LeftTop, Podl_RightTop, Podl_LeftBottom, Podl_RightBottom
    std::array<Tex,9>       plates;      // ButtonPodl_right_1..9
    Tex                     buttons, radio, raceBtn, backBtn, flare, header, arrows, msgBox, logo, cursor,
                            progressor, scrollbar;
    Image                   desktop;     // Language/<language>/Desktop.csi: the loading screen
    rc::CurveSpline         focusIn, focusOut, blick, blickAlpha;

    Page                    current = Page::None;
    Page                    next    = Page::None;  // after the buttons have gone
    Command                 nextCmd = Command::None;
    Command                 command = Command::None;
    uint32_t                headerText = 0;
    // the change of screen: the right buttons coming in (2) or going out (1) over 0.5 s (+0xb8, +0xbc); the
    // header the same (0x4b63a0 +0x40, +0x44)
    int                     slide  = 0;
    float                   slideT = 0;

    std::vector<Button>     right;      // slots 0..7, then the back and race buttons
    std::vector<Pill>       pills;
    std::vector<Enum>       enums;      // QuickRace: laps, skill; GhostRace: the ghosts' kind
    Players*                players = nullptr;
    std::unordered_map<std::string,FaceTex> faces;   // by the player's name
    // Page::Finish: its cars, the time it has been open, the time it has been going (< 0 not) and what then
    std::vector<FinishRow>  finishRows;
    float                   finishT   = 0;
    float                   finishOut = -1;
    Command                 finishCmd = Command::None;
    int32_t                 finishHits = 0;
    bool                    championWon = false;
    // Page::Champ: the first row of the tracks shown, the row under the mouse
    size_t                  champTop = 0;
    int                     champHover = -1;
    // the page of the frame's radio buttons shown, its "<  >" (the track's and the pictures', or the kind of the
    // results), the pictures' row still moving (in pictures), the players of the results in their order
    int                     infoPage = 0;
    std::vector<Enum>       infoEnums;
    float                   infoShotScroll = 0;
    std::vector<size_t>     statRows;
    size_t                  statTop = 0;
    // Page::ChampRace: the opponents of the race (AIRace<n> AI<n> of the expert races)
    struct Rival {
      std::string                 name;
      const std::vector<uint8_t>* face = nullptr;
      std::string                 faceKey;
      int32_t                     car = 0, boost = 0, resonator = 0, tires = 0;
      };
    std::vector<Rival>      rivals;
    // Page::Garage: the page shown, where "Wróć" goes, the car's "<  >" and the upgrade's; the upgrade just
    // bought blinks for 5 s (0x4d4a00: the car shown without it every other 0.2 s)
    int                     garagePage = 0;
    Page                    garageBack = Page::Main;
    Enum                    garageCar, upgradeEnum, upgradeCar;
    float                   blinkT = -1;
    int                     blinkPage = 0;
    // Page::Players: the list's rows (players' indices in the order shown), the one chosen, the first shown
    std::vector<size_t>     rows;
    size_t                  selRow  = 0;
    size_t                  topRow  = 0;
    int                     sortBy  = 0x11;   // 0x14a52f4: 1 name, 2 scores, 4 money, 8 time; 0x10 the larger first
    std::string             editing;          // 0x14a52f0: whose picture the "<  >" changes ("Zdjęcie")
    Enum                    faceEnum;
    int                     hoverRow = -1;
    bool                    hoverUp = false, hoverDown = false;
    Message                 message;
    NameEntry               nameEntry;
    Enum                    trackEnum, carEnum;
    Setup                   choice;
    float                   carYaw   = 0;
    float                   carAlpha = 0;
    // the main menu's pictures (0x4ca000): four of each track, the chosen one in the middle, its track the one
    // raced; `shotScroll` the row still moving to it, in pictures
    size_t                  shotAt     = 0;
    float                   shotScroll = 0;

    float                   mouseX = -1, mouseY = -1;  // 800x600 units
    bool                    mouseIn = false;
    float                   time = 0;
    float                   fade = 1;   // the opacity of what is drawn (the screen's controls changing)
    float                   wide = 0;   // how much wider than 800 units the window is
    float                   tall = 0;   // how much higher than 600 units it is (narrower than 4:3)
    float                   dx   = 0;   // moves what is drawn right, in units
    float                   dy   = 0;   // and down
  };
