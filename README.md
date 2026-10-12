# OpenRCCars

Szkielet open-source'owej reimplementacji **RC Cars** (Creat Studio, 2002) na silniku
[Tempest](https://github.com/Try/Tempest), tym samym, na którym działa OpenGothic.

Projekt nie zawiera żadnych danych gry. Wczytuje assety z legalnie kupionej instalacji
(Steam/GOG): `RCCars.pack` oraz `RCCarsDB/*.sb`.

![Surf (beach_1)](docs/beach_1.jpg)

## Co już działa

- **RCCars.pack**: odczyt katalogu i deszyfrowanie plików (XOR ze strumieniem klucza `CREATSTUDIOGames`, k → 3k+1)
- **.csi**: dekodowanie tekstur (RGB565, RGB888, ARGB8888, ARGB4444, ARGB1555, A8I8) z klasyfikacją przezroczystości;
  wszystkie 1316 tekstur z archiwum dekodują się poprawnie
- **.sb** (CREAT Studio Scene Project 6.0): hierarchia węzłów, siatki, UV, normalne, warstwy tekstur
  (BAS1/LM_1/ALP1/BAS2), osadzone lightmapy, markery (checkpointy `cp_*`); wszystkie 18 plików parsują się poprawnie
- **renderer**: trasa z teksturami, lightmapami, mieszaniem tekstur terenu, alpha-testem, przezroczystością i mgłą;
  przełączanie 10 tras
- **auto**: model z `Car.sb` (Overkill, Buggy, Hummer) stoi na pozycji startowej gracza (`INST 'Player'`), osadzony
  na ziemi; nadwozie i koła są osobnymi częściami (pod obrót i skręt kół); kamera za autem albo kamera swobodna
- **kolizje trasy** z węzła `COLL`: siatka komórek nad trasą, w każdej lista ścian siatek renderu, które ją przecinają;
  z tego powstaje siatka kolizji (trójkąty w przestrzeni świata) z raycastem po komórkach (DDA). Auto staje na
  ziemi wyznaczonej raycastem. Klawisz **K** pokazuje nakładkę kolizji: zielone podłoże, żółte zbocza, czerwone
  ściany, niebieskie niewidoczne ściany (ściany bez tekstury)
- **fizyka jazdy** ([Bullet](https://github.com/bulletphysics/bullet3) `btRaycastVehicle`): auto jeździ po trasie
  z klawiatury. Koła to promienie rzucane przez siatkę kolizji `COLL` (raycast z kroku 2), nadwozie to prostopadłościan
  zderzający się z tymi samymi trójkątami (ściany, słupy, niewidoczne bariery). Parametry każdego auta z
  `Settings/engine|springs|tires|steering|resistance|cdt<N>.ini`, krzywa przyspieszenia z `Splines/splAccelBase<N>.bspl`,
  ulepszenia (dopalacze, rezonatory, opony) z `Scripts/upgrades.ini`. Koła kręcą się, skręcają i pracują na
  zawieszeniu; klawisze jak w instrukcji oryginału (`Instrukcja.pdf`: spacja skok, Shift dopalacz, B blokada kół);
  dopalacz jedzie po krzywej `Splines/OVERKILL|BUGGY|HUMMER.bspl` (32/38/30 km/h zamiast 25/30/20), zbiornik turbo
  ładuje się z czasem (w oryginale na torze; jak dokładnie, jeszcze nie wiemy); skok (akcja `Jump` w `RCCars.exe`, bez parametru w danych, więc wysokość
  ok. 0,3 m jest dobrana na oko); auto przewrócone na dach wraca na koła po 2 s
- **nawierzchnie i woda**: rodzaj nawierzchni pochodzi z bitów materiału tekstury (`GLTX`): piasek, mokry piasek,
  głęboki piasek (`Sand_trample`), żwir i reszta. Jak w danych oryginału, jazdę zmieniają tylko głęboki piasek i woda
  (`Settings/tires<N>.ini`: `coeffDeepSand`, `coeffWater`; Hummer na dużych kołach mniej traci w wodzie); pozostałe
  nawierzchnie różnią się w oryginale tylko kurzem spod kół. Woda (tekstura `sea`: morze, strumienie, kałuże,
  wodospady) nie jest twarda: auto brodzi w płytkiej wodzie i zwalnia, w głębokiej tonie, a po 1 s całkiem pod wodą (po dach, antena się nie liczy)
  wraca do ostatniego punktu kontrolnego, jak w instrukcji („jeśli utoniesz w morzu”). Tam też wraca po klawiszu R
  i po wypadnięciu poza trasę. Nazwa nawierzchni pod autem jest w tytule okna
- **wyścig**: odliczanie 3, 2, 1, „Jazda!” (auto stoi do startu), 3, 5 albo 7 okrążeń, czas całkowity i czas
  okrążenia, najlepsze okrążenie, „zły kierunek”, meta z tabelą czasów i „jeszcze raz”. Na razie bez przeciwników.
  Punkty kontrolne to bramki o szerokości i wysokości zapisanej w danych trasy (właściwości markerów `cp_*`);
  okrążenie liczy się po przejechaniu wszystkich bramek po kolei, jak w instrukcji. Kokpit rysujemy obrazkami
  z gry (`Language/Polish` albo `Language/English`: odliczanie, „okrążenie”, komunikaty, czcionka `Smash26`)
  w miejscach z `Settings/cockpit.ini`; strzałka (szewron `coc_str_2`) pokazuje następny punkt kontrolny
  i miga na czerwono, gdy jedziesz w złą stronę. Prędkość i turbo na dole ekranu na razie tekstem
- **narzędzia**: `rcinfo` (inspekcja/eksport danych i jazda testowa, bez GPU) i `rcshot` (render offscreen do PNG)

![Auto na starcie (beach_1)](docs/car_beach_1.jpg)

![Nakładka kolizji (beach_1, klawisz K)](docs/collision_beach_1.jpg)

![Jazda: skręt w lewo (beach_1)](docs/driving_beach_1.jpg)

![Auto wjeżdża do morza: na plaży, przy brzegu, w wodzie (beach_1)](docs/water_beach_1.jpg)

![Wyścig: odliczanie, jazda, zły kierunek, meta (beach_1)](docs/race_beach_1.jpg)

![Urban 1](docs/urban_1.jpg)

## Budowanie (Windows)

Wymagania:
- Visual Studio 2022 z obciążeniem *Programowanie aplikacji klasycznych w C++* (zawiera CMake)
- [Vulkan SDK](https://vulkan.lunarg.com/sdk/home) **1.4.357 lub nowszy** (Tempest używa nowych rozszerzeń; instalator ustawia zmienną `VULKAN_SDK`)
- Git (CMake przy pierwszej konfiguracji sam pobiera [Bullet](https://github.com/bulletphysics/bullet3) 3.25)

```bat
git clone --recursive https://github.com/maxis27/OpenRCCars.git
cd OpenRCCars
:: jeśli sklonowano bez --recursive albo rozpakowano z archiwum:
git submodule update --init --recursive

cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config RelWithDebInfo
```

Uruchomienie:

```bat
build\bin\RelWithDebInfo\OpenRCCars.exe -g "D:\SteamLibrary\steamapps\common\RCCars"
```

Opcje: `-g <folder gry>` (domyślnie bieżący katalog), `-l <trasa>` (domyślnie `beach_1`),
`-dx12` (backend DirectX 12, eksperymentalnie), `-validation` (warstwy walidacji Vulkana).

### Linux

```sh
sudo apt install build-essential cmake ninja-build libx11-dev libxcursor-dev glslang-tools libvulkan-dev
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
./build/bin/OpenRCCars -g ~/.steam/steam/steamapps/common/RCCars
```

Jeśli systemowe nagłówki Vulkana są starsze niż 1.4.357, ustaw `VULKAN_SDK` na katalog z aktualnymi
[Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) (podkatalog `include`).

Same narzędzia do danych (bez Tempesta i GPU): `cmake -B build -DOPENRCCARS_TOOLS_ONLY=ON`.

## Sterowanie

| Klawisz | Akcja |
|---|---|
Jazda (kamera za autem):

| Klawisz | Akcja |
|---|---|
| W / ↑ | gaz |
| S / ↓ | hamulec, po zatrzymaniu wsteczny |
| A / ← , D / → | skręt w lewo / w prawo |
| Spacja | skok |
| Shift | dopalacz (zużywa turbo, ok. 3 s z pełnego zbiornika; procent w tytule okna) |
| B | zablokuj koła (tylne, do poślizgu) |
| T | postaw auto na koła w miejscu |
| R | powrót do ostatniego punktu kontrolnego (przed pierwszym: na start) |
| Enter | wyścig od nowa, od odliczania („Jeszcze raz”) |
| L | liczba okrążeń: 3 / 5 / 7 (i wyścig od nowa) |
| P, Pause | pauza |
| U | następny poziom ulepszeń (0–3: dopalacze, rezonatory, opony razem) |
| 1 / 2 / 3 | auto: Overkill / Buggy / Hummer |

Pozostałe:

| Klawisz | Akcja |
|---|---|
| V | kamera za autem ↔ kamera swobodna (auto wtedy stoi) |
| K | nakładka kolizji (debug; turkusowa woda, fioletowy głęboki piasek) |
| N | przeskok do następnego punktu kontrolnego (debug, do sprawdzania okrążeń i mety) |
| W / S / A / D | ruch kamery swobodnej |
| E, Spacja / Q, C | w górę / w dół |
| Shift | szybciej ×4 |
| przeciąganie myszą, strzałki | obrót kamery swobodnej |
| kółko myszy | prędkość kamery |
| F1–F10 | trasy: beach_1–4, country_1–4, urban_1–2 |
| Esc | wyjście |

Prędkość auta (km/h) jest też w tytule okna.

## Narzędzia

```sh
rcinfo <folder_gry> pack                          # statystyki archiwum
rcinfo <folder_gry> check                         # dekoduje wszystkie tekstury i sceny (test regresji)
rcinfo <folder_gry> sb RCCarsDB/beach_1.sb        # statystyki sceny, tekstury, markery
rcinfo <folder_gry> obj RCCarsDB/Car.sb car.obj   # eksport geometrii do OBJ
rcinfo <folder_gry> coll RCCarsDB/beach_1.sb [coll.obj]  # siatka kolizji: statystyki, test raycastu, eksport OBJ
rcinfo <folder_gry> drive RCCarsDB/beach_1.sb [auto 1-3] [ulepszenie 0-3]  # jazda testowa bez GPU (autopilot po checkpointach)
rcinfo <folder_gry> race RCCarsDB/beach_1.sb [okrążenia]  # bramki punktów kontrolnych i test logiki wyścigu (bez fizyki)
rcinfo <folder_gry> extract "Scripts\upgrades.ini" upgrades.ini
rcinfo <folder_gry> csi "Textures.1\ang_dor.csi" ang_dor.tga
rcshot <folder_gry> beach_1 shot.png [w h] [x y z yaw pitch]   # render offscreen, z autem i kamerą za nim
RCSHOT_CAR=Car3 rcshot ...                                      # inne auto (Car1/Car2/Car3), none = sama trasa
RCSHOT_COLL=1 rcshot ...                                        # z nakładką kolizji
RCSHOT_DRIVE=3 RCSHOT_STEER=1 rcshot ...                        # najpierw 3 s jazdy na pełnym gazie, druga połowa ze skrętem
RCSHOT_DRIVE=2 RCSHOT_JUMP=1.6 rcshot ...                       # skok w 1,6 s, zdjęcie w 2 s
RCSHOT_DRIVE=3 RCSHOT_BOOST=1 rcshot ...                        # z dopalaczem
RCSHOT_START="-8 -41 90" RCSHOT_DRIVE=1.6 rcshot ...            # auto najpierw w punkcie x z, obrócone o yaw (tu: do morza)
RCSHOT_TIME=2.5 rcshot ...                                       # chwila animacji wody (fale, piana)
```

## Struktura

```
formats/        formaty danych, czyste C++ bez zależności
  chunk.h       wspólny format elementów [id][typ][koniec]
  pack.*        RCCars.pack + deszyfrowanie
  csi.*         tekstury
  scene.*       sceny .sb
  collision.*   siatka kolizji z węzła COLL, raycast, nawierzchnie
  settings.*    pliki suwaków Settings/*.ini, Scripts/upgrades.ini, krzywe .bspl
physics/        fizyka jazdy (Bullet, bez GPU)
  cargeometry.* koła i nadwozie auta z Car.sb
  carparams.*   parametry auta z ustawień gry
  vehicle.*     świat Bulleta trasy, btRaycastVehicle z promieniami przez siatkę COLL, nawierzchnie i woda
  checkpoints.* bramki punktów kontrolnych w kolejności, postęp auta, powrót do ostatniego punktu
  race.*        wyścig: odliczanie, okrążenia, czasy, zły kierunek, meta
game/           gra (Tempest)
  resources.*   dostęp do instalacji gry, cache tekstur
  camera.*      kamera swobodna
  hud.*         kokpit wyścigu (obrazki i czcionka z Language/<język>)
  world/        Level (trasa), LevelMesh (geometria na GPU, batche po teksturach), CarMesh (auto),
                CollisionView (nakładka kolizji)
  mainwindow.*  okno, pętla, wejście
shader/         GLSL kompilowany do SPIR-V i wbudowywany w plik wykonywalny
tools/          rcinfo, rcshot
lib/Tempest     silnik (submoduł git)
```

## Ustalenia o danych

- Sceny są **prawoskrętne, oś Y w górę** (eksport z Mayi). Potwierdzone porównaniem renderu z góry z mapą trasy
  z menu gry (`Textures.1/shot_beach1_L.csi`) oraz czytelnymi napisami na szyldach.
- Ściany mają odwrotne nawinięcie względem domyślnego w Tempeście; nieprzezroczysta geometria używa `CullMode::Front`.
- Siatki z sufiksem `_LOD<n>` to kopie pod tym samym rodzicem. Przy obiektach pełna siatka nie ma sufiksu, a `_LOD1`/`_LOD2`
  to uproszczenia (pomijane). Morze składa się wyłącznie z LOD-ów: `_LOD1` jest najgrubszy, najwyższy numer najdokładniejszy,
  a grupy daleko od trasy mają tylko `_LOD1`/`_LOD2`. Rysujemy najdokładniejszy z każdej grupy (wcześniej znikały całe
  połacie morza i było widać dno).
- Zaszyfrowany plik w archiwum to `[31 14][u32 koniec][u32 rozmiar][dane]`; rozmiar w katalogu to rozmiar samych danych,
  więc cały blok jest o 10 bajtów dłuższy (wcześniej czytaliśmy o 10 bajtów za mało i "brakowało" końcówek plików).
- `Settings/*.ini` to pliki suwaków narzędzia do strojenia: `Key "nazwa"`, `Values min max pozycja`, opcjonalnie
  `Mask float` + `FValues od do` (pozycja przeliczana liniowo na ten zakres) albo `Mask spline` (nazwa pliku `.bspl`).
  Obok leżą skompilowane `.crs` (nazwy XOR 0x25), ale ze starszymi wartościami; czytamy `.ini`.
  Suwaki fizyki nie mają jednostek; przeliczenie na wielkości fizyczne jest naszym przybliżeniem (`physics/carparams.cpp`).
- `Splines/*.bspl` z edytora krzywych: punkty kontrolne to elementy `5b/80` z trzema floatami `(0, x, y)`.
  `splAccelBase<N>.bspl` to przyspieszenie (y, m/s²) od prędkości (x, czytane jako km/h): Overkill 9,6 → 0 przy
  25 km/h, Buggy 9,6 → 0 przy 30, Hummer 8 → 0 przy 20. `Scripts/upgrades.ini` mnoży je przez współczynniki
  4 poziomów ulepszeń (`BOOSTERS` przyspieszenie, `RESONATORS` prędkość maksymalna, `TIRES` przyczepność).
- Auto: osie X w lewo, Y w górę, Z do przodu; koła `WHEEL_FRONT_LEFT` itd. (Hummer ma 6 kół). Fizyka liczy w Bullecie
  w skali ×10 (z grawitacją ×10), bo Bullet nie lubi obiektów 0,4 m; na zewnątrz wszystko jest w metrach.
- Indeksy tekstur `GLTX` są lokalne dla węzła `MODL`: w `Car.sb` każde auto (i każdy LOD) numeruje tekstury od zera.
- Zagnieżdżone markery (`cp_1` → `cp_1_1`, `cp_1_2` …) mają transformacje w przestrzeni świata, nie względem rodzica
  (w przeciwieństwie do siatek): kolejne podcheckpointy prowadzą od `cp_1` do `cp_2`.
- `COLL` (wewnątrz `DESC`) nie zawiera osobnej geometrii: to jednorodna siatka komórek (np. 156×12×320 po 0,5 m
  na beach_1) z listą par (siatka, maska bitowa ścian) na komórkę; indeksy komórek `z + nz*(y + ny*x)`, bity od
  najmłodszego. Kolizje używają więc geometrii renderu, łącznie ze ścianami bez tekstury (niewidoczne bariery)
  i niektórymi siatkami `_LOD*`. Zweryfikowane na wszystkich 10 trasach: każda ściana nachodzi na swoją komórkę.
- Nagłówek `GLTX` (element `0x08`) to tablica 5 liczb: indeks, 0, dwa słowa flag i **materiał**: bity
  4 mokry piasek, 5 piasek (`sand_halfdry`), 6 udeptany/głęboki piasek (`Sand_trample`), 7 drewno, 8 trawa, 9 żwir
  (`Sand_gravel`, `path_*`), 10 asfalt, 11 kamień/beton/skała, 12 metal (po jednym bicie na teksturę, na wszystkich
  trasach). `RCCars.exe` zna tylko `surf_default|sand|wetsand|dunesand|gravel` (`Settings/surf_*.crs`: same parametry
  kurzu), a z fizyki opon `coeffDeepSand` i `coeffWater`. Kolejność nazw i kolory kurzu pasują do bitów 4–6 i 9.
- Woda to ściany z teksturą `sea` (bez bitu materiału; `RCCars.exe` szuka jej po nazwie). Leżą w `COLL` jak reszta,
  w kilku kopiach `_LOD1`–`_LOD4` na tej samej wysokości. Poziom morza: 0 na beach_1, 2,15 na beach_3; dno pod nim
  to zwykłe ściany kolizji (na beach_1 piasek opada od -0,3 m przy brzegu do -1,4 m). Na country_1 płytkie kałuże
  (`pool1..3`), na country_3 strumień (kilka cm) i wodospady. `Settings/Water.ini` (`speedDropCenter` 125/500,
  `speedDropOther` 76/500, `DampingVelocity2` 950/1000) czytamy jako utratę prędkości przy wpadnięciu do wody
  nadwozia i kół oraz tłumienie pod wodą; to nasze odczytanie nazw.
- Wygląd wody: tekstura `sea` nie ma kanału alfa, ale oryginał rysuje wodę półprzezroczystą (`Settings/WLOD_*.ini`:
  `alphaMin` 26 … `alphaMax` 255, `distAlphaClampMax` 8, fale, env mapa). U nas płytka woda jest przezroczysta wg
  głębokości nad dnem z kolizji (pełne krycie od 0,8 m; jednostka `distAlphaClampMax` to nasze zgadywanie), bez fal.
  Piana przy brzegu to ściany `coast*` z teksturą `coast_base` i maską `ALP1` `coast` bez `BAS2`: maska to
  przezroczystość piany (falista krawędź). W oryginale piana przesuwa się po plaży (`waterLOD_coast.ini`:
  `HeightOn/HeightOff`, `ScrollVel`, `AlphaMin` 36); u nas co 4 s podchodzi na plażę i cofa się (maska przesuwa się
  o 32% paska, przezroczystość 36…255), przesunięta w fazie wzdłuż brzegu.
- Fale: dwie fale kierunkowe wg `WLOD_beach_1_more1.ini` (`angle` 6 i `angle2` 4 w krokach 10°, `amp` 16 i `amp2` 25
  czytane jako pół centymetra, `length2` 161 cm), liczone na piksel (wierzchołki morza są co kilka metrów), z białymi
  grzbietami i odblaskiem słońca; tekstura morza płynie (`texSpeedMin/Max`). Długości i prędkości fal dobrane na oko.
- Brzeg: tafla morza jest płaska (beach_1: 0 m) i kończy się w powietrzu tam, gdzie plaża leży niżej (piasek przy
  brzegu na -0,31 m, tam leży też piana), a na zrzutach z oryginału woda schodzi do poziomu plaży. Oryginał ma do tego
  suwaki `magnetOffset`/`magnetRadius`/`clampY*` w `WLOD_*.ini`, ale ich kod w `RCCars.exe` jest zaszyfrowany
  (zabezpieczenie Steam). U nas na ostatnim metrze przed brzegiem tafla wygina się w dół do wysokości gruntu za
  brzegiem (`CollisionMesh::shoreHeight`); dalej jest płaska. Brzeg to miejsca, gdzie woda (wszystkie grupy i LOD-y
  razem, siatka 0,25 m) graniczy z co najmniej 0,75 m bez wody, a grunt tam leży niżej niż woda — szwy między
  grupami morza i skały wystające z wody się nie liczą. Tak samo dla rysowania i fizyki. Piana leży na wodzie.
- Markery (`MARK`) mają w elemencie `0x04` właściwości wpisane w edytorze poziomów jako pary tekstów (lista
  `0x05 { 0x06 klucz, 0x07 wartość }`). Punkty kontrolne mają `width`, `height` i `expand` (np. beach_1 `cp_1`:
  szerokość 8, wysokość 1, expand 3); tam, gdzie ich brak (cały country_1, prawie cały country_2), przyjmujemy
  6 / 2 / 1. Czytamy je jako bramkę w poprzek trasy (markery nie mają obrotu, więc bramka stoi prostopadle do linii
  od poprzedniego do następnego punktu) od gruntu do `height` nad markerem (markery wiszą do 2,7 m nad ziemią),
  a `expand` jako odległość przed i za bramką, w której punkt też się zalicza. Numeracja podpunktów ma dziury
  (country_1: `cp_4_3`, `cp_4_5`) i nie zawsze idzie po kolei w pliku, więc sortujemy po numerze.
  `rcinfo race` przejeżdża wszystkie 10 tras po linii markerów i zalicza każdą bramkę.
- Kokpit: `Settings/cockpit.ini` i `cockpit_time.ini` podają pozycje na ekranie 800x600 (np. `lapPosX` 713 → x 613,
  `timeTotalPosY` → y 6, `msgOffsetY` → komunikaty 142 px nad środkiem, `timeLapBlinkInSec` 4,2 s), `arrow.ini`
  strzałkę (`CX` 50 %, `CY` 87 %, pochylona o `AngleVert` 45°). Obrazki z napisami są w `Language/<język>/*.csi`
  (poza archiwum), `msg_321_s_f.csi` to „3”, „Jazda!”/„GO!”, „2”, „1”, „meta”/„Finish” w jednej teksturze.
  Czcionka `Smash20/26.csi` to siatka 10 kolumn × 9 wierszy (25,6 × 28,4 px) ze znakami w kolejności z `Smash*.dat`;
  szerokość liter mierzymy z kanału alfa. Teksty w `Language/Game/*.tbl` są zaszyfrowane, więc napisy tabeli
  wyników są na razie wpisane w kod. Liczbę okrążeń (3, 5 lub 7) wybiera się w oryginale w menu.
- Start auta gracza to `INST 'Player'` w folderze `Players`; jego oś +Z wskazuje kierunek jazdy. Auta są w skali
  modeli RC (ok. 0,4–0,6 m długości), przód modelu to +Z.

## Dalsze kroki

1. ~~Model auta postawiony na starcie~~ (zrobione; LOD-y i ulepszenia `UPGRADES*` jeszcze niewyświetlane)
2. ~~Kolizje trasy z węzła `COLL`~~ (zrobione: siatka kolizji, raycast, nakładka debug)
3. ~~Fizyka jazdy: Bullet `btRaycastVehicle` + parametry z ustawień gry~~ (zrobione; do dostrojenia na wyczucie
   względem oryginału; kolizje kul z `cdt*.ini` nieużyte)
   - ~~nawierzchnie i woda~~ (zrobione: głęboki piasek i woda spowalniają, w głębokiej wodzie auto tonie i wraca do
     ostatniego punktu kontrolnego); brakuje kurzu i plusków spod kół oraz stref `car_reset_volume_*` („dziury”)
4. Węzły `INST` (instancje obiektów), `HHID` (widoczność/sektory), animacje (`SPLN`/`ANIM`/`ASEQ`)
5. Dźwięk: odszyfrowane WAV z `Sound/`, muzyka MP3 z `Tracks/`
6. ~~Logika wyścigu: punkty kontrolne, okrążenia, czasy, kokpit~~ (zrobione dla jednego gracza; brakuje
   prędkościomierza i wskaźnika turbo z `cockpit_sp*`, świateł startowych z `cockpit.sb`, migających strzałek nad
   punktami kontrolnymi `cp_ar_*`, menu i zapisu rekordów)
7. Przeciwnicy: linie jazdy AI z `CarProfiles/` (`Scripts/ailayouts.ini`), miejsca w wyścigu (`place1..6`), tryby

## Licencja

Kod projektu jest udostępniony na licencji MIT (zobacz [LICENSE](LICENSE)). Silnik Tempest ma własną licencję
(MIT) w `lib/Tempest`. RC Cars i jego dane należą do ich właścicieli; repozytorium nie zawiera i nie może zawierać
żadnych plików z gry.
