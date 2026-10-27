# Formaty danych RC Cars

Co wiemy o plikach gry i jak je czytamy. Zachowanie gry, które z nich korzysta, opisuje
[rccars_exe.md](rccars_exe.md).

- [Układ współrzędnych i skala](#układ-współrzędnych-i-skala)
- [RCCars.pack](#rccarspack)
- [Tekstury .csi](#tekstury-csi)
- [Sceny .sb](#sceny-sb)
- [Ustawienia i krzywe](#ustawienia-i-krzywe)
- [Nagrania przeciwników (CarProfiles)](#nagrania-przeciwników-carprofiles)
- [Ścieżki postaci i aut](#ścieżki-postaci-i-aut)
- [Pliki obok archiwum (Language, Tracks)](#pliki-obok-archiwum-language-tracks)
- [Gracze (Players, RCCars.crs)](#gracze-players-rccarscrs)
- [Nazwy tras](#nazwy-tras)

## Układ współrzędnych i skala

- Sceny są **prawoskrętne, oś Y w górę** (eksport z Mayi). Potwierdzone porównaniem renderu z góry z mapą trasy
  z menu gry (`Textures.1/shot_beach1_L.csi`) oraz czytelnymi napisami na szyldach.
- Ściany mają odwrotne nawinięcie względem domyślnego w Tempeście; nieprzezroczysta geometria używa `CullMode::Front`.
- Auto: osie X w lewo, Y w górę, Z do przodu; koła `WHEEL_FRONT_LEFT` itd. (Hummer ma 6 kół). Auta są w skali
  modeli RC (ok. 0,4–0,6 m długości).
- Bullet (u nas tylko rekwizyty) liczy w skali ×10, bo nie lubi obiektów 0,4 m; na zewnątrz `physics/` wszystko
  jest w metrach (`physics/btconvert.h`).

## RCCars.pack

- Katalog plików i zaszyfrowane dane: XOR ze strumieniem klucza `CREATSTUDIOGames`, k → 3k+1 (`formats/pack.*`).
- Zaszyfrowany plik to `[31 14][u32 koniec][u32 rozmiar][dane]`; rozmiar w katalogu to rozmiar samych danych,
  więc cały blok jest o 10 bajtów dłuższy.
- Dźwięki są w `Sound\*.wav` jako zwykłe PCM 16 bit (44,1 kHz mono, menu stereo).

## Tekstury .csi

RGB565, RGB888, ARGB8888, ARGB4444, ARGB1555 i A8I8, z klasyfikacją przezroczystości (`formats/csi.*`). Wszystkie
1316 tekstur z archiwum dekodują się poprawnie (`rcinfo check`).

## Sceny .sb

CREAT Studio Scene Project 6.0: drzewo elementów `[id][typ][koniec]` (`formats/chunk.h`, `formats/scene.*`). Węzły
mają siatki, UV, normalne, warstwy tekstur (`BAS1`/`LM_1`/`ALP1`/`BAS2`), osadzone lightmapy, markery i skrypty.
Wszystkie 18 plików parsuje się poprawnie. Ten sam format elementów mają nagrania `CarProfiles/*.dat`.

### Siatki (MESH) i LOD-y

- Siatki z sufiksem `_LOD<n>` to kopie pod tym samym rodzicem. Przy obiektach pełna siatka nie ma sufiksu, a
  `_LOD1`/`_LOD2` to uproszczenia (pomijane). Morze składa się wyłącznie z LOD-ów: `_LOD1` jest najgrubszy, najwyższy
  numer najdokładniejszy, a grupy daleko od trasy mają tylko `_LOD1`/`_LOD2`. Rysujemy najdokładniejszy z każdej grupy.
- Nagłówek `0x08` węzła `MESH`: flaga `0x20000` (`SbMesh::flags`) daje siatce mapę otoczenia (węzły mórz
  `more1`/`more2`, `ENVIR_CAR_BODY_body` aut). Słowo 4 (`SbMesh::renderFlags`) mówi, jak siatkę rysować
  (`RCCars.exe` 0x48e4b0, 0x451310): `0x1000` przezroczystość z alfy tekstury, `0x2000` morza, `0x40000000` mnożenie
  przez to, co pod spodem (×2). Ten ostatni bit mają tylko paski `wt_wetsand` (151 siatek na wszystkich trasach).
- Auta w `Car.sb`: modele `Car<n>`, ich uproszczenia `Car<n>LOD1`, `Car<n>LOD2` i węzły ulepszeń `UPGRADES*`
  (`booster_N`, `compressor_N`, opony `tire2*`/`tire3*`, końcówka dopalacza `booster_N_end`).

### Tekstury węzła (GLTX)

- Indeksy tekstur `GLTX` są lokalne dla węzła `MODL`: w `Car.sb` każde auto (i każdy LOD) numeruje tekstury od zera.
- Nagłówek `GLTX` (element `0x08`) to tablica 5 liczb: indeks, 0, dwa słowa flag i **materiał**: bity
  4 mokry piasek, 5 piasek (`sand_halfdry`), 6 udeptany/głęboki piasek (`Sand_trample`), 7 drewno, 8 trawa, 9 żwir
  (`Sand_gravel`, `path_*`), 10 asfalt, 11 kamień/beton/skała, 12 metal (po jednym bicie na teksturę, na wszystkich
  trasach). `RCCars.exe` zna tylko `surf_default|sand|wetsand|dunesand|gravel` (`Settings/surf_*.crs`: same parametry
  kurzu); kolejność nazw i kolory kurzu pasują do bitów 4–6 i 9.
- Woda to ściany z teksturą `sea` (bez bitu materiału; `RCCars.exe` szuka jej po nazwie).
- Piana przy brzegu to ściany `coast*` z teksturą `coast_base` i maską `ALP1` `coast` bez `BAS2`: maska to
  przezroczystość piany.

### Kolizje (COLL)

`COLL` (wewnątrz `DESC`) nie zawiera osobnej geometrii: to jednorodna siatka komórek (np. 156×12×320 po 0,5 m na
beach_1) z listą par (siatka, maska bitowa ścian) na komórkę; indeksy komórek `z + nz*(y + ny*x)`, bity od
najmłodszego. Kolizje używają więc geometrii renderu, łącznie ze ścianami bez tekstury (niewidoczne bariery) i
niektórymi siatkami `_LOD*`. Zweryfikowane na wszystkich 10 trasach: każda ściana nachodzi na swoją komórkę.
U nas z tego powstaje siatka trójkątów w przestrzeni świata z raycastem po komórkach (DDA, `formats/collision.*`).
Woda (`sea`) leży w `COLL` jak reszta, w kilku kopiach `_LOD1`–`_LOD4` na tej samej wysokości.

### Markery (MARK)

- Element `0x04` to właściwości wpisane w edytorze poziomów jako pary tekstów (lista
  `0x05 { 0x06 klucz, 0x07 wartość }`). Punkty kontrolne mają `width`, `height` i `expand` (np. beach_1 `cp_1`:
  szerokość 8, wysokość 1, expand 3); tam, gdzie ich brak, gra bierze je z właściwości folderu `Checkpoints`
  (`FOLD`, np. country_2: `expand` 2, `width` 3), a gdy i tam ich nie ma, 2, 1 i 0,1 m.
- Zagnieżdżone markery (`cp_1` → `cp_1_1`, `cp_1_2` …) mają transformacje w przestrzeni świata, nie względem rodzica
  (w przeciwieństwie do siatek): kolejne podpunkty prowadzą od `cp_1` do `cp_2`. Główne markery `cp_N` wiszą
  najwyżej 0,5 m nad ziemią, pośrednie do 2,7 m. Numeracja podpunktów ma dziury (country_1: `cp_4_3`, `cp_4_5`) i
  nie zawsze idzie po kolei w pliku.
- Inne markery: `water_splash_N`, `water_wave_N`, `SUN_AF`, znaczniki postaci (np. `landhole1`, `seahole1`, `hole1..N`).

### Bryły (EVOL)

Dziecko `0x03` nazwa, `0xd0` kształt (u32), `0xd4` liczby; kształt 2 to graniastosłup `[ymin, ymax, x0, z0, x1, z1, …]`
(wielokąt w XZ). Bryły to m.in. `water_box`, `car_reset_volume_*` (beach_2, beach_3, country_2, country_4), `roadcar_*`
(tunele) i bryły postaci (`visibilityVolume`, `guardVolume` …).

### Instancje obiektów (INST)

- `0x03` nazwa, `0x0a` rodzaj (3, a 2 przy rekwizytach położonych na boku; znaczenie nieznane, obrót i tak jest w
  transformacji), `0x0b` transformacja, `0x72` nazwa modelu (`MODL` w innej scenie), `0x74` scena (`RoadCar` =
  `Roadcar.sb`; puste: gra szuka sama w `Car.sb`, `stone.sb`, `people.sb`, `AIChars.sb`).
- Start auta gracza to `INST 'Player'` w folderze `Players`; jego oś +Z wskazuje kierunek jazdy. Rekwizyty leżą w
  folderach `Stones`/`stones`, postacie w `AI Characters`, `Peoples`, `Animals` … Ciężarówki i sępy jednej trasy
  stoją wszystkie w jednym punkcie (pojawiają się tam i ruszają po ścieżkach).
- Instancja ustawia **węzeł główny** modelu, a nie początek układu sceny: w `stone.sb` modele stoją obok siebie
  (pachołek ma węzeł główny w (3,88; 0,10; −3,84), piłka 0,12 m nad ziemią w swoim środku). Liczone od węzła
  głównego środki mas ze skryptów wypadają tam, gdzie powinny: 0 dla piłek i puszek to ich środek, 0,03 dla
  pachołka to tuż nad ciężką podstawą.
- Skrypt instancji podaje ustawienia obiektu, np. postaci (znaczniki, bryły, `clother`) i malowanie aut z ulicy
  (`body` 1/2).
- `HHID` to tylko odcinanie niewidocznych siatek; nie jest potrzebne.

### Skrypty modeli (SCRI)

Tekst `u32 długość` + treść w formacie `Section Keys`. W `stone.sb`: `Geom` `type` (1 kula, 2 walec, 3 stożek,
4 butelka, 5 wiadro, 6 łuska, 7 czajnik, 8 balonik), `shiftCenterMassOY` (środek masy nad węzłem głównym, m);
`Params` `mass` (kg), `coeffHookNorm` (odbicie: 0,7 piłki, 0,3 reszta), `coeffFrict`; kłębek (`Klubok`) ma jeszcze
`ExtForces` `fWindBlow` (toczy go wiatr).

### Szkielety i animacje

Siatki postaci są związane ze szkieletem w pozie z elementu `0x38` węzła (pozycja, obrót w stopniach, skala; ręce
w bok), a nie w pozie `0x0b`; `ANIM` węzła ma dla każdej sekwencji (`ASEQ`) pozę spoczynkową i kanały `SPLN`
(`ROTX` … w stopniach). `formats/skeleton.*`.

### Dźwięki (SNCH)

Kanały `SNCH` (lista węzłów `SOND`) siedzą w `RCCars.sb` (np. kanał `Checkpoint` = `cp`, `cp_beside`, `cp_start`,
`cp_reset`, `cp_wrongway`) albo w modelach innych scen (`people.sb` `Man` = `man_voice`, `AIChars.sb` `Dog` =
`dog_attack`, `dog_pain`, `stone.sb` `ball`, `pepsi`, `klubok` …). `formats/soundbank.*`.

### Skrypty wyścigu (IRCS)

Odliczanie to skrypty w `RCCars.sb`: `RaceEntry` ma `1.0 Show3`, `2.0 Show2`, `3.0 Show1`, `4.0 StartRace` (sekunda
bez niczego, potem cyfry), `RaceAgain` („jeszcze raz”) to samo bez tej sekundy, start po 3 s.

## Ustawienia i krzywe

- `Settings/*.ini` to pliki suwaków narzędzia do strojenia: `Key "nazwa"`, `Values min max pozycja`, opcjonalnie
  `Mask float` + `FValues od do` (pozycja przeliczana liniowo na ten zakres) albo `Mask spline` (nazwa pliku `.bspl`).
  `RCCars.exe` przelicza każdy suwak liniowo z pozycji 1..100 (bez względu na zakres zapisany w pliku) na własny,
  wpisany w kod zakres: `od + (s-1)·(do-od)/99`; suwaki efektów dzieli przez stałą z kodu (brak klucza to 0).
- Obok leżą skompilowane `.crs` (rekordy `[u16 znacznik][u32 koniec]`, nazwy XOR 0x25), ale zwykle ze starszymi
  wartościami; czytamy `.ini`. Część ustawień efektów jest tylko w postaci `.crs`, `car_dustx` tylko jako `.ini`.
- `Splines/*.bspl` z edytora krzywych: punkty kontrolne to elementy `5b/80` z trzema floatami `(0, x, y)`.
- `Splines/*.spl` to tekst: liczba odcinków i po cztery punkty (początek, dwa uchwyty, koniec), liczone jako Hermite
  (`RCCars.exe` 0x40f830).
- Zostały w danych z edytora, a `RCCars.exe` ich nie czyta: `splAccelBase<N>.bspl`, `splAccelGrav<N>.bspl`,
  `Height2Stiffness.bspl`, `AngleSteer2RearDrift.spl` i `Settings/Water.ini` (`speedDropCenter`, `speedDropOther`,
  `DampingVelocity2`).

## Nagrania przeciwników (CarProfiles)

`CarProfiles/<trasa>/<nazwa>.dat` mają format elementów scen: nagłówek `0x60` (m.in. `g` = 136, długość klatki, i
teksty „Time path FULL: 68.185939”, „CYCLE: 65.022942”), `0x63` (liczba klatek, czas), `0x64` klatki, `0x65` lista
zdarzeń (nieużywana). Klatka (ok. 100 na sekundę) to pełny stan auta: czas, pozycja początku układu modelu,
kwaternion (w x y z), prędkość, prędkość kątowa, 4 długości zawieszenia, obroty kół (?) i na końcu słowo klawiszy
(0x01 gaz, 0x10 prawie zawsze z nim, 0x20 zdarzenie z numerem w górnych 16 bitach; `RCCars.exe` 0x5029b0).

Nagranie to jedno okrążenie od miejsca w szyku: od czasu FULL − CYCLE do końca to pętla (koniec 0,01–0,25 m od jej
początku). Odmiany `_2`, `_3` zaczynają się tak samo i inaczej jadą okrążenie; `_3` bywają skrótami
(`surf\over1_3` omija pętlę `cp_4_1` … `cp_4_6`). `Demo*` to długie (ok. 220 s) nagrania do dema. Miejsce gracza w
szyku daje `PlayerPath` i `start.dat`.

## Ścieżki postaci i aut

- `Profiles/<nazwa>.dat`: ścieżki ludzi; rekordy liczą się co drugi (0, 2, 4 …), każdy ze stanem (np. 1, 2, 5, 7;
  na beach_3 0, 5, 6, 8).
- `Profiles/vlt_<trasa>_<1|2>.gpf`: lot sępów, nagłówek 0x28 bajtów, potem pozycja, przód i góra na klucz.
- `Profiles/rdc_<trasa>_forw.gpf` i `_back.gpf`: ruch uliczny, 8 liczb na klucz (pozycja 0,75 m nad początkiem
  modelu, kierunek, skręt, pochylenie). Nagrania beach_2 i beach_3 zaczynają się kluczami cofającymi się w czasie.

## Pliki obok archiwum (Language, Tracks)

- Obrazki z napisami są w `Language/<język>/*.csi` (poza archiwum), np. `msg_321_s_f.csi` to „3”, „Jazda!”/„GO!”,
  „2”, „1”, „meta”/„Finish” w jednej teksturze; `place1..6.csi` to „1szy” … „6ty”; `msg_hits.csi` to „Łup!” i
  „Wielkie Łup!”.
- Czcionki `Smash20/26.csi` to siatka 10 kolumn × 9 wierszy (25,6 × 28,4 px) ze znakami w kolejności z `Smash*.dat`;
  szerokość liter mierzymy z kanału alfa. Metaliczne cyfry `cock_num` oryginał trzyma w samym `RCCars.sb`.
- `Language/Game/<język>.tbl` i `Language/System/<język>.tbl`: teksty gry, elementy `HSRC`, `CSRC`, `LSRC`, `DSRC`
  z tekstami zaszyfrowanymi XOR-em kluczem z `CSRC`, w Windows-1250 (polski) albo ASCII (`formats/texttable.h`,
  `RCCars.exe` 0x401920).
- `Tracks/`: muzyka MP3; `Tracks/Intro.dat` (84 MB) to intro w nieznanym formacie.

## Gracze (Players, RCCars.crs)

`Players/<plik>.scp` to jeden gracz (czyta 0x4e7740, zapisuje 0x4e7e70, `formats/playerprofile.h`): drzewo elementów
`[u8 id][u8 typ][u32 koniec]` jak w scenach, exe porównuje 16-bitowy znacznik `id | typ<<8`. Przed zapisem stary plik
idzie do `<plik>.bak`, a plik, który się nie czyta, gra kasuje i czyta `.bak`. Nazwa pliku to imię z literami
i cyframi (7 bitów, `isalnum`), każdy inny znak to losowa cyfra 0–8; najwyżej 20 graczy.

- `0x5020` korzeń `[f32 1.0]`; `0x4021` imię (do 15 znaków, Windows-1250); `0x1422 [u32 160]` nagłówek `.csi`
  zdjęcia i `0x1423 [u32 n]` jego piksele (128x256, ARGB, wiersze od góry); `0x5024` czas gry (s), `0x3025` forsa,
  `0x3026` wyniki, `0x5027` przejechane metry.
- `0x3230 [u32 auto 0..2]`: `0x0031` kupione (pusty element), `0x3032` dopalacz, `0x3033` silnik, `0x3034` koła,
  `0x3035` malowanie.
- `0x3240 [u32 trasa 0..9]`: `0x0270` najlepsze okrążenie, 20 × `0x3271 [u32 n]` najlepszy wyścig na n okrążeń,
  `0x0279` najlepszy wyścig mistrzostw (w polu czasu jest wygrana kwota), `0x5044` czas jazdy (s), `0x3045` liczba
  wyścigów, `0x3046` najlepsze miejsce (0 = pierwsze, `INT_MAX` = brak), `0x5047` przejechane metry.
- Rekord: `0x5072` czas (2 000 000 = brak), `0x3073` auto, `0x3074`–`0x3076` jego ulepszenia, `0x2477 [u32 4]` dzień,
  miesiąc, rok, 0 i `0x2478 [u32 4]` godzina, minuta, sekunda, ms.
- Na końcu `0x3050`–`0x3053` ostatnia trasa, auto, okrążenia i umiejętności przeciwników, `0x4060` imię z chwili
  założenia, `0x3061` liczba, której znaczenia nie znamy (zawsze 0).

Nasz zapis daje bajt w bajt ten sam plik (`rcinfo <gra> scp Players/<plik>.scp` to sprawdza). Nagłówek zdjęcia ze
starego pliku zawiera wskaźniki z pamięci gry; my przy nowym zdjęciu zostawiamy w tych miejscach zera.

`RCCars.crs` (ustawienia, „CREAT Studio configuration”) to drzewo rekordów `CSRC [u32 długość][nazwa]`: sekcja ma
`[u16 dzieci]` i 6 zer, wartość `[u16 typ][u16 rozmiar][u16 pojemność][u16 0]`, dane i `[u32 0]` (typ 1 liczba,
2 napis, 3 struktura). Wybrany gracz to napis „Player”/„Name” (pojemność 16), a opcje zmieniają swoje rekordy
(`rcinfo <gra> crs`). Struktura typu 3 to klawisze akcji w `Player1`/`Player2`: 44 bajty, `[u32 1]` i dwa klawisze
`[urządzenie][liczba kodów][3 kody]` (pojemność równa rozmiarowi).

## Nazwy tras

Trasy w `ailayouts.ini` i `championship.ini` mają nazwy z gry: 1 Surf (beach_1), 2 Fishers (country_3), 3 Fort
(beach_3), 4 AAD (urban_2), 5 Mines (country_2), 6 Camping (beach_2), 7 Avax (urban_1), 8 Rancho (country_1),
9 Silo (country_4), 10 War path (beach_4).

Mapy kokpitu `trackmap_1..10` (512x512) są obrócone dowolnie; która należy do której trasy i jak leży na niej świat,
dopasowaliśmy do pomarańczowych kropek (to `map_cp` w miejscach głównych punktów `cp_N`) i do narysowanej trasy:
beach_1..4 → 1, 6, 3, 10; country_1..4 → 8, 5, 2, 9; urban_1..2 → 7, 4 (tabela `TrackMaps` w `game/hud.cpp`;
odchyłki do kilku texeli). `map.ini` podaje róg mapy (620, 79).
