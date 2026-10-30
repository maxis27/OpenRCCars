# Jak działa RCCars.exe

Zachowanie oryginału odczytane z kodu `RCCars.exe` (kod jest zaszyfrowany zabezpieczeniem Steam, odszyfrowaliśmy go
tylko do analizy) i to, jak przenosimy je do OpenRCCars. Liczby w nawiasach, np. 0x4f5e50, to adresy funkcji
w `RCCars.exe`. Miejsca, gdzie robimy inaczej albo zgadujemy, są wprost tak opisane („u nas”, „zgadnięte”,
„wywnioskowane”). Formaty plików opisuje [formaty.md](formaty.md).

- [Fizyka auta](#fizyka-auta)
- [Nawierzchnie i woda pod kołami](#nawierzchnie-i-woda-pod-kołami)
- [Powrót na trasę](#powrót-na-trasę)
- [Zderzenia z innymi ciałami](#zderzenia-z-innymi-ciałami)
- [Wyścig i punkty kontrolne](#wyścig-i-punkty-kontrolne)
- [Kokpit](#kokpit)
- [Przeciwnicy](#przeciwnicy)
- [Rekwizyty](#rekwizyty)
- [Postacie](#postacie)
- [Ruch uliczny](#ruch-uliczny)
- [Woda](#woda)
- [Efekty](#efekty)
- [Wygląd auta i światło](#wygląd-auta-i-światło)
- [Słońce, detale i ruchome dekoracje](#słońce-detale-i-ruchome-dekoracje)
- [Mgła](#mgła)
- [Kamera](#kamera)
- [Dźwięk](#dźwięk)
- [Gracze](#gracze)
- [Tryb deweloperski](#tryb-deweloperski)

## Fizyka auta

Oryginał ma własny silnik fizyki: auto waży 1 kg, grawitacja to 10 m/s², więc siły są przyspieszeniami. Parametry
każdego auta są w `Settings/engine|springs|tires|steering|resistance|cdt<N>.ini`; suwak `s` (1..100) przechodzi
w `od + (s-1)·(do-od)/99`, gdzie zakres jest wpisany w kod (`physics/carparams.cpp` robi to samo). Ulepszenia
(dopalacze `BOOSTERS`, rezonatory `RESONATORS`, opony `TIRES`) są w `Scripts/upgrades.ini`. U nas ten model liczy
`physics/vehicle.*`, rekwizyty `physics/props.*` (klasa STON), a spotkania obiektów `PhysicsWorld::step` z kulami
z `physics/balls.h` (0x533430). Zewnętrznego silnika fizyki nie ma.

- **Silnik** (`engine<N>.ini`): bez dopalacza przyspieszenie = krzywa auta (`Splines/OVERKILL|BUGGY|HUMMER.bspl`:
  przyspieszenie w m/s² od prędkości w km/h, Overkill 6 → 0 przy 32 km/h, Buggy 9 → 0 przy 38, Hummer 4 → 0 przy 30)
  w punkcie `v·3,6 / (gaz · RESONATORS)`, razy `RESONATORS *_SPL_OY`; powyżej `speedBaseMax + 0,1` km/h ×
  `RESONATORS` silnik nie ciągnie. Z dopalaczem krzywa jest rozciągnięta o `speedBoostMax / speedBaseMax`, ×1,2, a
  limit to `speedBoostMax + 0,1`. Kiedy silnik ciągnie, dodaje też opór (niżej), więc na płaskim auto przyspiesza
  dokładnie wg krzywej. Siła silnika najwyżej 20 × przyczepność tyłu (w poślizgu razem z `coeffDriftRear`). Pod górę
  (napęd ponad 10° od poziomu), gdy sprężyny spychają auto w dół zbocza, każde tylne koło dodaje połowę krzywej
  `Splines/<auto>_RESTRICT.bspl` w punkcie `v·3,6 / (gaz · RESONATORS)`, najwyżej tyle, ile spychają (0x4ee5e0):
  Overkill +2 m/s² do 15 km/h, Hummer +6 do 20 km/h, Buggy −4 do 25 km/h (pod górę słabszy). Wsteczny: 4 m/s²
  (z dopalaczem 6) do 2,83 m/s, dalej malejąco do zera przy 3,33 m/s (12 km/h).
- **Napęd** (0x4eea50): siła silnika po połowie na tylne koła stojące na podłożu nachylonym mniej niż 46°, wzdłuż
  podłoża. Klawisz hamulca hamuje i przy |v| ≤ 2 m/s (cała prędkość, też w bok) wrzuca wsteczny, gaz tak samo w
  drugą stronę (0x4ee5e0); hamując silnik nie ciągnie. Biegi, hamulec i siłę silnika gra liczy od nowa w każdym
  kroku Eulera, gdy choć jedno koło dotyka ziemi.
- **Opór** (`resistance<N>.ini`): `a = coeffAirResistance·v² + coeffFrictionBearings` przeciw ruchowi, zakresy
  0..0,1 i 0..10 (Overkill 0,032 i 0,51). Działa od 0,0001 m/s, bez wygładzania (0x4ed970).
- **Turbo**: zbiornik w jednostkach, pełny ma od 50 do 100 jednostek, zależnie od tempa ładowania (`boostSpeedUp`
  0,1..1,5 × `BOOSTERS`) na tle najwolniejszego (Buggy bez ulepszeń) i najszybszego (Hummer z pełnymi). Na starcie
  jest pełny, ładuje się sam (`boostSpeedUp · BOOSTERS · 0,95` jedn./s), za każdy punkt kontrolny +10, za każde
  „Łup!” i „Wielkie Łup!” +5, z wciśniętym dopalaczem schodzi `boostSpeedDown` (1..10, 5,45 jedn./s). Gdy się
  opróżni przy wciśniętym klawiszu, dopalacz rusza znowu po 0,9 s i dopiero powyżej 10 jednostek. `BOOSTERS` nie
  zmienia przyspieszenia, tylko ładowanie turbo.
- **Skręt**: pozycja kierownicy rośnie w stronę wciśniętą o `4,5 - 0,5·v` (v w m/s, najmniej 1) na sekundę, wraca
  (i przechodzi na drugą stronę) o 6 na sekundę; `steering<N>.bspl` zamienia ją na kąt kół (20° na jednostkę),
  który rośnie do `AngleSteer` (0..100, 29,3°). Pełny skręt nie maleje z prędkością, tylko wolniej się go nabiera.
- **Skok** (spacja, co najmniej 0,5 s od poprzedniego): +3 m/s wzdłuż osi „w górę” auta (Buggy 3,3) i obrót wokół
  pionu ×1,5; auto przechylone o ponad 80° zamiast skoczyć staje na koła. Auto przewrócone na dach wraca na koła
  po 2 s.
- **Bryła** (0x4f0910, 0x4f5e50): bryła sztywna o masie 1 kg, bezwładność prostopadłościanu całego modelu (Buggy
  ×2 wokół osi przód-tył), środek masy `CenterMassOY` (0..0,2 m) nad początkiem modelu; prędkość najwyżej 20 m/s,
  moment pędu najwyżej 1. Nadwozie to 9 kul z `cdt<N>.ini`: `CdtRadUp<i>` (0,01..0,3 m) w `CdtDeltaX|Y|Z<i>`
  (−0,3..0,3 m, Buggy `CdtDeltaZ_CENTRAL<i>`), każda trzy razy: na środku i przesunięta o `shiftRoofBaseLefter`
  w lewo i w prawo. Koło to kula `CdtRadWheel` (0,01..0,1 m, tylne × `coeffRadBackWheels` 1..2).
- **Sprężyny** (`springs<N>.ini`, 0x4f91b0, 0x4f0f80): `length` 0,01..0,5 m, `lenMin` × 0,1..0,9, `lenMax`
  × 1,1..2, ugięcie `kPos` = (length − lenMin) × 0,1..0,9, tłumienie `kSpeed` 1..10, mocowanie `touchPoint`
  (0..1 m) nad piastą. Sztywność k = m·g / (liczba kół · ugięcie). Koło dotykające ziemi pcha auto wzdłuż jego osi
  „w górę” siłą k·(length − długość − 0,02 r) − kSpeed·(zmiana długości)/dt; tłumienie może przeważyć sprężynę
  i ściągać auto w dół (gra ogranicza je do 2·10⁶ × siła sprężyny, czyli praktycznie wcale), a gdy któraś sprężyna
  nie mieści podłoża, zmiana długości liczy się jako zero. Do ruchu liczy się tylko część wzdłuż normalnej podłoża,
  a moment w osiach auta razy `coeffMomentOX` (0,5..2, pochylenie) i `coeffMomentOZ` (1..3, przechył), bez obrotu
  wokół pionu.
- **Koła na sprężynach**: koło to kula o promieniu r wisząca `długość` pod górą sprężyny wzdłuż osi „w górę” auta
  (0x4ef680). Po ruchu nadwozia, koło po kole (0x4fb340): kula w ziemi idzie w górę (0x4fb9e0) do 5 razy o tyle,
  ile trzeba, żeby dotykany punkt trasy wyszedł z niej wzdłuż osi auta, plus 0,01 r, najwyżej do `lenMin` i łącznie
  o 0,95 r; gdy nad którymś kołem nie ma miejsca (trasa w kuli 1,5 r nad jego środkiem, 0x4fbd70), idzie zamiast
  tego ku wolnemu spoczynkowi o 0,95 r na sekundę. Wolna kula zmierza do spoczynku (`length`, a gdy auto jedzie
  wolniej niż 5 km/h i w tej części kroku uderzyło w trasę albo nad kołem nie ma miejsca, `length − 0,95·kPos`):
  w dół o r na sekundę, r·(1 + 18·t) po t s od ostatniego wolnego spoczynku, 10·r od 0,5 s i przy ustawianiu auta,
  najwyżej 0,99 r na krok, po czym znowu wychodzi z ziemi, ale nie wyżej, niż była (0x4fbc50); w górę o tyle samo,
  o ile nie wejdzie przez to w ziemię. Koło, którego środek przeszedłby przez trasę, zostaje. Koło, któremu nic nie
  pomoże, kończy przebieg: reszta zostaje, a krok liczy się jako uderzenie (0x4f5e50). Kół dotykających ziemi
  (w 6 mm od trasy) i ich punktów szuka się na początku każdej części kroku (0x4f0230); po części z uderzeniem albo
  z kołem bez miejsca nad sobą sprężyny w następnej nie pchają (0x48 danych auta). U nas kula „w ziemi” to o ponad
  2 mm, inaczej koło podnoszone wzdłuż osi auta ze zbocza tylko by go dotykało.
- **Opony** (`tires<N>.ini`, 0x4f7fa0, 0x4edac0, 0x4ee280): przyczepność `CoeffFrontTires|RearTires` 0,1..1
  × ulepszenie `TIRES`. Działają najwyżej w dwóch kołach, przednim i tylnym: przy mniej niż 3 kołach na ziemi
  w pierwszych dwóch dotykających, inaczej w zewnętrznych w zakręcie (jadąc prosto w prawych). Każde zatrzymuje
  poślizg w bok (hamując: każdy poślizg) siłą, która razem z drugą znosi przyspieszenie punktu koła od reszty sił
  wzdłuż obracającego się z autem kierunku, razem z odśrodkowym i z prędkością punktu wzdłuż obrotu tego kierunku
  (0x476c50, 0x476ba0; bez tego auto ślizgało się w każdym zakręcie), najwyżej 5 × przyczepność z przodu, 10 ×
  z tyłu, i jego prędkość w tym kierunku (najwyżej 10 × przyczepność na sekundę). Hamulec ze skrętem powyżej
  `angleDriftOn` (1..45°) to poślizg: tył ma przyczepność × `coeffDriftRear` (1..0,1) i oba koła limit 10. Z kołami
  prosto opony gaszą też obrót auta (1/s).
- **Obrót kół** (0x4ef280, raz na klatkę po krokach): koło na ziemi toczy się z `v/r` (v: prędkość jego punktu
  wzdłuż kierunku koła), w powietrzu 0; obrót goni to tempem 200 rad/s² na ziemi i 5 rad/s² w powietrzu. Gaz albo
  wsteczny (powyżej 0,3) dokłada do `SpeedAngMaxREL` (`tires<N>.ini`, 0..20 rad/s; 10,7/10,3/10,1) tempem
  70 rad/s², na ziemi tylko poniżej 4 m/s (całość w miejscu, nic przy 4 m/s), bez klawisza dodatek wraca do 0
  tempem 35; hamowanie zeruje jedno i drugie. Poślizg koła (0x5019a0) to |obrót − toczenie| / 20, najwyżej 1;
  z niego kurz i dym buksowania oraz dźwięk `car_motor_ws`: tylne koło z poślizgiem ponad 0,45 przez 0,3 s, poniżej
  5 km/h, bez hamowania, z gazem albo wstecznym (0x50b238). Koła rysujemy obrócone o ten obrót.
- **Kroki** (0x4f6ea0): klatka krótsza niż 0,02 s to jeden krok, dłuższa to kroki po 0,02 s, dopóki zostaje więcej
  niż 0,038 s, a reszta w dwóch połowach (przy 60 kl./s krok 1/60 s). Siły (grawitacja, opór, sprężyny, napęd,
  opony; 0x4f0910) liczą się od nowa przy każdym kroku Eulera, także w próbach szukania zderzenia. U nas każdy krok
  dzieli się jeszcze na równe części najwyżej po 1/120 s: przy krokach długości klatki nasze sprężyny co chwilę
  tracą i łapią podłoże, więc stojące auto drży bez końca (np. na deskach pomostu).
- **Start** (0x4f27a0): auto postawione na trasie najpierw osiada przez 2 s (100 kroków po 0,02 s) tylko pod
  grawitacją i sprężynami, które wtedy opadają z szybkością 10·r; gra zeruje przy tym poziomą część siły i obrót
  wokół pionu (0x4f0910), więc auto osiada w miejscu i nie zsuwa się ani nie obraca.
- **Spoczynek** (0x4f59a0, 0x4f6610): gdy przez 2 s nie jest wciśnięty gaz, hamulec, skręt ani turbo, auto jedzie
  wolniej niż 1,3 km/h i obraca się wolniej niż 1 rad/s, a od 1 s cały czas dotyka trasy (koło do 6 mm od niej albo
  zderzenie), gra zeruje mu pęd i moment pędu i pomija krok, póki jakaś kula jest do 3 cm od trasy. Które bity
  klawiszy gra sprawdza, odczytaliśmy tylko po turbo (0x577c); reszta to nasze założenie.
- **Ruch i trasa** (0x4f5e50): krok dzieli się na najwyżej 3 części. Każda najpierw przycina pęd do 20 i moment pędu
  do 1 (0x4f5770), potem przesuwa auto krokiem Eulera (0x4f5590) o tyle, ile kroku zostało, a gdy po drodze kula
  przeszłaby przez ścianę, tylko o tyle, by żaden punkt kuli nie przeszedł więcej niż 0,9 promienia, połowiąc, aż
  żaden nie przejdzie więcej niż 0,99 (0x4f5b60). Jeśli potem kula nadwozia jest w ścianie (albo kula koła, którego
  sprężyna nie mieści podłoża, 0x4fb340), auto wraca i w 5 próbach szuka chwili zderzenia: najpierw 0,2 drogi od
  wolnego do zajętego, potem w połowie, aż kula jest 6 mm od ściany (0x4f0270). Wtedy pierwsza dotykająca kula
  nadwozia traci poślizg wzdłuż ściany, najwyżej 10 m/s × przebyty czas (0x4f0560), a każdy styk dostaje impuls,
  po którym odjeżdża od ściany z prędkością 0,05 m/s, bez odbicia (0x4f0750, do 10 przejść). Reszta kroku
  przepada; auto, które zrobiło mniej niż 5% kroku, podnosi się o 2ⁿ·3 mm (albo, gdy to w ścianie, schodzi
  o 15 mm), a po 5 takich krokach z rzędu wraca na ostatni punkt kontrolny (0x508b30). Nasze dodatki: kula już w
  ścianie na początku części kroku (koło, które sprężyna opuściła przy ścianie) nie zatrzymuje ruchu, tylko jest
  wypychana, a kula koła może wejść w ścianę na 2 mm (stoi na niej). Ściany to trójkąty siatki `COLL` (ściany,
  słupy, niewidoczne bariery).
- **Rysowanie kół** (0x504820 Overkill, 0x505780 Buggy, 0x5068e0 Hummer; u nas `physics/carrig.*`): gra nigdy nie
  przesuwa koła o długość sprężyny, tylko obraca węzły, na których koła wiszą, więc koła zostają na osiach. Sztywna
  oś (`FRONT_AXLE`, `REAR_AXLE`) wychyla się o atan(opad kół / `lenAxe`) (0,05..0,2 m), najwyżej 30° w dół i 90°
  w górę, i przechyla o atan(4 · różnica wysokości lewego i prawego koła). Hummer czyta kąty z tablic wysokości koła
  od kąta, zbudowanych przy wczytaniu auta (0x5065c0), czyli osie obracają się dokładnie tak, by koła były tam, gdzie
  sprężyny (najwyżej 20° w dół); środkowa oś przesuwa się w pionie najwyżej o tyle, o ile opada przednie koło przy
  20° (0x14edd4c). Wahacze Buggy (`AXLE_*_UP`, `AXLE_*_DOWN`) obracają się tak, by zwrotnica z kołem
  (`AXLE_*_WHEEL`) była na wysokości koła, najwyżej `angleProcUpMax` w górę i `angleProcDownMax` w dół (0..90°,
  35,5° i 26,4°). Przednie koła skręcają z wspornikami (`AXLE_FRONT_*_SUPPORT`), a amortyzatory między nadwoziem a
  osią lub wahaczem obracają się i rozciągają (Overkill: sięgają `SpringLength` z `WheelsVisual.crs`, 0,01..0,1 m,
  77 → 0,079 m, wzdłuż osi Z węzła).

## Nawierzchnie i woda pod kołami

Rodzaj nawierzchni pochodzi z bitów materiału tekstury (`GLTX`, zob. [formaty.md](formaty.md#tekstury-węzła-gltx)):
piasek, mokry piasek, głęboki piasek (`Sand_trample`), żwir i reszta. Jak w danych oryginału, jazdę zmieniają tylko
głęboki piasek i woda; pozostałe nawierzchnie różnią się w oryginale tylko kurzem i śladami spod kół.

- **Głęboki piasek i woda** (`tires<N>.ini`, `coeffDeepSand`, `coeffWater`, 0..1; 0x4eeea0): każde koło stojące na
  ziemi (v to prędkość auta) w wodzie dokłada `coeffWater·v²` przeciw ruchowi punktu, którym jej dotyka, od zera
  przy kole zanurzonym w 30% do całości przy 90%, a poza wodą w głębokim piasku `coeffDeepSand·v²`; koło w powietrzu
  nie hamuje. Liczy się to razem z resztą sił przy każdym kroku Eulera. Hummer na dużych kołach mniej traci w wodzie.
- **Woda** nie jest twarda: auto brodzi w płytkiej wodzie i zwalnia, w głębokiej tonie. Wodę hamuje tylko opór kół;
  `Settings/Water.ini` gra pomija, więc my też. Wodę pod autem gra szuka promieniem tylko w morzach i w obiektach
  `stream`/`pool` (0x51c240, 0x535120): wodospady nie są dla auta wodą. U nas tak samo (`CollisionMesh::isWater`).
  Fizyka liczy wodę na wysokości `y0 + offset` morza, bez fal.

## Powrót na trasę

Auto wraca na ostatni zaliczony główny punkt kontrolny (przed pierwszym: na start), stawiane na jego markerze
przodem w stronę następnego markera trasy:

- po klawiszu **R** (co najmniej 1 s od poprzedniego);
- po **utonięciu** (0x508a10): liczy się punkt 0,2 m nad początkiem układu auta (u nas ~0,22 m nad spodem kół, przy
  dachu na 0,29 m); auto tonie po 2 s z tym punktem pod wodą, po 0,5 s, gdy jest głębiej niż 0,5 m pod
  powierzchnią; wynurzenie zeruje licznik;
- po wyjechaniu **poza pudło kolizji trasy** (w bok albo 0,1 m pod jego dno, 0x508160);
- po chwili w **strefie `car_reset_volume_*`** (beach_2, beach_3, country_2, country_4; czas `FilterTime` ze skryptu
  strefy, od 10 ms do 3 s; 0x509570, 0x509690). W strefie kokpit pokazuje „Słaby sygnał” (`msg_low_signal`), a auto
  traci sterowanie i hamuje (0x4f6c87);
- po 5 krokach fizyki z rzędu bez ruchu (zakleszczenie, 0x508b30);
- 2 s po trafieniu z broni ([niżej](#efekty)).

Przeciwników obowiązują te same zasady (utonięcie, pudło kolizji, strefy), a wypchnięty z nagrania wraca od razu po
wjechaniu w dowolną strefę (0x4fe3f0). Trafia na ostatni minięty główny punkt kontrolny (przed pierwszym na swoje
miejsce na starcie) i stamtąd autopilot prowadzi go z powrotem na nagranie (0x508b50).

## Zderzenia z innymi ciałami

Zderzenia aut z innymi ciałami liczy własny moduł gry (0x534d00, raz na klatkę: pary klas i ich funkcje).

- **Auto z autem** (0x533990): stykające się auta odbijają się sprężyście wzdłuż linii środków mas; gdy jednym
  z nich jest gracz, a oba ostatnio zderzyły się z autem ponad 2 s temu, wolniejsze auto (przy podobnej prędkości
  przeciwnik) ma środek liczony 10 cm wyżej, więc dostaje impuls lekko w górę.
- **Auto z rekwizytem**: odbijają się sprężyście wzdłuż linii środków mas, przez jej środek, co najmniej z prędkością
  0,2 m/s (0x533ee0, 0x4f0750 z drugim ciałem: impuls na podwójną prędkość zbliżania). Koła nie stają na rekwizytach
  (oryginał pyta o podłoże tylko trasę, 0x4fbd70), lecz w nie uderzają. Środek masy rekwizytu jest przy tym
  przesunięty o losowy 1 cm (u gracza; u nas przy każdym aucie), a auto na rekwizycie zawieszonym na nagraniu
  przeciwnika strąca go z nagrania (0x4fd9d0).
- **Spotkania obiektów** (0x533430, raz na klatkę po fizyce): obiekty w promieniu 10 m od widzów (auta graczy,
  0x487300), pary z tablicy 0x534d00, tylko gdy ich początki są bliżej niż 2,5 m; pierwsza para stykających się kul
  (0x533740) woła obsługę pary z punktem w połowie środków i normalną od B do A. Kule: auto 0x4ef9e0 (koła, potem
  nadwozie), rekwizyt 0x50dc50, człowiek 0x511e30 (3 × 0,2 m na 0/0,2/0,4 m), strażnik 0x51a350 (2 × 0,2 m na
  0/0,2 m), pies 0x518820 (5 na kościach; u nas jedna), auto z ulicy 0x516480 (tylko kule kół, bez pudła).
  Rekwizyt z rekwizytem (0x534240) odbijają się jak auto z rekwizytem; auto z ulicy pcha rekwizyt impulsem
  normalna × masa w jego środku (0x5348d0).
- **Rekwizyty** (klasa STON, 0x50c700..0x50f420): bryła sztywna z kul. Kształt z SCRI (`Geom type` 1..8: kula,
  walec, pachołek, butelka, wiadro, muszla, czajnik, balon; brak lub inny → kula), kule z `Settings\CYLINDER.ini`,
  `BOTTLE.ini`, `BUCKET.ini`, `SHELL.ini`, `kettle.ini`, `BALLON.ini` (pachołek wpisany w kod 0x50e190, kula ma
  promień 0,9 × przekątna pudła / 2√3). Domyślne środek masy, masa, tarcie, odbicie 0x50d0d0 (kula 0, 0,5, 1, 0,7;
  walec i balon 0, 0,1, 1, 0,3; pachołek 0,03, 1, 1, 0,3; butelka, muszla, czajnik 0,02, 0,4, 1, 0,3; wiadro 0,1,
  0,3, 1, 0,3), SCRI je nadpisuje; bezwładność 0x50cd10. Stawiany promieniem w dół od każdej kuli (0x50cea0), śpi od
  startu, jeśli stoi prosto (do 10°). Rusza się tylko, gdy auto gracza jest w promieniu 50 m, nie więcej niż 20 m
  nad nim i patrzy w jego stronę (0x50ec10); zasypia po 2 s spokoju (0x50efa0). Krok 0x50f190: do 4 części,
  `MAX_IMPULSE.ini` ogranicza pęd, metoda punktu środkowego (0x50dba0), grawitacja, opór, hamowanie obrotu przy
  ziemi (0x50d670), styk z trasą z tarciem Mirticha (0x475c80). Wiatr (`fWindBlow`, 0x50d3f0) działa tylko w grze
  sieciowej (0x4a0b00), więc u nas nigdy.
- **Auto z człowiekiem, strażnikiem i psem**: odpychają samo auto od punktu styku do 0,05 m/s (0x534340, 0x5344f0,
  0x5349d0). Ciało człowieka i strażnika to trzy kule po 0,2 m jedna nad drugą (0x511e30), a auto zatrzymuje się na
  nich jak na przeszkodzie (0x4f0730, impuls bez odbicia). Potrącenie liczy prędkość auta sprzed zderzenia. Pies
  dodatkowo odpycha auto przy każdym dotknięciu o 0,5 m/s w bok i 0,12 m/s w górę (0x4f3470).
- **Auto z ruchem ulicznym**: samochód tylko pcha (0x534880), zob. [ruch uliczny](#ruch-uliczny).
- Każde z tych zderzeń auta przeciwnika (z innym autem, człowiekiem, psem, krabem, samochodem) wytrąca je z nagrania
  (0x4fd9d0).
- **„Łup!” i „Wielkie Łup!”** (`msg_hits.csi`, przez 1 s, ważniejsze od „złego kierunku”; każde daje +5 jednostek
  turbo). „Łup!” za dotknięcie rekwizytu ze `stone.sb` szybciej niż 7 km/h (najwyżej co 0,3 s). Po uderzeniu
  w przeciwnika (oba auta co najmniej 2 s od poprzedniego) gra przez 0,5 s sumuje obrót **przeciwnika** w jego osiach
  (0x5334b0): średnio ponad 3 rad/s wokół X, 5 wokół Z albo 7 w sumie to „Wielkie Łup!”, ponad 0,8 / 1 / 2 to
  „Łup!”, czyli liczy się, jak mocno gracz nim rzucił. Ludzie, strażnik (od 10 km/h) i pies (od 15 km/h) dają
  „Wielkie Łup!” (najwyżej co 2 s); kraby, pająki, mewy i ruch uliczny nic.

## Wyścig i punkty kontrolne

- **Odliczanie**: skrypt `RaceEntry` z `RCCars.sb` (sekunda bez niczego, potem „3”, „2”, „1”, „Jazda!”), `RaceAgain`
  bez tej sekundy; auto stoi do startu. Polecenie `Show3` (0x4e2060 → 0x52a6a0) gra dźwięk 2 kanału `Checkpoint`,
  czyli `cp_start.wav`, i kanał `Prestart` (`prestart.wav`, 9 s) naraz. Światła startowe z `cockpit.sb`
  (`lightsframe`, `light1..12`) oryginał pomija: wczytuje ten plik jako model `Arrow` tylko dla strzałki `extru9`,
  a tekstur świateł nie ma w archiwum.
- **Okrążenia**: 3, 5 albo 7 (wybór w menu). Liczą się główne punkty kontrolne `cp_N` (na beach_1 cztery, każdy
  z kolorowym graffiti na ziemi): okrążenie to przejechanie ich wszystkich po kolei, jak w instrukcji. Czas całkowity,
  czas okrążenia, najlepsze okrążenie, meta z tabelą czasów i „jeszcze raz”.
- **Zaliczanie**: punkt `cp_N` zalicza się, gdy auto gracza minie jego marker bliżej niż 1,6 m, liczone w 3D (auta
  AI: 2,88 m), i sprawdzany jest tylko następny w kolejności. Gra bierze podpunkty `cp_N_1`, `cp_N_2` … do pierwszego
  brakującego numeru (country_1 pomija więc `cp_4_5`) i najwyżej 8 głównych punktów. Tak jak w oryginale na
  country_3 da się zaliczyć `cp_3` wcześnie: stoi 2,2 m od drogi przy wjeździe w pętlę, którą trasa objeżdża dwa razy.
- **Korytarz trasy** (0x4e9560, 0x4e9980): `width`/`height`/`expand` z markerów opisują korytarz od markera do
  następnego: prostopadłościan wzdłuż odcinka, `expand` dłuższy z obu końców, `width` w obie strony i `height` w górę
  i w dół (oś „w górę” to pion prostopadły do odcinka). Opisują korytarz, a nie zaliczanie punktu. Auto szuka się
  najpierw w odcinkach od ostatniego głównego punktu do następnego (od końca), potem w pozostałych od `cp_1`; poza
  korytarzem zostaje na ostatnim odcinku, na którym było (0x4eba30, 0x4eb670).
- **Dalsza droga** (0x4e8f00): kierunek odcinka korytarza, na którym jest auto (następnego po 90% długości; za
  ostatnim przed punktem prosto na punkt); w innym odcinku za następnym głównym punktem (bliżej w tył niż w przód po
  okrążeniu) kierunek odwrotny, a poza korytarzem prosto na następny główny punkt.
- **„Zły kierunek”**: auto zwrócone przodem przeciw dalszej drodze nabija licznik (1/s, najwyżej 12), zwrócone
  zgodnie zbija go 3/s; ponad 6 s to „zły kierunek”, chyba że ostatni zaliczony główny punkt jest bliżej niż 8 m
  (poziomo).
- `rcinfo race` przejeżdża wszystkie 10 tras po linii markerów (i drugi raz obok punktów pośrednich) i zalicza każde
  okrążenie, a `rcinfo gates` dojeżdża autem z fizyką do każdego z 50 głównych punktów (rusza kilka metrów przed nim)
  i wszystkie się zaliczają.

## Kokpit

Kokpit rysujemy obrazkami z gry (`Language/Polish` albo `Language/English`) i czcionką `Smash26` w miejscach
z `Settings/cockpit.ini`.

- **Pozycje**: `Settings/cockpit.ini` i `cockpit_time.ini` podają pozycje na ekranie 800x600 (np. `lapPosX` 713 → x
  613, `timeTotalPosY` → y 6, `msgOffsetY` → komunikaty 142 px nad środkiem, `timeLapBlinkInSec` 4,2 s). Czas i numer
  okrążenia są metaliczne (`cock_num`), czas okrążenia miga pod czasem tylko po jego ukończeniu. W lewym górnym rogu
  miejsce (`place1..6`) w wyścigu z przeciwnikami. U nas lewe elementy kokpitu trzymają się lewej krawędzi szerokiego
  ekranu.
- **Zegary**: na dole `cockpit_sp1`/`cockpit_sp2`/`cockpit_sp4`, dopalacz po lewej, prędkość po prawej (pełny przy
  50 km/h). `indicatorAngMin`/`indicatorAngMax` z `cockpit.ini` (−34° i 127° od góry, zgodnie z ruchem wskazówek) to
  dokładnie początek i koniec żółtej skali `cockpit_sp1` i czerwonej `cockpit_sp2` wokół środka tarczy (130; 126,6
  z 256). Ze zrzutu oryginału: rurka wypełnia się na czerwono od dolnego końca (zero) w górę, nad wartością jest żółta
  do tego, co auto może osiągnąć (zbiornik dopalacza, prędkość maksymalna z dopalaczem; tam ciemna zatyczka
  z `cockpit_sp4`), dalej szara (szara rurka z `cockpit_sp2` obrócona o 184°); na wartości srebrna obejma
  z `cockpit_sp4`. Rozmiar i miejsce zegarów zmierzone z obrazka kokpitu w opcjach (`opt_cock_all`;
  `opt_cock_[aclm]_*` pokazują części kokpitu: miejsce i tabela, mapa, czat i turbo).
- **Mapa**: pod numerem okrążenia mapa trasy (`trackmap_<n>`, zob. [formaty.md](formaty.md#nazwy-tras)) wokół auta
  w ramce `messagebox`, z punktami głównymi (`map_cp`, następny świeci) i autami (`map_arrow`: gracz i przeciwnicy).
- **Strzałka kierunku** (0x4af3c9, 0x4b0fa0, 0x4b1360, 0x4e8f00), w nawiasach z `overtaking.csi`: model `Arrow`
  z `cockpit.sb` to dwa szewrony (`extru9` z bokami `extru9_2_body` i 0,324 m za nim `extru9_3` z `extru9_1_body`),
  wszystko z teksturą `coc_str_2`, rysowane bez światła i bez przezroczystości (boki biorą srebrny pasek z dolnej,
  przezroczystej części tekstury). Gra szuka `extru9` i `extru9_3` i nadaje im kolor ARGB z v = 512·k: czerwony
  min(512−v, 255), zielony min(v, 235), bez niebieskiego, czyli k = 1 zielony, 0,5 żółty, 0 czerwony. k dąży (1 na
  sekundę) do drogi, jaka została po korytarzu do następnego głównego punktu, podzielonej przez drogę od poprzedniego
  do niego; w złą stronę do 0. Kamera z `Settings/arrow.ini` (zastępuje `Arrow_cam` z `RCCars.sb`): `NearPlane`
  10 mm, pole widzenia `AngleVert` 45° w poziomie i w pionie, kwadrat `Len` 17 % szerokości i wysokości ekranu ze
  środkiem w `CX` 50 %, `CY` 87 %, kamera w (0; (`pos_y`−200)/100; (`pos_z`−200)/100) = (0; 1,12; 1) patrzy na
  (0; cos 39°; sin 39°), gdzie 39° to `dir` − 90. Model obraca się wokół osi pionowej w stronę
  [dalszej drogi](#wyścig-i-punkty-kontrolne) najwyżej o 90° na sekundę (10 razy szybciej, kolor też, zaraz po
  zaliczeniu punktu, dopóki nie wskaże nowego kierunku z dokładnością do 1°). Miga (0,25 s widać, 0,25 s nie) przy
  jeździe w złą stronę i gdy auto było już bliżej niż 5 m od następnego punktu, ale go nie zaliczyło.
- **Strzałka nad punktem**: nad następnym głównym punktem wisi animowana strzałka z gry (`cp_ar_2_f1..3` zielona,
  a nad `cp_1`, gdzie kończy się okrążenie, czerwona `cp_ar_3_*`). `anim_cp.ini` (`Size` 146, `Shift` 128,
  `MinDist` 237, `MaxDist` 900, `BlinkDelta` 10): `Shift` to texele obrazka wysokiego na 256, więc jest podniesiony
  o połowę wysokości i cień narysowany u jego dołu leży na ziemi pod markerem; resztę czytamy jako centymetry
  i setne sekundy: strzałka ma 1,46 m, znika bliżej niż 2,37 m od kamery, dalej niż 9 m trzyma rozmiar na ekranie
  (rośnie w górę od ziemi), klatka co 0,1 s (zgadnięte; kod gry, który to czyta, nie jest rozpracowany).
- **Komunikaty**: odliczanie, „okrążenie”, „zły kierunek”, „najlepsze okrążenie”, „Łup!”, „Słaby sygnał” obrazkami
  z `Language/<język>`. Tabela wyników to osobny ekran mety (`dlgFINISH`, niżej).
- **Konsola czatu** (pole `chatRace` z `dlgMAIN.ini`: 5 linii po 5 s, czcionka `Smash20` z polskimi literami;
  linie co 28, wysokość czcionki, od góry pola, pierwsza na y 260, najnowsza najniżej; 0x4adb20, 0x4ae600)
  pokazuje komunikaty z `Language/Game/<język>.tbl`: okrążenia i meta przeciwników, utonięcia, strefy resetu,
  trafienia z broni, kopnięcia i rzuty przez ludzi, pchanie aut z ulicy; przeciwnicy dostają je też z
  `Settings/AIEmu.ini`, gdy przejeżdżają obok ludzi, psów i aut (szansa `ProbCheck`).

## Przeciwnicy

- **Skład** (`Scripts/ailayouts.ini`): do pięciu kierowców, auto i nagranie na trasę; na łatwym startują trzej, na
  normalnym czterej, na trudnym wszyscy (domyślnie normalny). Kierowcy (Doc, BabyShark, Daisy, Dakilla, Johny,
  MCJocker, MaxXMad) mają swoje malowanie auta `car_askin<auto><1–4>` i ulepszenia `AI<n>Boost/Reson/Tires`. Gracz
  staje na swoim miejscu w szyku (`PlayerPath`, `start.dat`).
- **Nagrania**: dopóki nikt ich nie potrąci, nie jeżdżą z fizyką, tylko odtwarzają przejazdy z
  `CarProfiles/<trasa>/*.dat`. Nagranie to jedno okrążenie od startu, którego koniec schodzi się z początkiem pętli;
  kolejne okrążenia kierowca jedzie z odmian tego nagrania (`over1`, `over1_2`, `over1_3` …) po kolei. Okrążenia
  przeciwników liczymy po przejechaniu `cp_1` (do 2,88 m) pod koniec nagrania, a nie po kolejnych punktach, bo
  odmiany `_3` bywają skrótami. Odtwarzając nagranie, są bryłą kinematyczną (pchają rekwizyty). Które z odmian
  nagrania i w jakiej kolejności bierze oryginał, nie wiemy; malowanie `Skin` 1–3 czytamy jako drugą do czwartej
  tekstury `car_askin<auto>N` (pierwszą ma model i gracz), to też zgadnięte.
- **Tempo** (0x503430): czas nagrania rośnie o `dt` × współczynnik: `CoeffCommonOpponents` trasy (0,85–0,97) ×
  poziom trudności (`Scripts/aiCommonCoeffs.ini`: 0,92 / 1 / 1,07) × „gumka”, czyli wartość krzywej
  `Splines/FuncWaitAccel<n>` w odległości przeciwnika przed graczem wzdłuż trasy (0x4fd4b0, 0x4fd5d0; szybki wyścig
  bierze krzywe `_A`, 0x4fd6e2): kto jest 40–50 m za graczem, jedzie do 1,1–1,2 raza szybciej, kto 20–45 m przed nim,
  zwalnia do 0,6–0,65. `Settings/OpponentBehav.ini` (`distEquil`, `coeffWait*`, `coeffAccel*`) i
  `Scripts/aiShiftsFuncWaitAccel.ini` należą do innych trybów i jeszcze ich nie używamy.
- **Wypadnięcie z nagrania** (0x533990 → 0x4fd9d0): gdy dotknie ich inne auto (albo potrącą człowieka, psa,
  rozjadą kraba, uderzy ich samochód lub kula), stają się autem z fizyką jak gracz (parametry swojego auta, ta sama
  masa 1 kg, więc się przepychają, obracają i przewracają).
- **Autopilot** (0x4fdb50–0x4fe490) prowadzi je z powrotem na nagranie: celuje w punkt nagrania 2,7 m przed
  najbliższym (szukanym do 6 m naprzód), prędkość 4–10 m/s zależnie od odległości do celu, mniejsza przy ostrym
  skręcie, koła do 35° (przez pierwszą sekundę co najmniej 30°, z prędkością 90°/s); zablokowane (wolniej niż 2 km/h
  albo dociśnięte do przeszkody) cofa przez 1,5 s. Po 2 s wraca do odtwarzania, gdy jest 10 cm od punktu nagrania
  i jedzie w jego kierunku (do 10°), a po 5 s wraca od razu, jeśli gracz go nie widzi (u nas: dalej niż 50 m albo za
  graczem, zgadnięte). Powrót płynnie przechodzi w nagranie przez sekundę.
- Kokpit pokazuje miejsce obrazkiem z gry (`place1..6.csi`: „1szy” … „6ty”), a ekran mety tabelę, w którą wpadają
  czasy kolejnych kierowców.
- **Imiona i twarze** (0x4a9ce0): kierowca `AIPlayer<n>` z `ailayouts.ini` ma imię z `Language/Game` 40499+n
  („MC Zgryw”, „Maks Mad”, „Doktorek”, „Rekinek”, „Różyczka”, „Dakilla”, „Jasiu”; tak też w czacie) i twarz
  `Face` szukaną po nazwie wśród `FacesSys\` i `Faces\` (0x4e5a10), bez niej `face1.tga`.
- **Zegar**: czas wyścigu gracza staje na mecie, ale przeciwnicy liczą dalej (rekord wyników 0x4ea2d0, +0x14 rośnie
  do ich mety); wcześniej dojeżdżający po graczu dostawali jego czas.

## Rekwizyty

- Rekwizyty ze `stone.sb` (puszki, butelki, piłki, wiadra, pachołki, czajniki, baloniki, łuski, toczące się kłębki)
  stoją tam, gdzie kładzie je trasa (od 8 do 17 na trasie), i są bryłami sztywnymi: auto je potrąca, przewraca i
  odbija jak oryginał. Masa, sprężystość, tarcie, kształt (kula, walec albo otoczka wypukła modelu) i środek masy
  pochodzą ze skryptu `SCRI` każdego modelu.
- Rekwizyty stoją w edytorze zwykle 0,1–0,5 m nad ziemią (kilka do 1,4 m): po wczytaniu trasy opadają na nią przez
  2 s, zanim ruszy wyścig.
- **Wiatr** (0x50d3f0): kłębki (`fWindBlow`) pcha 1 m/s² w stronę, która powoli obraca się według szumu 1D, i z
  powrotem w stronę miejsca startu, gdy odtoczą się dalej niż 3 m.
- **Dźwięk**: potrącony rekwizyt (przez auto, inny rekwizyt albo auto z ulicy) gra dźwięk z kanału w swoim modelu
  w `stone.sb` (`ball`, `pepsi`, `klubok` …; 0x50cb00).

## Postacie

Każdy rodzaj to osobna klasa gry (`PEOP` 0x50fda0, `GUAM` 0x51a3d0, `GULL` 0x512360, `CRAB` 0x513880, `SPDR`
0x514d40, `DOG$` 0x5188f0, `VULT` 0x51af80) z ustawieniami w `Settings/People.ini`, `seagull.ini`, `spider.ini`,
`dog.ini`, `guard.ini`, `vulture.ini`. Skrypt `INST` podaje znaczniki i bryły (`visibilityVolume`, `guardVolume`,
`attackVolume`, `inhabitVolume`, `landhole1`/`seahole1`, `hole1..N`, `flightDir`) oraz `clother` (podmiana tekstur,
np. `man_1`→`man1_1`). Animacje szkieletowe liczymy na CPU.

- **Ludzie** chodzą po nagranych ścieżkach (`Profiles/<nazwa>.dat`), są tylko do 50 m od gracza. Auto bliżej niż
  0,8 m od stóp (albo szybsze niż 15 km/h do 4 m przed nimi, w kącie 25°) dostaje kopniaka (`Pendal`, 4 m/s od nich i
  7 m/s w górę) albo raz na pięć, gdy jest 2,2 m miejsca, zostaje podniesione i rzucone (`Brosok`).
- **Strażnicy** obracają się do gracza, celują, strzelają i kopią ([strzały](#efekty)).
- **Mewy** chodzą i odlatują z krzykiem, gdy auto, człowiek albo pies podejdzie na 4 m.
- **Kraby** ruszają z punktu między `landhole1` a `seahole1` bokiem do auta, uciekają do morza i giną pod kołami
  (rozgniecenia kraba oryginał nie pokazuje).
- **Pająki** biegają 0,25 m/s między dziurami, uciekają (1,65 m/s) przed autem bliżej niż 1,8 m i giną pod kołami.
- **Psy** (do 25 m od domu) gonią punkt przed autem w stożku 45° i atakują przez 1,5 s (`dog_attack`).
- **Sępy** krążą nad country_2 po `Profiles/vlt_<trasa>_<1|2>.gpf`.
- **Ślady stóp** (0x50f930, ustawienia `peop_sole.ini` 0x50fcb0, wartości przez 100): człowiek, który się rusza
  (szybciej niż 0,01 m/s) w miejscu ścieżki o stanie 1, 2, 3 albo 5, co `TimeDelay` (0,68 s) kładzie ślad `ShiftX`
  (0,12 m) na prawo od stóp wzdłuż osi X modelu, potem na lewo (lustrzany w poziomie), `ShiftY` (0,03 m) nad stopami,
  płasko (normalna w górę), palcami w stronę, w którą patrzy; `RepairMan` śladów nie zostawia. Ślad to kwadrat
  o boku 2 × `Size` (0,36 m) z teksturą `sole_footprint`, „naklejka” 0x478440 (najwyżej 256 naraz, wspólne z dziurami
  po kulach): pojawia się przez 0,2 s, żyje 30 s, blednie przez ostatnie 10 s. Rysuje się jak ślady opon (flagi
  `0x80000021`, mieszanie „x2”): kolor przechodzi w szarość 128, która zostawia ziemię bez zmian (0x50fbc0). U nas
  ślady kładą tylko widoczni ludzie (do 50 m od gracza), a pula 256 jest osobna od dziur po kulach.
- Zderzenia z autem i „Wielkie Łup!”: [zderzenia](#zderzenia-z-innymi-ciałami); dźwięki: [dźwięk](#dźwięk).

## Ruch uliczny

Klasa `RDCR` (0x516650; `physics/traffic.h`). Na beach_2 i beach_3 po drodze jeżdżą w obie strony hummer, dwa kampery
i trzy pikapy (w kilku malowaniach), na urban_1 hummer, a na beach_4 pływający BTR.

- **Odjazdy**: wszystkie auta stoją w jednym miejscu poza trasą. `Settings/rdc_<trasa>.ini` (bez pliku nie ma
  ruchu): `Freq`/100 s między autami, `Disp`/100 tego w obie strony losowo, `Accel`/10 tempo odtwarzania nagrania,
  `Wheel` obrót kół w °/s, `Slow` (hummer z urban_1 i BTR: słabsze odepchnięcie i dźwięk `roadcar_slow`). Od startu
  (komunikat 0x259) co `Freq`±`Disp` rusza pierwsze stojące auto, na zmianę po `rdc_<trasa>_forw.gpf` i `_back.gpf`
  (klucze cofające się w czasie na początku nagrań beach_2 i beach_3 gra przeskakuje), a na końcu ścieżki wraca do
  kolejki.
- **Rysowanie**: model z `Roadcar.sb` ma obrócony węzeł główny, który gra zastępuje pozą auta. Do 60 m od kamery
  auto jest rysowane i animowane: przechylone o −atan(pochylenie/4,2), opuszczone o 0,75·cos tego kąta, przednie
  koła skręcone o 150·skręt (do 30°), koła kręcą się, gdy auto jedzie. Tabela modeli (0x574230) podaje pudło kolizji
  (z całym poddrzewem) i koła; pudło i `<koło>_body` dostają flagę 4 i nie są rysowane. Malowanie ze skryptu `INST`
  (`body` 1/2: `trailer_txt_0N`, `box`). Światła (`fara*_body`, tekstura z połówką zgaszoną i zapaloną) świecą
  w bryłach `roadcar_*` (tunele). Auta trąbią albo włączają syrenę, gdy kamera jest blisko.
- **Zderzenie** (0x534880 → 0x5165a0 → 0x4f3470): przeciwnik wypada z nagrania, auto gracza dostaje 6 m/s od
  samochodu i 1,5 m/s w górę (`Slow`: 2 i 0,5) przy każdym dotknięciu; hummer stoi 0,6 m nad drogą, więc autko
  zmieści się pod nim między kołami.
- **BTR** (`roadcar.ini`: `TowerVel`, `TowerShootT`) celuje w auto w klinie 20 m ±15° przed lufą, po 0,33 s strzela
  serią (komunikat 0x261 do `BURST_MNG`, jak strażnik, więc trafienia działają tak samo, także na przeciwników),
  potem co 1,25 s, cztery razy, dopóki cel jest bliżej niż 25 m; zgubiwszy cel, wodzi wieżą ±50°. Tempo obrotu
  wieży (2 × `TowerVel`) jest zgadnięte.
- **Cienie i poświaty** (0x516f50, przy `VIDEO_RoadCarShadow`) idą do kolejki świateł gry (0x44c640, do 100 na
  klatkę), która kładzie je na pobliskie ściany trasy (0x44c750, 0x45a220): od ściany odległej o h < R plama ma
  promień √(R²−h²), uv = 0,5 + d·0,453125/ten promień, a siłę (1/255)^((h/R)²). Poświata: kampery i ciężarówki (ta
  sama flaga co światła), przy każdym reflektorze punkt 2 m przed nim, R = 2 m, biała tekstura `shex_wht` dodana do
  obrazu, niezależnie od tego, czy światła się palą. Cienie: tylko auta `Slow`, przy każdym kole punkt `ShadowY`/100 m
  (0,48) pod osią, R = `ShadowR`/100 m (0,46) z `roadcar.ini`, tekstura `wheel_shd` obrócona wzdłuż koła,
  odejmowana od obrazu (tryby mieszania 4 i 5 z 0x45c410 to nasze odczytanie). U nas plamy nie leżą na morzu (fale
  je poruszają), więc pływający BTR ma cienie tylko na brzegu. `rcinfo traffic` podaje, na ilu ścianach leżą (hummer
  na urban_1: 4 cienie na około 10 ścianach).

## Woda

- **Gdzie jest**: ściany z teksturą `sea`. Poziom morza: 0 na beach_1, 2,15 na beach_3; dno pod nim to zwykłe ściany
  kolizji (na beach_1 piasek opada od −0,3 m przy brzegu do −1,4 m). Na country_1 płytkie kałuże (`pool1..3`), na
  country_3 strumień (kilka cm) i wodospady.
- **Morze** (`formats/water.*`): ściany `sea` pod węzłem, który ma plik `Settings/WLOD_<trasa>_<węzeł>.ini` (np.
  `WLOD_beach_1_more1.ini`). Suwaki są przycinane do zakresu i skalowane: `amp`, `amp2`, `speed`, `length2` /100,
  `period`, `period2` /10, `angle` ×10°, `posX/posZ` −500, `offset` i `magnetOffset` /100 − 0,5 m, `magnetRadius`
  /20 m, `alphaRadius` /10 m. Wierzchołek morza ma wysokość `y0 + offset + (1−s)·magnetOffset + s·(fala1 + fala2)`,
  gdzie `fala1 = amp·(0,75·sin(period·(x·cos angle + z·sin angle + speed·t)) + 0,25)`,
  `fala2 = amp2·sin(|xz − środek|/length2 + period2·t)`, a `s = clamp(d/magnetRadius)` przy odległości `d` od brzegu
  morza (krawędzi siatki wody). Fala 1 przychodzi z kierunku `angle`, fala 2 to kręgi zbiegające się do `posX/posZ`.
  Na beach_1 `offset` to −0,37 m, więc morze leży poniżej piasku przy brzegu (−0,31 m) i styka się z plażą.
  Odległość od brzegu liczy się tylko wewnątrz bryły `water_box` (`EVOL`, beach_1, beach_3, country_3); poza nią
  `s = 1`. Krycie rośnie od 35/255 na brzegu do pełnego w odległości `alphaRadius`, ale tylko przy aucie: zwykła
  odległość `sqrt(dxz² + 187·dy²)` od auta między `distAlphaClampMin` a `Max` (0 i 8) przechodzi w pełne krycie.
  Szarość wierzchołka idzie od `colorMin` do `colorMax` z wysokością fali. Nie robimy `clampY*` (w znalezionym kodzie
  nieużywane) ani `alphaPow`. Odbicia: [mapa otoczenia](#wygląd-auta-i-światło). U nas teselacja
  (`shader/water.tesc`, `water.tese`) dzieli morze przy kamerze co 15 cm (dalej co 2% odległości) i podnosi
  wierzchołki w GPU; bez teselacji robi to shader wierzchołków na oryginalnej siatce.
- **Piana przy brzegu** (ściany `coast*`): piana idzie za falą 2 morza (`more1`): z `h = sin(faza fali 2)` w miejscu
  piany, gdy woda opada, krycie spada od `AlphaMax` (h = −1) do `AlphaMin` (h = 1), a gdy woda rośnie, do `AlphaMin`
  już przy `HeightOn` (`waterLOD_coast.ini`: `(v−100)/100`, czyli 0). Tekstura przesuwa się o `ScrollVel` (0,11) na
  sekundę. `HeightOff` jest wczytywane, ale nieużywane.
- **Pozostała woda** (kałuże `pool*` na beach_4 i country_1, strumienie `stream*` i wodospady `wtr_fall*` na beach_1
  i country_3, bez fal w `WLOD`) jest płaska i u nas przezroczysta wg głębokości nad dnem z kolizji (pełne krycie od
  0,8 m). `RCCars.exe` (0x523260, tabela 0x575710: nazwa obiektu zawiera `stream`, `wtr_fall` lub `pool`) tylko
  przesuwa jej teksturę (strumienie i wodospady płyną, kałuże falują szumem `PoolNoiseFreq/Len` z
  `WaterLOD_Stream`), a jej wierzchołków nie rusza.

## Efekty

- **Cząsteczki** (0x477xxx; `fx/particles.*`): emiter tworzy `rate` cząsteczek na sekundę (ułamki przechodzą na
  następną klatkę), rozkłada je wzdłuż drogi, którą przebył od poprzedniej klatki (0x4776e0), i rysuje jako kwadraty
  zwrócone do kamery, posortowane od najdalszego, z alfą malejącą z wiekiem (0x459260). Rozmiar to
  `skala × DX / głębokość` pikseli ekranu 640 szerokiego przy poziomym polu widzenia 80° (0x405f70), u bryzgów
  (flaga 0x80) stała liczba pikseli. Suwaki efektów czyta 0x532410 jako pozycję suwaka przez dzielnik zapisany
  w kodzie (brak klucza to 0), nie przez `FValues`. Efekty aut rysują się z mgłą.
- **Kurz** (`car_dustx`, 0x52e000, 0x52e320, 0x52e810, 0x52e960; tekstura `dust`): z miejsca styku koła z suchą
  ziemią, nie w wodzie, do 12 m od kamery. Nawierzchnia (`Settings/surf_default|sand|wetsand|dunesand|gravel.crs`)
  daje mnożnik intensywności (0 dla asfaltu), kolor i czas życia. Ilość to `dust_int(km/h) × 2/3 × mnożnik`
  (`Splines/dust_int.spl`); jadąc do przodu kurzą tylne koła, na wstecznym przednie (połowa) i tylne (ćwierć), na
  głębokim piasku wszystkie. Przeciwnicy kurzą tylko tylnymi i z krócej żyjącym kurzem. Cząsteczka dostaje
  `dust_speed(m/s)` procent prędkości auta (`dust_speed.spl`), zwalnia w poziomie (`SpeedAtt`), obraca się coraz
  szybciej i rośnie w ciągu sekundy do `DynamicScale` (tył) albo `DynamicScaleForw` (przód) razy (0x52e180). Przy
  buksowaniu (poślizg koła ponad `WheelSpinThresh`, z gazem albo hamulcem) kurz leci do tyłu z prędkością obrotu
  koła × `WheelSpinSpeed`, a ilość to `dust_int(rad/s obrotu) × WheelSpinScale`.
- **Bryzgi** (`WATER_SPRAY`, `water_spray.crs`, 0x527e20, 0x527f70, 0x528220, 0x528780; tekstura `particle`): gdy
  lustro wody przecina koło, a auto jedzie szybciej niż 15 km/h; ilość to `wspr_int(km/h) × cięciwa / promień`.
  Krople lecą na zewnątrz i w górę pod kątem `AngleIncidence`, połowa w drugą stronę i trzy razy krócej, płyną
  z autem (`SpeedDecay`), drgają (`OscillationLen`) i maleją do połowy w 0,7 s. Są kilkupikselowe.
- **Ślady opon** (`car_trace`, 0x52f050–0x5300d0; tekstury `t_halfdry_tire2_N`): każde koło (u przeciwników tylko
  tylne) ma pierścień punktów, 32 dla przednich i 48 dla tylnych kół (u przeciwników połowa); pełny nadpisuje
  najstarszy. Punkt powstaje, gdy auto jedzie co najmniej 1 km/h, a koło dotyka ziemi poza wodą; siła śladu zależy od
  dźwiękowej nawierzchni (0x534fc0): piasek 0,7, mokry piasek 1, udeptany 0,3, żwir i drewno 0,6, inne bez śladu;
  przednie koła połowa. Punkt leży 1 cm nad ziemią, a krawędzie pasa są o 5 cm × szerokość opony (0,95/1,07/1,25/
  1,35, tablica 0x5738c8, wybiera ją kupiona opona; u nas poziom ulepszenia) w bok od kierunku jazdy. Dopóki auto
  jedzie prosto po jednym trójkącie ziemi, ostatni punkt przesuwa się dalej (test 0x52fb60), inaczej dochodzi nowy;
  zmiana kierunku przód/tył przerywa pas. `car_trace.ini` ma dwa klucze `maxLen` i żadnego `maxHeight`, więc gra
  czyta `maxHeight` = 0 i prawie każda klatka dokłada punkt: ślad ma długość pierścienia (przy 60 klatkach na sekundę
  kilka metrów). Pierwszy punkt pasa i koniec pasa są przezroczyste, ostatnia ćwierć życia (60 s) zanika. Rysowane do
  20 m od kamery (0x52fd00) jako tekstura × siła + szarość (1 − siła)/2, mnożona przez to, co pod spodem, dwa razy
  (u nas mieszanie `DstColor`/`SrcColor`, wywnioskowane): przy sile 0 ziemia się nie zmienia. Kolejne czworokąty
  jednego auta są podnoszone o 2e-5 m. We mgle idą ku połowie koloru mgły, jak przebieg 0x463e70 dla takich mieszań.
- **Kilwater** (`wtr_trace`, 0x528ce0, 0x528ed0, tylko auta graczy): jeden czworokąt na wodzie za każdym tylnym
  kołem i za karoserią, tworzony co klatkę, gdy lustro wody przecina koło (albo połowę wysokości karoserii) i auto
  jedzie co najmniej 1 km/h. Sięga `ForwZ` do przodu i `BackZ` do tyłu (razy 0,25 + 0,025 × km/h do 30 km/h),
  `ForwX`/`BackX` w bok; tekstura przewija się o `wtr_text(km/h)` procent na sekundę, a jej rozciągnięcie losuje się
  z `AnimDUVMin..Max` przy wejściu do wody. Przezroczystość `ForwA`/`BackA` maleje, gdy koło wychodzi z wody albo w
  nią zapada. Ustawienia `*Body` czyta, ale używa tylko dla koła o numerze −1, którego gra nie podaje.
- **Piana wokół kół** (`whr_horse`, 0x526020, 0x5262e0): czworokąt leżący na wodzie wokół każdego koła (długość
  `ForwZ` × cięciwa, jaką woda wycina z koła, szerokość `ForwX` × szerokość opony) i wokół karoserii, sprawdzanej
  ćwierć jej wysokości niżej (`ForwZBody`, `ForwXBody`, tekstura powtórzona 4 razy). Tekstura skacze co `AnimDelay`
  s; obie wodne warstwy mają drugą teksturę (`*_alpha`) jako maskę przezroczystości.
- **Spaliny** (`exhausted_gas`, 0x530180, 0x5303c0, 0x5306d0, tylko auta graczy): z węzła `booster_N_end` modelu
  (N = kupiony dopalacz, 0x50c0e0, 0x50bcc0), tekstura `dust`, do 100 kłębów. Ilość to `eg_int(km/h)` × 0,75 przy
  gazie, × 1,5 na turbo (powyżej 5 km/h), a włączenie turbo powyżej 10 km/h daje na `ExplodeTime` s `2 × eg_int(50)`
  kłębów, trzy razy dłużej żyjących, prawie przezroczystych (`ExplodeAlpha`); kolor ciemnieje wtedy 512/s do
  `ExplodeColor` i wraca 256/s. Kłąb dostaje `Speed` wzdłuż rury plus prędkość auta, zwalnia (`SpeedAtt`) i rośnie
  bez końca o `DynamicScale` − 1 na sekundę (0x530330). Buksujące tylne koło (poślizg ponad `WheelSpin`, z gazem albo
  hamulcem) dymi `eg_int(2 × poślizg)` kłębów na sekundę. Flagę, która odpala kłąb (0x577c), uznajemy za włączenie
  turbo (wywnioskowane).
- **Brud** (0x52e320, 0x531c20, 0x507dc0, 0x531450): każde koło, które kurzy, dokłada do licznika auta
  `ilość kurzu × 0,1` na sekundę (do 3000), każde koło w wodzie zmywa 350 na sekundę. Licznik wybiera między czystym
  autem a teksturami `dirt_car<auto>_1` i `_2` (auta 2 i 3 mają osobne `a`/`b` dla dwóch tekstur karoserii
  `car_askin*` i `car_bskin*`) i miesza sąsiednie; brud kładzie się na lakier według swojej przezroczystości. Tylko
  auta graczy.
- **Rozbryzgi fal** (`WATER_SPLASH` 0x5272f0, `WATER_SPLASH_MNG` 0x527d80, znaczniki 0x5277a0, co klatkę 0x527a40):
  znacznik `water_splash_N` (do 16; beach_1, beach_3, country_3) bliżej niż `ActiveRad` (20 m w poziomie) od kamery
  i na ekranie co `TimeDelay ± TimeDelayDisp` s (1 ± 0,25) rozbryzguje się w losowym miejscu `Radius` (5 cm) od
  siebie; na znacznik są dwa emitery. Rozbryzg przez `EmitLife` (0,15 s) wyrzuca 540 kropli na sekundę wachlarzem
  w płaszczyźnie zwróconej do kamery: krople startują obok siebie w odstępie do `SplashLen` (5 cm) i lecą w górę i na
  boki najwyżej `AngleSpan` (45°) od pionu z prędkością `VelLen ± VelLenDisp`, bez grawitacji, zwalniając do
  `VelAttenuation` na koniec życia; maleją do `Scale` (0,5) i bledną od połowy życia. Tekstura `particle`, rozmiar
  zależny od odległości (flaga 0x800), więc to drobne krople. Dźwięk to `water_splash` z `AmbientSounds`.
- **Fale przy brzegu** (`WaterLOD_Wave` 0x525c10, liczby całkowite; `Water_Wave_New` 0x52a550; znaczniki 0x5255c0,
  co klatkę 0x525700, fale 0x529dd0, rysowanie 0x52a030): tylko na morzu `more1` (albo pierwszym z falami). Znacznik
  `water_wave_N` (do 8) w promieniu `ActiveRad` (40 m) i na ekranie co `TimeLong` (2 s) puszcza długą falę na
  grzbiecie fali 2 morza tuż za znacznikiem, obróconą losowo o `Angle` (5°), a co `TimeShort` (0,5 s) krótką
  w losowym miejscu do `Len` (3,5 m) od niego; jest ich najwyżej 32. Każda płynie z falą 2 do środka morza
  z prędkością `period2 × length2` m/s, leży na wodzie (znika, gdy wody nie ma) i żyje `TimeLifeLong` albo
  `TimeLifeShort ± Disp` s. To czworokąt na grzbiecie, `Len` (1,7 m) w każdą stronę, stojący `Height` (0,58 m) ×
  jasność nad wodą i obrócony wokół grzbietu do kamery; całość zanurza się wzdłuż normalnej wody o `DHeight − 1`.
  Tekstura `water_wave` przesuwa się wzdłuż grzbietu skokami (`AnimSpeed` co `AnimWait`), `water_wave_alpha` wygasza
  brzegi; jasność rośnie do `IncTime` życia i maleje od `DecTime`.
- **Strzały strażnika i BTR** (`BURST_MNG` 0x52d570–0x52df20): strażnik (0x51a950) jest aktywny, gdy auto gracza jest
  w jego `visibilityVolume`. Celuje w najbliższe auto w `guardVolume` (gracza albo przeciwnika; auto, do którego
  strzelał ostatnio, liczy się jak dwa razy dalsze, 0x5359f0), odwraca się do niego i 0,15 s po namierzeniu wysyła
  komunikat 0x261 z broni 1,5 m nad stopami, potem co 1,25 s, trzy razy, i znów wybiera cel (detektor 0x535720
  z parametrami 0,15 / 1,25 / 2; BTR ma 0,33 / 1,25 / 3). Każdy komunikat zaczyna serię od nowa (0x52de30), wygrywa
  ostatni. Seria to `NShoots` (6) strzałów co `TimeShoot` (0,1 s), przechodzących w poprzek auta od `+r` do `−r` ×
  `Radius` (losowy kierunek `r`), celowanych w auto z wyprzedzeniem `prędkość × Offset / 2`; promień sięga 1,5 raza
  dalej (co najmniej 10 m). Kula, która nie trafi w ziemię, przepada. Kula w ziemi wyrzuca stożek grudek
  `BULLET_EXPLODE`, kłąb `BULLET_DUST` i zostawia dziurę `bullet_footprint` (10 cm, 8 s, blednie przez ostatnie 4 s,
  najwyżej 256), także gdy po drodze trafiła auto. Dźwięki: `burst` przy broni na początku serii, `bullet` w miejscu
  trafienia pierwszej kuli.
- **Trafienie auta** (0x508290, 0x508560, 0x508ae0): po drodze do ziemi kula przechodzi przez pudło wokół karoserii
  (`ENVIR_CAR_BODY`) dwa razy większe w każdą stronę (połówki wymiarów brane ×4 od narożnika) i trafia z szansą
  `rand() / 32767 × 100 < p`: gracz Overkillem i Hummerem 20%, Buggy 10% (`Geometry typeCar` 0/2 i 1), auta
  komputera 15%. Pierwsze trafienie zapala dym `BULLET_SMOKE` (`TimeEmit`), każde pcha auto 6,5 m/s od strzelca
  i 3,25 m/s w górę (0x4f3470, jak przy psie i ruchu ulicznym) i liczy się w `car+0x5724`. Dopóki licznik nie jest
  zerem, gra co klatkę zeruje klawisze auta i trzyma `Brake` (klawisz 7, ręczny; 0x4f6b20), a 2 s po pierwszym
  trafieniu (kolejne nie przedłużają) stawia auto na ostatnim punkcie kontrolnym jak `R` (0x508b50). Przeciwnik
  wypada z nagrania, autopilot nim jedzie (nadpisuje wyzerowane klawisze) i po 2 s też wraca na swój punkt.
  Komunikat 0x4e86 o trafieniu trafia tylko do dziennika gry sieciowej, nie przenosimy go.

## Wygląd auta i światło

- **Ulepszenia na modelu** (0x50bf90): spośród węzłów `UPGRADES*` widać tylko `booster_<doładowanie+1>`
  i `compressor_<rezonator+1>`, opony `tire2*`/`tire3*` z teksturą `tire<2|3>_<opony+1>`, a przy doładowaniu powyżej
  0 koła są poszerzone o 0,95 / 1,07 / 1,25 / 1,35 według poziomu opon (0x50be40). Gracz ma poziom z klawisza U (w `-dev`; menu
  ulepszeń jeszcze nie wybiera), przeciwnicy `AI<n>Boost/Reson/Tires` z `ailayouts.ini`.
- **LOD-y** (0x4f98e0, 0x501c10, 0x4fcd8d, 0x50bb60): dalej od kamery auto rysuje się modelami `Car<n>LOD1` (od
  7,96 m) i `Car<n>LOD2` (od 8,87 m), odległości z `Settings/LOD.ini`; każdy model wyposażony tak samo.
- **Mapa otoczenia (odbicia)** (0x463360, 0x465aa0, 0x48f530): siatki, które ją mają, rysuje drugi raz z teksturą
  `cubemap_racer` (sześć jednakowych ścian sześcianu: miękka biała plama na czarnym), dodaną do obrazu
  (`SrcAlpha, One`) z siłą równą alfie `TEXTUREFACTOR` razy krycie siatki. Współrzędne to wektor odbicia
  w przestrzeni kamery (`D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR`), więc blask zależy od kąta patrzenia, nie od świata.
  Mapę dostają siatki z flagą `0x20000` (morza i `ENVIR_CAR_BODY_body` aut), siatki z `GRE1` lub `GRE2` w nazwie
  i karoseria (0x48e4b0, 0x4df7b0, 0x507130). Siła: morze `alphaEnvMap` z `WLOD_*` (`int(v·2,55)/255`, 16 → 0,157;
  0x521540, 0x524f80), w aucie wg nazwy z `Settings/carEnvMap.ini` (0x4f9d00, 0x5080b0, 0x507d80): karoseria
  `EnvMapBody` 112/255, `*GRE1*` (szyby, chromy) `EnvMapGRE1` 255/255, `*GRE2*` (lampy, zderzaki) `EnvMapGRE2`
  137/255; reszta z flagą (np. rekwizyty ze `stone.sb`) całą mapę. `EnvMapUpgrades` (156) dotyczy węzłów ulepszeń;
  tego klucza jeszcze nie używamy. U nas to jeden przebieg `shader/level.frag` (`environment()`), który dodaje mapę
  do koloru; takie siatki oryginał rysuje bez mgły.
- **Cień auta** (0x5072f0, przy `VIDEO_CarShadow`; `game/world/carshadow.*`): tylko auta graczy, przeciwnicy
  w oryginale cienia nie mają. Jedna plama kolejki świateł (rodzaj 3, z teksturą, 0x44c640) położona na trasie jak
  cienie kół ruchu ulicznego i przyciemniająca to, co pod nią (`Zero, InvSrcColor`, tryb 4 z 0x45c6e0): promień
  `ShadowSize<n>`/100 m (0,36 / 0,29 / 0,42), środek `ShadowShift<n>`/100 − 0,5 m przed środkiem auta (0 / 0 / 0,01),
  tekstura `shadows2` Overkill, `shadows3` Buggy, `shadows1` Hummer (`Settings/carRender.ini`); tekstura obrócona
  tak, że jej oś v biegnie wzdłuż auta. `ShadowDensity`, `FilterSpeed`, `TextureSizePow2` i `FrameSkipNmb` są
  czytane, ale nic ich nie używa.
- **Światło aut** (`carLight.ini`, 0x5077e0 dla każdego rysowanego LOD): najbliższy model (LOD 0) ma światło
  Direct3D: otoczenie `AmbientI` (138) i jedno kierunkowe `DirectI` (128), oba razy jasność auta i obcięte do 255;
  dalsze LOD-y rysują się bez światła, z samą teksturą. Światło pada z (0,0,1) obróconego o `AngleAzimuth` (0°) wokół
  pionu i o `AngleElevation` (75°) w górę (0x40b4c0), czyli z (0; 0,966; 0,259). Klucze kolorów (`AmbientR` …
  `DirectB`) nic nie czyta.
- **Jasność auta** (0x531ff0, `car_lmshd.ini`, co klatkę dla każdego auta; `fx/carlight.*`): gdy kamera jest bliżej
  niż `maxdist` (25 m) i włączone jest `VIDEO_Light` albo `VIDEO_EnvMap`, gra uśrednia teksel lightmapy (bez
  filtrowania, (r+g+b)/3, 0x4572c0: uv z pierwszych trzech rogów ściany) pod pierwszymi czterema kołami dotykającymi
  trasy, a gdy żadne nie dotyka, tylko dla auta gracza, pod autem (promień 1,5 m w dół z 1 cm nad nim). Wartość × 255
  przechodzi przez `Splines/light_car.bspl` (liniowo, poza końcami stała: 112,6 → 158,9, 200,9 → 260,0) i / 255,
  więc 0,62 … 1,02. Za pierwszym razem jasność od razu taka, potem goni cel z prędkością `alphavel`/100 (1,27) na
  sekundę, gdy różni się o więcej niż `deltaalpha`/100 (0,05). Auto dalej od kamery ma 1. U nas przeciwnicy
  odtwarzający nagranie nie mają styków kół, więc szukamy lightmapy pod ich kołami tak, jak są narysowane
  (wywnioskowane, nie z oryginału). `rcinfo <gra> shade RCCarsDB/beach_2.sb` pokazuje lightmapę wzdłuż trasy
  i jasność, jaką daje.
- **Jasność ludzi, strażników, rekwizytów i aut z ulicy** (0x532cf0 ustawia, 0x532d50 parametry, 0x532d80 co
  aktualizację; `VIDEO_CharLight`, domyślnie włączone; `fx/carlight.*` `FeatureShade`): teksel lightmapy pod nimi
  (promień od 0,1 m nad miejscem, gdzie stoją, 2 m w dół; ściana bez lightmapy daje 1, brak trafienia zostawia
  jasność) × 255 przez `Splines/light_feat.bspl` (ta sama krzywa co aut; `feat_lmshd.ini` tylko ją nazywa, gra
  i tak pyta o „light_feat”) / 255, obcięte do 0..1, czyli 0,62 w cieniu i 1 w słońcu. Idzie do współczynnika
  tekstury Direct3D wszystkich ich siatek (0x451390 → węzeł +0x2fc, flaga 0x100; 0x464d08, 0x45c980: MODULATE
  TEXTURE, TFACTOR) bez żadnego innego światła, więc w słońcu widać ich tekstury takie, jakie są. Co `interval` s
  (ujemny: co aktualizację) bierze wartość spod nich za cel, potem goni go z prędkością `speed` na sekundę, gdy
  różni się o więcej niż `delta`; na początku jasność 1. Rekwizyty (`STON` 0x50cb00): 2/s, 0,1, co klatkę, ziemia,
  której dotyka bryła (u nas promień pod nimi). Ludzie (`PEOP` 0x510400): 2/s, 0,1, co 1 s, gdy są aktywni.
  Strażnicy (`GUAM` 0x51a7d0): raz, przy ustawieniu. Auta z ulicy (`RDCR` 0x516f50, gdy są rysowane): 2/s,
  `DeltaAlpha`/100 z `roadcar.ini` (0,14), co klatkę; ich reflektory zostają białe. Mewy, kraby, pająki, psy i sępy
  tego nie mają (u nas zostają w świetle reszty modeli).
- **Paski bieżnika na trasach** (`wt_wetsand`): to nie ślady aut, tylko część mapy z bitem `0x40000000`
  (mnożenie ×2, ten sam stan `0x121` co ślady opon): ich szare tło (128) zostawia piasek, a ciemniejszy bieżnik go
  przyciemnia. U nas `ScenePipelines::modulate` po nieprzezroczystych, bez światła; mgła ciągnie je do szarości,
  która nic nie zmienia. Czy oryginał kładzie na nie lightmapę (country_4), nie wiadomo; pomijamy ją, bo
  przyciemniłaby piasek dwa razy.

- **Antena** (0x4ff2d0 ustawia, 0x4fe9d0 porusza, łańcuch punktów 0x44cc10; `physics/antenna.*`,
  `game/world/antennaview.*`): gra chowa węzeł `ANTENNA` pełnego modelu auta i rysuje `Car<n>Antenna` z `Car.sb`,
  siatkę `Antennaa` ze skórą na stawach `jnt3_1..3`. Łańcuch ma `nPoints` (4) punkty co `chainLength`/(n−1); dwa
  pierwsze siedzą na aucie (punkt z pudełka `ANTENNA`: największe x i z, najmniejsze y; i jedno ogniwo wyżej wzdłuż
  osi Y auta). Kroki po 3 ms z pozą auta interpolowaną; na wolne punkty działa ciężar masa × 10, opór powietrza
  √(volume/l)·l·sin(kąt do prędkości)·`windFriction`, na czubek pchnięcie −100·(1 + sin a) wzdłuż osi Z auta,
  zginanie ku poprzedniemu ogniwu siłą kąt × `stiffness`/l w poprzek ogniwa (suwaki /100, powyżej 1000 → 10, więc
  sztywność 10), tłumienie ruchu względem ramki poprzedniego punktu `damping`/l; długości ogniw trzyma układ
  trójdiagonalny mnożników Lagrange'a, potem v += a·dt/2, p += v·dt i powrót na długości. Symulacja idzie z x i z
  auta podzielonymi przez 2 (antena czuje połowę ruchu na boki). Pierwsza ramka to ćwierć obrotu wokół z dla
  wszystkich aut, więc łańcuch zawsze startuje prosto w górę. Rysując (0x4ff0c0), gra stawia staw j na punkcie j
  z ramką punktu j+1, a potem obraca go wokół jego własnej osi y o kąt z jego macierzy w modelu (0x4ff2d0 zapisuje
  macierze `jnt3_1..3` względem rodzica pod +0xdc4; kąt = asin(−z osi x), 0x4074c0, obrót 0x40cab0); kolejne stawy
  idą za nim, więc wygięcia się sumują. Antena Buggy (modelowana wygięta do tyłu o 33°, 31° i 8°) zostaje więc
  wygięta do tyłu także w grze, antena Car3 o 10° u podstawy, Car1 prosta. Tylko auto gracza szuka
  sufitu: promień w górę z podstawy na najwyższą wysokość z `CacheData/car_antenna_Car<n>_H2F.data` + 0,1 m; gdy
  coś jest bliżej, ta tablica daje kąt a, przy którym czubek mieści się pod spodem (bez sufitu: ostatni kąt
  tablicy; odległość minus jedno ogniwo przycięta do 0…0,4667 i plus ogniwo, bez interpolacji: kąt z wpisu
  poniżej). **Tablica H2F** (0x4ffe30, liczona tylko, gdy pliku brak): antena stoi 10 s krokami 0,01 s, potem dla
  a od π/2 co π/100 po 10 s symulacji zapisuje (a, wysokość, a), dopóki wysokość rośnie (najwyżej 200 wpisów).
  Wysokość to góra obrysu instancji `Car<n>Antenna` (+0x220 węzła, czyli największe y; 0x210 to najmniejsze x, y,
  z, 0x21c największe; obrys skóry po wygięciu stawów) minus y podstawy (pozycja ramki punktu 0, +0x8f0). Z
  wygięciem stawów nasza symulacja daje tablicę Buggy co do milimetra (0,091 przy π/2 … 0,191 przy 3/2 π). Tablice
  Car1 i Car3 z paczki liczono przy innych suwakach (obie wychodzą z `chainLength` 20 zamiast 25 i sztywnością 25
  zamiast 10, przy π/2 i na końcu co do milimetra), ale gra
  bierze je takie, jakie są (sprawdza tylko nagłówek `ARAC`, liczbę wpisów i końcowe `ARAC`), więc my też.
- **Dźwięk `Antenna_scratch`** (0x4fe5f0, tylko auto gracza; `Sounds::tickAntenna`): kanał `AntennaSnd` w modelach
  `Car<n>Antenna` z `Car.sb` ma jeden dźwięk (`Antenna_scratch`, flagi 0x80420: pętla, bez restartu, ogon
  350 ms). Gdy promień w górę z podstawy trafia (coś w zasięgu anteny) i auto jedzie szybciej niż 1 km/h
  (długość prędkości ciała ×3,6, 0x50b6a0), gra ustawia mu głośność (SOND +0x1c): −15 dB przy 1 km/h liniowo do
  0 dB przy 35 km/h i więcej, i go gra; gdy nic nad anteną nie ma albo auto stoi, zatrzymuje kanał (z ogonem).
  Przy `VIDEO_Light` gra barwi jeszcze antenę jasnością auta (+0xe824 × 255, szaro), co u nas robi `CarLight`.

## Słońce, detale i ruchome dekoracje

- **Słońce i flara obiektywu** (klasa `SUN$` 0x478cd0, `VIDEO_Sun`; `fx/lensflare.*`, `game/world/sunview.*`):
  słońce stoi na znaczniku `SUN_AF` (trasy miejskie go nie mają). Tarcza `sun_disc` w kolorze `ColorR/G/BSun`,
  mieszana alfą (flagi 0xa0000003), na głębokości słońca, więc zasłania ją to, co stoi przed nim; szerokość
  `SizeSun` × szerokość ekranu 4:3 o tej wysokości. Co `TimeShootRay` s promień od kamery do słońca (0x456260) mówi,
  czy je widać, i flara narasta lub gaśnie przez `FadeTime`. Jej pięć plam leży na prostej od punktu 0,1 m
  (płaszczyzna widoku, 0x500760) + `CamOffset` przed kamerą ku słońcu, w odległościach `Dist1..4` (dwie pierwsze
  ujemne, za tym punktem), piąta (`shine`) na słońcu; dodawane (SrcAlpha, One; flagi 0xa0200005) przed wszystkim.
  Przezroczystość: 255 × (1 − odsunięcie słońca od środka ekranu / (pół ekranu + pół tarczy)), mniejsza z osi,
  razy narastanie. Tarcza jest kwadratowa na każdym ekranie: wysokość to szerokość / (proporcje × 0,75), a proporcje
  gra bierze ze stałej 4/3 (0x48f520), nie z ekranu. Założone: bez mgły.
- **Tekstury detali** (`Settings/Detail.ini`, 0x480a70; `VIDEO_Detailer` domyślnie włączone): sześć tekstur
  `detailer`, `detailer2..6` kładzionych na siatki, których pudełko jest bliżej kamery niż `DistObj` × 0,1 m
  (3 m): kolor × 2 × mix(0,5, detal, `Density`/256), detal powtórzony `Scale` × 0,1 razy na teksturę bazową,
  z przesunięciem mipmapy `LODBias` × 0,04 − 2. Gra trzyma numer detalu przy teksturze (+0x40) i dla ściany z
  dwiema teksturami bierze mniejszy niezerowy (0x480ef0). Numer nadaje wczytanie `GLTX` sceny (0x48e350): najniższy
  z bitów 23..28 grup `GLTX` (`SbTexture::groups`) daje `detailer` (23: kamień, bruk), `detailer2` (24: piasek),
  `detailer3` (25: asfalt, beton), `detailer4` (26: żwir, ścieżki), `detailer5` (27: `floor`) albo `detailer6`
  (28: nie ma go żadna tekstura ani w danych). Bity 21 (drewno, metal) i 22 (piasek, zawsze z 24) detalu nie dają.
- **Ruchome dekoracje** (`AnimObj`, 0x52b7e0/0x52c110, ruch 0x52b860/0x52c180; `fx/animobjects.*`): każdy węzeł
  z „SUBMARINE” w nazwie (łódź `SUBMARINE_NC` na beach_4) dostaje przesunięcie w górę `Y0 + DY·cos(T·t)`
  (`Settings/submarine.ini`: −0,96 m, 1,44 m, 0,17 rad/s), czyli wynurza się i zanurza co 37 s. Pod węzłem
  „radar_ZRK” część przed dzieckiem „center” kręci się wokół pionu przez środek jego pudełka, 60°/s.
- **Dym z chłodnic** (`HATCH_SMOKE`, `VIDEO_HatchSmokeEff`, 0x52b750/0x530c60; `fx/worldfx.*`): każdy węzeł
  z „KOOLER” w nazwie (dwa na beach_4) dymi ze środka swojego pudełka, `ShiftY` wyżej, `Intensity` kłębów na sekundę,
  ale tylko gdy jest na ekranie. Kłąb startuje `ShiftXZ` m w losową stronę, żyje `TimePart ± TimePartDisp`, wznosi się
  z `SpeedY` hamując o `SpeedDecay` na klatkę, obraca się i rośnie `DynamicScale` razy przez `DynamicScaleTime`
  najdłuższego życia (do 10 m). `ZOffset` (0,49) traktujemy jako przesunięcie głębokości sprite'a (założone).

## Mgła

Moduł „Fog” (0x5324c0; `formats/fog.h`): po wczytaniu trasy gra czyta `Settings/fog_<trasa>.ini` (0x532870):
`Density` × 0,001, `ColorR/G/B`; `Start` i `End` też, ale bez znaczenia, bo Direct3D dostaje mgłę wykładniczą
`D3DFOG_EXP` w tablicy (0x461970): piksel w odległości `w` od kamery zachowuje `exp(-density·w)` swojego koloru,
reszta to kolor mgły. Gęstości: beach 0,006–0,020, country 0,009–0,026, urban 0,023–0,025 (beach_1: po 100 m zostaje
55% koloru; country_1: 12%). Kolor mgły barwi też niebo. Siatki z mapą otoczenia (morze, karoserie) mają mgłę
wyłączoną (0x464e50 … 0x4658b0). Każda zmiana przechodzi liniowo w 0,5 s (0x532aa0), także na starcie z „braku
mgły”. Obiekt z sekcją `FOG` (`FOG_Start`, `FOG_End`, `FOG_EndInt` jako gęstość, `FOG_Color`) mógłby zmieniać mgłę na
zdarzenia 126/127 (wjazd i powrót do mgły trasy), ale żadna trasa takiego obiektu nie ma. W oryginale wyłącza ją
`VIDEO_UseFog 0` w `Config.gm`, u nas jest zawsze. Efekty aut (kurz, bryzgi, spaliny, ślady opon, kilwater, piana)
oryginał rysuje z włączoną mgłą, więc u nas też.

## Kamera

- **Kamera za autem** (`game/chasecamera.*`; 0x5007a0 co klatkę, 0x501180 przy rysowaniu, ustawienia
  `Settings/Camera.ini` czyta 0x4f9da0, suwaki 1..100 liniowo na zakresy wpisane w kod): kamera trzyma własne oko
  i co klatkę ogląda je z układu auta: środek auta (stan bryły, +0x5884) z kierunkiem nosa położonym płasko (gdy nos
  celuje mniej niż 30° od pionu w górę, nos i tył dachu razem, 0x500840). W tym układzie oko ma odległość po ziemi,
  wysokość i kąt wokół auta (0 = prosto z tyłu); każdą ciągnie „sprężyna” 0x4722c0 (cel, tolerancja, pasmo pod i nad
  celem, z którego wartość wraca od razu, tempo powrotu w paśmie, opcjonalnie × (odległość/pasmo)^wykładnik), a oko
  wraca tam, gdzie mówią, patrząc na środek auta (0x471ca0). Odległość `defDistXZ` (30 → 0,79 m) bez opóźnienia, plus
  do połowy więcej od 4 m/s (całość przy 8 m/s, narasta 1 m/s) i 1/5 więcej przy wciśniętym turbo z gazem (0,5 m/s,
  wraca 0,3 m/s); wysokość `defDistY` (0,36 m) w paśmie ±75% wraca `accDistY` (0,71 m/s); kąt w paśmie ±20° wraca
  z `accAlpha`×200 (99°/s) × (kąt/20)², ale 30°/s, gdy auto kręci się szybciej niż 180°/s (0x500a80). Po resecie auta
  wszystko staje na celu (dt = 2e6, 0x508b50).
- **Teren zasłania auto** (rysowany widok, bez wpływu na sprężyny, 0x500e60): gdy teren przecina odcinek od 15 cm nad
  autem do 6 cm pod okiem (0x500d60), oko obraca się w górę wokół auta do `CDT_AngleUp` (29°) z `CDT_AngleUpSpeed`
  (29°/s), a widok podnosi się o `VisTurn` (70 → 11,8°) plus do `CDT_AngleUp_DIR_ADD` (13°) proporcjonalnie do tego
  obrotu, z `CDT_AngleUp_SPEED_DIR_ADD` (suwak 151 poza 1..100, 1,51°/s). `CDT_DistSpeed*` i `CDT_radius` gra pomija.
- **Pole widzenia**: domyślna kamera gry ma poziome pole widzenia 80° przy 4:3 (pionowe 64,4°, 0x405d40). U nas
  pionowe 65° jest stałe, więc na szerokim ekranie widać więcej po bokach. Założone: punkt, na który kamera patrzy
  (macierz obiektu +0xf8), to też środek bryły.
- **Auto w rękach człowieka** (Brosok, 0x5111f0 → 0x4f3640): oko w tej jednej klatce skacze na 2 m za autem i 1 m
  w górę (w paśmie wysokości, czyli ≥0,73 m), a potem stoi w miejscu (tylko obraca się za autem), dopóki człowiek
  trzyma auto (+0xe828, co klatkę 0x4f3700) i przez 3 s od złapania (+0xe830); po rzucie (0x4f3690) i tych 3 s
  sprężyny ciągną je z powrotem.
- **Nad wodą** (0x501180, ostatni krok kamery za autem): jeśli punkt 7 cm pod okiem kamery jest pod wodą (promień
  tylko po wodzie, 0x5350d0), oko idzie na 14 cm nad jej powierzchnię; kierunek patrzenia zostaje. Powierzchnia to
  animowana siatka morza (0x5353b0 bierze bieżące wierzchołki), więc liczymy ją z falami (`Level::waterHeight`,
  `WaterWaves::lift`). Kamera nigdy nie schodzi pod wodę i oryginał nie ma widoku spod wody.
- Sztywna kamera lotu F9: [tryb deweloperski](#tryb-deweloperski).

## Dźwięk

Kanały `SNCH` z `RCCars.sb` i modeli, pliki `Sound\*.wav` z archiwum, odtwarzane we własnym mikserze (`audio/`):
silnik gracza i przeciwników (`audio/carsound.h`), punkty kontrolne, turbo, hamulec, lot i lądowanie, toczenie się po
nawierzchniach, uderzenia i otarcia, buksowanie (`car_motor_ws`, [obrót kół](#fizyka-auta)), rozbryzgi fal
`water_splash`, wodospady `WATER_FALL_N`, ruch uliczny i muzyka MP3 z `Tracks/` w kolejności z `Autoexec.gm`.

- **Wczytywanie** (menedżer dźwięków `snm*`): oryginał wczytuje wszystkie zarejestrowane dźwięki naraz (`snmLoadAll`,
  0x495f20) w kroku `SEND_INIT` ładowania trasy (0x48d15b, po `LOAD_INST` i `LOAD_TEXTURES`, przed zdjęciem ekranu
  ładowania), a dźwięki menu po ich rejestracji (0x4dd755); przy wyjściu z trasy zwalnia je (0x495fa0, 0x48c700).
  Odtwarzanie niewczytanego dźwięku nie wczytuje go, tylko loguje błąd i milczy (`snmGetBuffer`, 0x496000). U nas
  wszystkie dźwięki (`RCCars.sb`, `people.sb`, `AIChars.sb`, `stone.sb`, `Interface.sb`) wczytują się raz przy starcie
  gry i zostają w pamięci; wcześniej czytaliśmy je przy pierwszym odtworzeniu, co zatrzymywało klatkę wyścigu.

- **Szum morza** (kanał `water` z `RCCars.sb`: pętla `water.wav`, pełna głośność do 3 m, −20 dB w 10 m, dalej cisza;
  0x522ed0, 0x522f60, 0x523140): gra bierze ostatnie znalezione morze (węzły z „more” w nazwie szukane w głąb, dzieci
  węzła od najnowszego, 0x51eff0, 0x44ed80), czyli `more1`, a nie lagunę `more2` na beach_1. Dźwięk stoi w narożniku
  obrysu tego morza najbliższym kamerze w płaszczyźnie xz, na wysokości `offset` z `WLOD_*` (oryginał bierze to
  przesunięcie jako wysokość, więc na beach_2, gdzie morze leży na 1,54 m, dźwięk jest 1,5 m pod wodą). Gdy promień
  tylko po wodzie (morze, strumienie, kałuże) z 10 m nad kamerą w dół na 20 m trafi wodę (0x535130), dźwięk idzie pod
  kamerę: na tę wodę albo na kamerę, jeśli jest pod nią. Trasy bez morza (country_1/2/4, urban) nie mają tego
  dźwięku, także nad strumieniami.
- **Postacie**: kanały `SNCH` siedzą w modelach `people.sb` (`Man`, `Woman`, `RepairMan`: `man_voice`/`woman_voice`,
  `Guard` nieużywany) i `AIChars.sb` (`Seagull`: `seagull_vzliot`, `seagull`; `Dog`: `dog_attack`, `dog_pain`); krab,
  pająk i sęp ich nie mają. Gra co klatkę woła kanał postaci (0x494400): człowiek mówi (krzyczy) raz, gdy animacja
  zmienia się na `Hurt` (0x510714); mewa krzyczy `seagull_vzliot` w animacji `start_flight`, a w każdej innej gra
  pętlę `seagull` (czeka, aż krzyk się skończy, flaga 0x800), a gdy gracz jej nie widzi, kanał milknie (0x5127bb);
  pies przy graczu skomli `dog_pain`, póki dotyka go auto szybsze niż 15 km/h (0x5349d0), inaczej szczeka
  `dog_attack` w animacji `attack` (flaga 0x100: zaczyna tylko w cichym kanale, więc nie przerywa skomlenia
  i powtarza się po skończeniu), a poza nią milknie (0x518fa2).
- **Uderzenia w ciała**: gracz słyszy `car_cdt_obj`, gdy wjedzie w człowieka albo strażnika szybciej niż 10 km/h,
  w psa szybciej niż 15 km/h albo dotknie auta z ruchu ulicznego (0x534340, 0x5344f0, 0x5349d0, 0x534880),
  i `car_cdt_bullet` przy każdym trafieniu kulą (0x508560); kanał `car_CDT&Boost` bierze najpierw kulę, potem auto,
  obiekt, nawierzchnię (0x50a620).
- **Strzały**: `burst` przy broni na początku serii, `bullet` w miejscu trafienia pierwszej kuli.

## Gracze

Gracze to pliki `Players/*.scp` (format w [formaty.md](formaty.md#gracze-players-rccarscrs)), lista ładowana przy
starcie (0x4e7450). Bez żadnego pliku gra zakłada gracza „Gracz” (Language/Game 40507) i od razu go zapisuje;
błędy to teksty 403/404 z `Language/System`. Grający to 0x14ac16c, jego imię gra trzyma w `RCCars.crs`
(„Player”/„Name”, 0x404a30). Wybranie gracza (0x4e8440) ustawia trasę, auto, okrążenia i umiejętności z jego
ostatniego wyścigu. U nas, gdy `RCCars.crs` wskazuje gracza, którego nie ma, gra wybiera pierwszego z listy
(oryginał zostawia wtedy pustkę; nie sprawdziliśmy, co robi z tym menu główne).

- **Nowy gracz** (0x4e7c60, 0x4e8320): forsa `DefaultCash` z `Scripts/championship.ini` (100), auta z `Car<n>
  IsEnabled`, wyniki 0, ostatnie okrążenia 3, wszystkie rekordy puste. Zdjęcia to wszystkie `*.tga` z `FacesSys/`
  w archiwum i z `Faces/` w folderze gry, posortowane po nazwie pliku (0x4e5660); domyślne to `face1.tga` (0x4e5970).
- **Panel gracza** (0x4a7170, układ 0x4a6f40 i `dlgPLRSCOMM.ini`): zdjęcie (górne 158 z 256 wierszy, w nim ramka),
  w menu głównym „Witaj, <imię>”, gdzie indziej samo imię (Smash26, podkreślone), trzy linie Smash20 „Ranga”,
  „Samochód” (wybrane auto) i „Czas gry” (`h:mm:ss`, 0x49ec00 forma 2), pod kreską Smash26 do prawej
  „Wyniki: %i  Forsa: %s” (kwota z 0x4a98f0: „$”, tysiące oddzielone spacją).
- **Ranga** (0x4e85d0): gdy wyniki i forsa są poniżej 50, a gracz jechał na więcej niż jednej trasie, „Opóźniony”;
  inaczej według wyników: < 100, 250, 500, 1000, 2000, 4000, 7000, 10000 i więcej to teksty 40551–40559.
- **Wybór gracza** (`dlgPLRSCOMM`, 0x4d1c43, obsługa 0x4d2400): bez przycisku wyścigu, „Kontynuuj” (0x5e2) wybiera
  zaznaczonego, zapisuje go w `RCCars.crs` i wraca. Lista (klasa LST, 0x4bcc60) w (0,10625; 0,38833)–(0,64375;
  0,85333) ekranu, wiersze od 0,06 jej szerokości, kolumny 0,3/0,2/0,3/0,2 („Gracz”, „Wynik”, „Forsa”, „Czas”),
  nagłówek 0,12 wysokości listy Smash26, wiersze po 0,105 Smash20, zaznaczony biały, inne szare, kreski pod
  nagłówkiem, pod każdym imieniem i pod listą, czerwony znacznik przed zaznaczonym (szary przed innymi). Zaznaczenie
  ustawia ostatni wyścig tego gracza (0x4d25f0).
- **Sortowanie** (0x5e5, porównanie 0x4d2110): stan 0x14a52f4 zaczyna od 0x11 (imię, rosnąco); każde naciśnięcie to
  następny: wyniki, forsa, czas gry (malejąco), imię. Napis przycisku mówi, co będzie następne (10025–10028).
- **Utwórz gracza** (0x5e3 → 0x4d2910): ekran przyciemnia się w 0,5 s, linia „Imię gracza:” (podział 0,46875), do
  10 znaków bez `\ / * & ? | .`, pigułki „Tak”/„Anuluj”. Gdy imię już jest: 41007, gdy nie da się założyć: 41008,
  gdy zapisać: 10884. Nowy gracz od razu gra, ma 5 okrążeń, a jego zdjęcie to pierwsze z listy, zmieniane strzałkami.
- **Zdjęcie** (0x5e6): strzałki obok zdjęcia (0x5dc) działają dla zaznaczonego gracza; od razu dostaje on zdjęcie
  wskazane przez strzałki i każda zmiana zapisuje plik (0x4d2710).
- **Usuń gracza** (0x5e4): bez zaznaczenia 41006, przy jednym graczu 41005, inaczej pytanie 41009 z „Tak”/„Anuluj”;
  usunięcie (0x4d1dbf, 0x4e8250) kasuje sam `.scp` (`.bak` zostaje), a gdy to był grający, nikt nie gra, dopóki
  „Kontynuuj” kogoś nie wybierze.
- **Okno komunikatu** (0x4cb830, 0x4cc0a0, rysowanie 0x4cb8c0): ekran ciemnieje w 0,2 s (do alfy 127), potem ramka
  z `messagebox.csi` 378x146 na środku (lewa część 0,16 ekranu, prawa jej lustrem, środek rozciągnięty), tekst
  Smash20 w górnych 82 jednostkach, pigułki 135x36: sama „Tak” na środku albo „Tak” i „Anuluj” 42 od boków.
- **Pasek przewijania** (klasa VSB, 0x4bdb20, `scrollbar.csi`): strzałki, tor i suwak; wygląd tylko przybliżony
  (u nas szare strzałki, gdy nie ma czego przewijać).

Po wyścigu (szybki wyścig to tryb 1, 0x4d2dd5):

- **Start** (0x4c9f70): graczowi zapisuje się trasa i auto, a w szybkim wyścigu także okrążenia i umiejętności.
  Auto musi być kupione: przycisk wyścigu szybkiego wyścigu jest wyłączony dla auta niekupionego i trasy
  niedostępnej (0x4c9e70), opis auta pokazuje wtedy samo „Nie dostępny” (41106, 0x4d4100).
- **Okrążenie** (0x4eb2b0 → 0x4c5560, tryby 1–5, 7, 8): lepsze od najlepszego na trasie staje się rekordem z autem,
  jego ulepszeniami z profilu i datą; czat 20100 „><imię> najlepsze okrążenie <czas>”.
- **Meta** (0x4c56c0, po „Wyjdź” ekranu mety, niżej): najlepszy wyścig na tyle okrążeń (czat 20101), czas wyścigu do trasy i do
  „Czasu gry” (gra liczy tylko ukończone wyścigi), +1 wyścig, przejechane metry (licznik auta car+0x5d38, 0x4f1c80,
  nie liczy przestawienia na trasę) do trasy i do gracza, zapis pliku. Miejsce, nagrody i rekord mistrzostw (0x0279)
  tylko w mistrzostwach (tryb 5). Wyścigu nie zapisuje się przy fladze 0x200 (zdarzenie 31 skryptu; u nas: gdy
  w wyścigu użyto F9 albo N z `-dev`). „Jeszcze raz?” wyścigu nie zapisuje (0x4c46c6: tylko 0x27e, wyścig od
  nowa); rekord okrążenia (0x4c5560) jest już w profilu.

## Po mecie

Stan wyścigu auta (car+0x5d3c, pole +4: 0 odliczanie, 1 wyścig, 2 meta) ustawia na 2 meta 0x4eaad0, która też
pisze w czacie 20106 i pokazuje komunikat 8 (napis „meta” z `msg_321_s_f`) na 2 s:

- **Napis** (0x4afbb0 z flagą 1, 0x4b02a5, 0x4b12b0): rośnie od zera do pełnej wielkości przez pierwsze
  min(2; 0,8) s, potem stoi do końca swoich 2 s i znika. Tabela przychodzi dopiero po 3 s.
- **Auto gracza** (0x4f6c39, co klatkę dla każdego auta): skończone nie ma klawiszy (car+0x574c zerowane)
  i trzyma `Brake` (0x5784 | 1, 0x5788 = 1), jak przy „Słabym sygnale”; toczy się i staje.
- **Przeciwnik** (car+0x50 == 1; 0x4f6d0e, 0x4f7341): po swojej mecie licznik car+0x128 rośnie; przez 2 s
  jedzie fizyką (0x4f5e50, jak zepchnięty z nagrania, flaga 0x4398), bez klawiszy, z ręcznym, a kąt kół 0x5c6c
  wraca do zera 120°/s. Potem nie ma ani fizyki, ani nagrania: stoi tam, gdzie wyhamował. Odstawianie na trasę
  (0x508160) działa dalej.
- **Strzałka kierunku** (0x4b0fa0, 0x4e94e0) nie pyta o metę, więc zostaje na ekranie.

## Ekran mety

`dlgFINISH` (tworzenie 0x4c4f90, obsługa 0x4c44e0, wypełnianie 0x4c47e0, układ 0x4c5b50, przyciski 0x4c5500):

- **Kiedy**: gdy auto gracza skończy wyścig (stan 2, 0x4a5100), gra uruchamia skrypt `LevelFinish` z `RCCars.sb`
  („3 ShowTable”), więc tabela pojawia się 3 s po mecie (zdarzenie 30 → 0x4de520 z 0x4c44e0; w trybach 7 i 8,
  grze w sieci, inna, 0x4cc670; wyścig duchów mety nie ma). W mistrzostwach przy wygranej na ostatniej trasie skrypt to `ChampionshipWin`. Wyścig w tle
  trwa: tabela co klatkę bierze kolejność z 0x4ea2d0 (skończeni po czasie, reszta po drodze).
- **Rysowanie** (0x15): ekran przyciemniony `0x7f000020`, ramka 0x4acb20 (`messagebox.csi`: rogi 32 piksele,
  UV 0x56cd38, prawe odbite, środek z teksela 0,4), potem ogólna obsługa okien 0x4de520 przyciemnia jeszcze raz
  `0x5f000000` i rysuje kontrolki. Ramka i kontrolki pojawiają się w 0,5 s (alfa okna 0 → 255) i znikają w 0,5 s
  po naciśnięciu przycisku.
- **Układ** z `Settings\dlgFINISH.ini` (jednostki 800×600): ramka 5,20 788×570; tabela 87,35 597×315, nagłówek
  10 %, wiersz 15 %, szerokości kolumn 8/19/9/16/14/14 % i reszta (20 %) dla ostatniej; „Wyjdź” (10004,
  pomarańczowy) 244,535 150×33, „Jeszcze raz?” (10067, czerwony, w mistrzostwach wyłączony) 15 jednostek na prawo,
  tak samo szeroki (`buttonRaceAgainSX` w pliku ma 34, gra go nie czyta). Do tego tabela premii 0x170f i napisy
  0x1710–0x1714 tylko w mistrzostwach.
- **Tabela** (klasa LST, flagi 0x500000: linia pod nagłówkiem i pod tabelą, znaczniki): nagłówek `Smash26`
  wyrównany do dołu, „Gracz” (41300) nad twarzą, nic nad imieniem (w sieci odwrotnie), dalej „Poz.”, „Czas”,
  „Dziura”, „Najl. okr.”, „śr. prędkość” (41301–41305) na środku. Wiersze `Smash20`: twarz (0x4c59f0: górne 158/256
  obrazka, wysoka na wiersz, szeroka na 0,5948 wysokości we frakcjach ekranu, na środku kolumny skróconej
  o `tableMordaShift` 0,009), imię podkreślone, miejsce, czas (forma 3, `%i:%02i.%02i`), różnica do gracza
  („+” za nim, „-” przed albo równo; u gracza „---”, 41306), najlepsze okrążenie (0..599,99) i średnia prędkość
  `%3.2f km/h` (41307). Wiersz gracza biały z czerwonym znacznikiem, inne szare (0xd0d0d0).
- **Średnia prędkość** (0x4eac90, ×3,6 w 0x4ea0f0): w trakcie (długość okrążenia × (okrążenie − 1) + droga na nim)
  / czas, przez pierwszą minutę mieszana z 7 m/s (waga czas/60); po mecie długość okrążenia × okrążenia / czas.
  Długość okrążenia (0x4e9560) to suma odcinków `cp_N` → `cp_N_M` … → `cp_1`.
- **Przyciski**: „Wyjdź” i klawisz akcji 12 (Esc; u nas też Enter) wygaszają okno i wysyłają 0x26b: okno się
  zamyka, wyścig się kończy (0x48c500(3)) i zapisuje (0x4c56c0). „Jeszcze raz?” wysyła 0x26d: okno się zamyka
  i 0x27e startuje wyścig od nowa.
- **Nie przeniesione**: tabela gry w sieci (0x4ccb40, `dlgMULTI_FINISH`). Premia mistrzostw opisana niżej.

## Mistrzostwa i garaż

Zasady są w `Scripts\championship.ini` (czytane do 0x149a670): `Common DefaultCash`, `Track<n>` (kolejność jak
`AIRace<n>`: Surferzy … Ścieżka wojenna) z `IsOpened`, `AccessCash` (potrzebne wyniki), `RaceTariff` (opłata),
`Place1..3` (nagrody), `Car<n>` z `IsEnabled` i `AccessCash` (cena) oraz `Boost-Car<n>`, `Resonator-Car<n>`,
`Tires-Car<n>` z `Level1..3`. Tryby gry (0x4a4ca0, globalna 0x149a674): 1 szybki wyścig, 4 trening, 5 wyścig
mistrzostw.

- **Trasa otwarta** (0x4e84a0): poza mistrzostwami (flaga 0x100 z 0x1499dd0) wystarczy `IsOpened`; inaczej wyniki
  ≥ `AccessCash`, a ostatnia trasa wymaga jeszcze wygranych (najlepsze miejsce 0) na wszystkich pozostałych (0x4e8580).
- **Ramka mistrzostw** (0x4be150): nagłówek 10051; przyciski: „Mistrzostwa” (radio 0x76f), „Mapa i informacje”
  i „Statystyki toru” (0x770, 0x771; strony z sekcji „Mapa i informacje, statystyki toru”), „Garaż” (0x772), „Nowe mistrzostwa”
  (0x773, pomarańczowy), „Trening” (0x774, pomarańczowy, aktywny, gdy trasa jest otwarta), „Menu główne”, „Wyścig”.
- **Strona tras** (`dlgCHAMP`, 0x4be720, wypełnianie 0x4be0a0): panel gracza; tabela 88,233 440×227, nagłówek
  16 %, wiersze 13 % (6 widocznych, pasek przewijania), kolumny 0,2/0,2/0,17/0,12/reszta, nagłówki 40900–40904
  Smash26; komórki Smash20: nazwa podkreślona, `AccessCash`, opłata (kwota), miejsce („xxx” 40905 dla zamkniętej,
  „bd” 40906 bez wyścigów albo poza 0..5, inaczej I–III 40912–14, IV–VI 40922–24), nagrody `%i/%i/%i`.
  Pod tabelą nazwa trasy (115,455 413×40, Smash26, podkreślona) i trzy linie do prawej (114,497 413×91): 40930
  (wyniki, opłata), 40931 (nagrody), a w trzeciej 40933 z miejscem albo 40932, dla zamkniętej 40935 (ostatnia trasa
  bez wygranych) albo 40934. Wejście na stronę z zamkniętą trasą wybiera ostatnią otwartą licząc od pierwszej.
- **Wyścig** (0x76d): zamknięta trasa → 40929 (ostatnia, nie wszystko wygrane) albo 40928; za mało forsy na
  opłatę → 40919; auto niekupione → 10883; inaczej okno opłaty. **Trening** (0x774): auto niekupione → 40916,
  inaczej od razu wyścig: 3 okrążenia, łatwi przeciwnicy, bez opłaty i nagród. Wyścig mistrzostw ma 5 okrążeń
  i przeciwników „Ekspert” (maska 4 w `ailayouts.ini`, 0x4a9770; szybki wyścig: umiejętność 0 → 1, 1 → 2, 2 → 4).
- **Nowe mistrzostwa** (0x773): pytanie 40927 „Tak”/„Anuluj”, potem 0x4e8320: forsa `DefaultCash`, wyniki 0, auta
  z `IsEnabled` bez ulepszeń, skórka 0; miejsca i rekordy tras zostają. Następnie wybrane auto to ostatnie kupione,
  trasa to ostatnia otwarta od pierwszej; zapis (błąd: 10884).
- **Okno opłaty** (`dlgCHRACE`, 0x4c0610, tabela 0x4c0b80, obsługa 0x4c0b10): menu przyciemnione `0x5f000000`,
  ramka 10,18 782×573; tabela 79,109 604×318 (nagłówek 10 %, wiersze 15 %, kolumny 0,09/0,19/0,21/0,17/0,17/reszta,
  `tableMordaShift` 0,008): „Gracz” (41405) nad twarzą, pusto nad imieniem, „Samochód”, „Silnik”, „Dopalacz”,
  „Koła” (41406–41409). Pierwszy wiersz to gracz (imię Smash26), dalej przeciwnicy AI1..AI5 trasy z bitem HARD:
  imię kierowcy (40499+numer), twarz, auto i poziomy („poziom %i” 41410, „brak” 41411; silnik to rezonator).
  Napisy (wszystkie 520×60 od x 140): 140,44 `%s: "%s"` (41402 i nazwa trasy, środek), 140,48 `%s: %s` (41403,
  `Place1`, do prawej i dołu), 140,416 `%s: %s` (41400, opłata), 140,446 `(%s: %s)` (41401, forsa po opłacie,
  Smash20). Pigułki „Wyścig” (czerwona, 257,530 134×36: opłata pobrana i zapisana, wyścig) i „Wróć” (414,530).
- **Po wyścigu** (0x4e07c0): z szybkiego wyścigu na jego stronę, z treningu i mistrzostw na stronę tras, po
  wygranej ostatniej trasy (flaga 0x14a3a88) na gratulacje (0x4c13a0, 0x4c1790): panel gracza, auto 291,255–694,577,
  „Gratulacje, %s” (44852, 112,253–522,286, Smash26 podkreślony, szary) i 44853 (112,340–330,515, Smash20),
  „Wróć” do menu głównego. Skryptu `ChampionshipWin`, który `LevelFinish` uruchamia w takim wypadku, w `RCCars.sb`
  nie ma (błąd oryginału: tabela się nie pokazuje); u nas tabela mety jest zawsze, a gratulacje po niej.
- **Nagroda** (0x4c56c0, tryb 5): miejsce < 3 daje `Place<miejsce+1>` + premię do forsy i do wyników; najlepsze
  miejsce to minimum; rekord mistrzostw (0x0279) zapisuje się, gdy go nie ma albo jego „czas” (obcięty do int)
  jest mniejszy od nagrody, a „czasem” jest nagroda.
- **Premia** (0x4c47e0, wynik w 0x14a3fa4, zerowana przy otwarciu tabeli): gdy gracz jest wśród trzech pierwszych
  i nie ostatni: 5 za każde uderzenie, które dało dopalacz, i 5 za każdą sekundę (zaokrągloną) przewagi nad
  następnym, razem najwyżej `Place3`. Na ekranie mety: tabela 85,399 611×55 (dwa wiersze po 0,5, w pierwszym:
  41314 z liczbą uderzeń, punkty, 41316 z sekundami, punkty; kolumny 0,32/0,08/0,13/0,39/0,08, Smash20), 41318
  z kwotą `Place3` (Smash26, środek), 173,320 456×108 `%s: %s` (41308 i nagroda miejsca; bez nagrody 41320),
  na dole „Wynik rajdu:” (41309, Smash26), suma nagrody i premii czcionką liczb kokpitu (`cock_num`) i „$/punktów”
  (`Currency` 0, 41404, Smash20), całość na środku ekranu. Polskie teksty 41314 i 41318 nie mają `%i`/`%s`, więc
  liczby się w nich nie pokazują (angielskie mają).
- **Garaż** (0x4c5f00, wspólne `dlgCARSCOMM`): nagłówek 10052, „Wróć” na poprzednią stronę (mistrzostwa albo
  szybki wyścig), bez przycisku wyścigu; radio „Wybierz samochód” (10059), „Ulepsz dopalacz” (10057), „Ulepsz
  silnik” (10058), „Ulepsz koła” (10056), ulepszenia wyłączone dla auta niekupionego. Wspólne: auto obracane
  0,19 504×403, „Forsa” (41501) i kwota, „Samochód” (41500), opis auta (40102+4·auto, 339,198 205×240, do prawej,
  szary). Auto pokazuje ulepszenia i skórkę gracza (0x4aca10), niekupione bez nich (0x50bb60).
- **Strona auta** (`dlgSETCAR`, 0x4d5330, 0x4d54f0): strzałki z autami (290,121 280×25; wybrane auto to auto
  wyścigu), „Posiadane ulepszenia” (40800), 40816 i 40817 z długimi nazwami ulepszeń („Dom.” 41800–41802 dla 0),
  cena 40809 (kupno) albo 40810 (sprzedaż: auto i ulepszenia), stan 40807/40808 dla niekupionego. Przyciski:
  „Następna skóra” (10012, 4 skórki `car_askin<auto><skórka>`), „Kup samochód” (10013: za mało forsy 40814, inaczej
  pytanie 40815 „Tak”/„Nie”), „Sprzedaj samochód” (10060: gdy to jedyne auto, a forsa po sprzedaży nie starczy na
  najtańsze inne, 40806; inaczej pytanie 40801; skórka zostaje).
- **Strony ulepszeń** (`dlgSETDETAIL`, 0x4d4690, stan 0x4d4f50, obsługa 0x4d4d40 i dwie podobne): auto bez strzałek,
  obrazek `upgr_boost|reson|tires<poziom>_<auto>` (350,412 197×149, UV 0,109–0,887 × 0,211–0,793), strzałki
  poziomu pod nim (na start min(poziom, 2)), nazwa części 40700–40702, opis: długa nazwa i opis poziomu (40112,
  40140, 40170 + 2·(3·auto + poziom − 1), opis +1). Wobec oglądanego poziomu v: posiadany < v „Nie dostępne”
  (40704, półprzezroczyste); = v cena (40703) i „Ulepsz” aktywne; = v+1 „ZAINSTALOWANE” (40705) i wartość
  sprzedaży (40706) i „Sprzedaj” aktywne; większy 40707. „Ulepsz”: 40710 gdy już jest, 40712 za mało forsy,
  inaczej pytanie 40709 z forsą po zakupie; kupione ulepszenie miga na aucie 5 s (co 0,2 s stare, 0x4d4a00).
  „Sprzedaj”: inny poziom niż oglądany → 40713 (strona kół na tym kończy, dopalacz i silnik idą dalej do pytania).
- **Ceny**: sprzedaż to `int(cena × 0,9f)` ($50 → $44). Silnik i koła mają ceny z `Resonator-Car<n>` (0x4d6840
  i 0x4d7130 pytają 0x4a0990); `Tires-Car<n>` czyta tylko konsola. Zostawiamy jak w oryginale.

## Wyścig duchów

„Wyścig duchów” (10050) w menu głównym to ekran 0x4d3000: ten sam co szybki wyścig (nagłówek 10050, radio
„Podsumowanie”, „Mapa i informacje”, „Statystyki toru”, zakładka „Garaż”, „Menu główne”, „Wyścig”), tylko zamiast
liczby okrążeń i umiejętności (0x4d3620) jest „Typ wyścigu:” (41116, `enumGhostType` 115,247 400×25, etykieta 0,5
szerokości, `Settings\dlgRACESUM_GHOST.ini`) z „Zwykły”/„Multi” (41112, 41113) i objaśnieniem pod nim
(`staticGhostTypeExplain` 102,271 215×117, Smash20, flagi jak opis auta; 41114, 41115). Wybór siedzi w `RCCars.crs`
`Player/GhostMode` (0 albo 1, nowy plik 0, 0x4e2660; zapis 0x4d3e60 przy wyjściu z ekranu). „Wyścig” (0x4d31a0)
ustawia flagę 0x40 i tryb 2 („Zwykły”) albo 0x80 i tryb 3 („Multi”).

- **Uczestnicy** (0x4a5320): tryb 2 to gracz i jego duch (typ 2, parametr 0); tryb 3 to gracz i duchy aut 0..2
  (typ 2, parametr 1..3), a duch innego auta niż gracza znika, gdy nie ma pliku. Plik (0x4a9170):
  `<GhostPath z Config.gm>\<GhostPath z AIRace<n>>\Track<n>_Car<m>.scg` (`.\GhostRecords\track1\Track1_Car1.scg`).
  Nazwa ducha to 40220 + auto („Duch - MT/TC/OT”, 0x49fb80). Bez przeciwników, bez miejsca w kokpicie.
- **Okrążenia** (0x4a4bc0): 19, ale wyścig się nie kończy (0x4eaa20 w trybach duchów nie woła mety), więc nie ma
  tabeli mety; wychodzi się z menu (Esc, „Przerwij”) na ekran duchów (0x4e07c0). Kokpit (0x4afdc0) pokazuje samo
  okrążenie (`%i` zamiast `%i/%i`) i czas bieżącego okrążenia (stan wyścigu +0x18) zamiast czasu wyścigu.
- **Duch** to auto (typ 2), które odtwarza okrążenie (`CarProfile`, ten sam format co `CarProfiles\*.dat`) z
  prędkością nagrania (0x503880: bez gumy). Nic go nie dotyka: zderzenia aut pomijają typ 2 (0x533940), ludzie
  i zwierzęta auta z car+0x4c (0x533970, ustawiane duchom w 0x4f27a0), strażnicy i ich cele typ 2 (0x5359f0,
  0x513100); nie da się go zepchnąć z nagrania (0x534880 z car+0x4c). Duch bez okrążenia jest ukryty (flaga 2 auta
  i jego części, 0x4f1dd0, 0x4f27a0). Duch z okrążeniem czeka na jego pierwszej klatce (car+0x44 = 0), a gdy je
  przejedzie, stoi na ostatniej.
- **Nagrywanie** (0x4f6ea0 → 0x502b90): auto gracza co krok, klatka gdy minęło ≥ 0,02 s od poprzedniej, najwyżej
  100000 klatek (0x501f80). Klatka (0x4f5180): czas, pozycja, kwaternion, prędkość, prędkość kątowa, 6 długości
  sprężyn (car+0x595c), 6 ich zmian (0x5970), kąt kół w stopniach (0x5c6c), 6 × 0x5974, klawisze (0x502b00).
- **Linia startu** (0x4fbeb0, licznik przejazdów stanu wyścigu +8): pierwszy przejazd (1, tuż za startem) tylko
  zaczyna nagranie i okrążenia duchów od początku. Każdy następny: duch gracza (duch auta gracza, 0x4f1dd0) się
  pokazuje; jeśli nie ma okrążenia albo to okrążenie jest szybsze od czasu jego ostatniej klatki, dostaje nagranie
  (zamiana buforów), wygląd auta gracza i następną skórkę (+1 z 4), komunikat 10 kokpitu na 2 s („najlepsze
  okrążenie”, 0x4afbb0) i czat 20100 z czasem. Potem wszystkie duchy z okrążeniem ruszają od pierwszej klatki,
  a nagranie zaczyna się od nowa. W „Zwykłym” pierwsze pełne okrążenie zawsze staje się duchem („Okrąż tor jeden
  raz, a potem walcz ze swoim najlepszym czasem”).
- **„Wróć na start”** (42001 zamiast „Wróć na tor”, 0x4c7850; 0x4c7690) i reset klawiszem (0x4f6b20, nie częściej
  niż co 1 s) w trybach duchów wołają 0x4f69e0: auto na pozycję startową, nagranie od nowa, stan wyścigu od zera
  (0x4e9ce0), duchy z okrążeniem stają na pierwszej klatce i czekają. Utonięcie, wyjazd poza tor i strefy resetu
  stawiają auto na ostatnim punkcie jak zwykle.
- **Wygląd** ducha z pliku (0x4f15c0): poziomy ulepszeń z nagłówka (chunk 0x63: +8 dopalacz, +0xc rezonator,
  +0x10 koła), skórka 0. Duch gracza bez okrążenia ma wygląd auta gracza.
- **Zapis** (0x4f17d0, przy niszczeniu aut na końcu wyścigu): w trybie 3 każdy duch z okrążeniem zapisuje je do
  swojego pliku (0x502010: `Time path`/`Distance` FULL i CYCLE od klatki 0, `Cycle NMB`, słowa 0x70–0x73, rozmiary
  0x24/0x88/0x0c, nagłówek 0x24 bajty, klatki, zdarzenia po 12 bajtów). W trybie 2 duch ginie z wyścigiem.
- **U nas**: nasze trasy nie mają przejazdu przez linię przy starcie, więc pierwszym przejazdem jest „Jazda!”,
  a „Wróć na start” i R pokazują odliczanie od nowa (bez sekundy przerwy); „Jeszcze raz?” zostawia duchy (zapisuje
  je w „Multi”) i zaczyna od odliczania. Duchy grają silnikiem jak przeciwnicy, kiedy jadą.

## Mapa i informacje, statystyki toru

Ramki szybkiego wyścigu, wyścigu duchów i mistrzostw mają tablice stron przełączanych przyciskami radio (0x56f58c
duchy: 0x4d3430, 0x4caa20, 0x4d8060; 0x56e040 mistrzostwa: 0x4be630, 0x4caa20, 0x4d8060): pierwsza to strona ramki,
druga „Mapa i informacje” (`dlgMAPINFO`), trzecia „Statystyki toru” (`dlgSTAT`). Przyciski po prawej zostają,
zmienia się tylko środek (komunikaty 0x26f/0x270 chowają i pokazują kontrolki strony w 0,5 s).

- **Mapa i informacje** (0x4cabc0 tworzy, 0x4cb540 układ z `dlgMAPINFO.ini`, 0x4cb080 wypełnia, 0x4cb2b0 zmiana
  trasy, 0x4cb4e0 zmiana zdjęcia). Poza grą sieciową (tryby 7, 8: czat zamiast zdjęć):
  - `shotTrackView` (0x839): `shot_<trasa>_L` (0x4a8ea0 z indeksem -1, `%s_L`) na 512x512 jednostek, cała tekstura.
  - `shotTrackEnum` (0x838): „<  >” z nazwami tras (40000+, 0x4a8a00); zmiana ustawia trasę ramki (0x4a4c50)
    i wysyła do niej 0x279 (stan przycisku „Wyścig”). W trybie 8 wyłączony.
  - `staticTrackInfo` (0x837): Smash20, szary 0xd0d0d0, opis 40030+trasa (piąty tekst 0x4a8a00).
  - `shotList` (0x834, klasa SHS 0x4b8fd0): 5 zdjęć `shot_<trasa>_0..4` (`%s_%i`), wybrane w ramce
    `shotList` (349,387 199x152), pozostałe w rzędzie między `BoundL`/`BoundR` (25..686) i `BoundUp`/`BoundDn`
    (402..478), co `Space` 5, w kółko (indeks modulo liczba zdjęć). Tekstura z ramką: UV 0x56edd8
    (0.109, 0.203)–(0.891, 0.793). Zmiana przesuwa rząd przez 0,17 s (0x4b9e90).
  - `enumShot` (0x835): „<  >” bez tekstu (5 pustych wartości), wybiera zdjęcie; `staticShortListText` (0x836)
    „Zdjęcia” (41200) Smash26 z linią.
  - `staticClosedInfo` (0x83a): „Zamknięty” (41201), gdy trasa niedostępna dla gracza (0x4e84a0).
- **Statystyki toru** (0x4d81a0 tworzy, 0x4d86c0 wypełnia, 0x4d8860 tabela, 0x4d8b80 sortowanie, układ
  `dlgSTAT.ini` przez 0x4d8cd0):
  - `staticTrackName` (0xb54) Smash26 z linią, `staticTrackInfo` (0xb55) Smash20 z opisem 40020+trasa, `shotTrack`
    (0xb56) `shot_<trasa>_0`, `staticClosedInfo` (0xb5b) „Nie dostępny” (40616) na trasie niedostępnej.
  - `tableStat` (0xb59, klasa LST): kolumny 0.34/0.30/0.36, nagłówek 0.12, wiersz 0.1, pasek przewijania 0.04;
    nagłówki „Gracz” (40601), wynik wg rodzaju (tablica 0x56fa20: 40614 „Wyniki”, 40602 „Okrążenie”,
    40603..40605 „3/5/7 okrążeń”), „Samochód” (40606). Wiersze to wszyscy gracze (0x14ac170).
  - `enumStatType` (0xb57) z „Posortuj wyniki według” (40600, `staticShowResult` 0xb58): 40615 „Wyniki” (tylko
    w ramce mistrzostw, flaga 0x100 0x1499dd0), 40607 „Okrążenie”, 40608..40610 „3/5/7 okrążeń”.
  - Rekordy trasy w pamięci profilu (+0x3d8 na trasę): +0x164 najlepsze okrążenie, +0x190 rekord mistrzostw (jego
    „czas” to wygrane pieniądze, pokazywane `%i`), +0x240/+0x298/+0x2f0 najlepszy wyścig 3/5/7 okrążeń. Wartość
    ≥ 10000 to „n/d” (40612, a w kolumnie auta 40611); czas w formie 3 0x49ec00 (`%i:%02i.%02i`), auto 40100+4·auto.
  - Sortowanie (0x4d8b80): czasy rosnąco, pieniądze mistrzostw malejąco (po `ftol`), remis wg imienia (`strcmpi`).
    Wiersz gracza grającego jest zaznaczony, a lista zaczyna się pół strony nad nim (+1).
- **U nas**: strony zmieniają się od razu (bez 0,5 s chowania kontrolek). Na mapie strzałki ← / → zmieniają
  zdjęcie, ↑ / ↓ trasę, kliknięcie zdjęcia z boku przesuwa je na środek; w statystykach ← / → zmienia rodzaj,
  ↑ / ↓ i kółko przewijają. Trasa zamknięta wybrana na mapie w mistrzostwach ustępuje ostatniej otwartej po
  powrocie na stronę tras (jak 0x4be720).

## Demo gry

- **Start**: „Demo gry” (0x649) albo 180 s w menu głównym bez naciśniętego przycisku: pokazanie menu (komunikat
  0x14, 0x4c9670) ustawia zegar komunikatu 0x26d na 180 s (nie w trybie deweloperskim sieci, 0x147d204),
  przycisk (0x7c) i 0x89 go kasują. 0x26d (0x4c9d6b): tryb 6 (0x4a4c90), trasa 0 (0x4a4c50), zapis `RCCars.crs`,
  potem 0x48c420("Beach_1", "DemoEntry"): mapa wczytywana ze skryptem wejścia `DemoEntry` zamiast `LevelEntry`.
- **Skrypty** (kompilacja 0x4e1080, wykonanie 0x4e1c20, linia `<s> <polecenie> [argumenty]`): `DemoEntry` nie ma go
  w `RCCars.sb`, tylko w „Scene Scripts” trzech tras: `beach_1` (`Set "NextDemoRace" = "Country_3"`, `EnterDemo`,
  `SetActive/SetFocus/CameraFollow '$CAR' 2`, `StartRace`, `100 LoadMap "Country_3" "DemoEntry"`), `country_3`
  (auto 3, dalej `Country_4` po 110 s) i `country_4` (auto 2, dalej `Beach_1` po 110 s). `EnterDemo` (31) ustawia
  flagę 0x200 w 0x1499dd0. `'$CAR' n` to adres `$CAR` o numerze n (0x4e18b0 typ 9, 0x4e23f0 → `gmpFindAddress`);
  bierzemy n jako numer auta w wyścigu od 0, czyli `Demo<n+1>`. Bez `Show3..Show1` wyścig rusza od razu.
- **Auta** (0x4a5320, tryb 6): pięć aut rodzaju 3 (demo), numery 1..5, bez auta gracza. 0x4a9630 czyta z
  `AIRace<n>` `Demo<i>Ref` (kierowca `AIPlayer<n>`, jego auto i skórka, 0x4a9ce0) i `Demo<i>Path` (nagranie
  `CarProfiles\Demo1(Surf)\over1.dat` …, po jednym pliku). Gumka (0x4fd5e0) mierzy odległość od auta aktywnego
  (0x4870c0), w demie od śledzonego.
- **Klawisze** (0x4c89a0, komunikat 0x8c z tablicą 0x148fbf8: akcja 11 Enter i Enter numeryczny, 12 Esc): Esc
  w demie zamyka mapę (0x48c500(2)) i wraca menu główne zamiast menu wyścigu; Enter wczytuje `NextDemoRace`
  (0x4047e0) z `DemoEntry`. Pauzy nie ma (0x4ca960).
- **Kokpit** (0x4afdc0): kokpit auta aktywnego; przez pierwsze 10 s (zegar 0x149dc14) w sekundach 1–2, 3–4, 5–6,
  7–8 i 9–10 napis 41903 „Wciśnij <Enter>, aby załadować następne demo...” w prostokącie 0.3,0.125..0.7,0.183
  ekranu (0x471380), do lewej.
- **Inne**: zderzenie dwóch aut (kule 0x533740) ma w demie promienie ×1,1; stan auta 0x50acf0 w demie przechodzi
  w 3 dopiero po 3 s. Wyniku w demie się nie zapisuje (flaga 0x200, 0x4c5583).
- **U nas** (`MainWindow::startDemo`, `tickDemo`): `DemoEntry` czytany z `SCRI` trasy (`rc::GameScript`), auto
  gracza trzymane nad trasą, poza wszystkim; kokpit pokazuje prędkość i okrążenie śledzonego auta, bez strzałki,
  miejsca i komunikatów wyścigu. Kule ×1,1 i opóźnienie stanu 0x50acf0 nie są przeniesione.

## Intro i ekran startowy

- **Kolejność startu** (0x4a2ae4, kroki 0x0f..0x13): intro (0x4a3030), potem ekran startowy, pod którym gra się
  wczytuje (0x4dd5e0), i menu główne. Intro nie gra w trybie deweloperskim (0x147d204) ani gdy zmienna skryptów
  `AutoRunIntro` jest 0 (rejestrowana jako 1 w 0x4902a2, `Config.gm` ustawia 1).
- **Scripts\intro.ini** (`INTRO_INFO`): `introName` = `"intro.dat"` (szukany w folderze rodzaju „Track”, 0x489390,
  czyli `Tracks\Intro.dat`), `introNum` = 3 (najwyżej 4; brak albo < 1: bez intro), `part_<n>` = od, do sekund
  (brak: 0, 0; do < 0: do końca filmu). Tu 0–10 s (logo 1C), 10–17 s (logo Creat Studio), 17,2 s do końca (film).
- **Film**: `Intro.dat` to zwykły strumień MPEG-1 (wideo MPEG-1 640x480, 25 kl/s; dźwięk MP2 44,1 kHz stereo;
  104,6 s). Odtwarza go DirectShow (0x4a20d0: FilterGraph, okno 800x600; 0x4a1eb0: `RenderFile`, okno filmu
  w kliencie okna gry). Części grają po kolei (0x4a1d80 `SetPositions` od, do), koniec części to `EC_COMPLETE`
  (0x4a2340) albo Esc (0x4a2f10 → 0x4a23b0, tylko w krokach 2–3: Esc kończy bieżącą część, nie całe intro).
- **Głośność** (0x4a1fe2): `IBasicAudio::put_Volume((SoundBkVolume − 100) · 40)` w setnych dB, od −3999 w dół
  −10000 (cisza), czyli (v − 100) · 0,4 dB; nie zależy od `Use_BkSound`.
- **Ekran startowy** (0x4a2ae4 krok 0x13, tablica 0x56b8a0: nazwa, x0, y0, x1, y1 jako części ekranu): `Desktop`
  (0, 0, 1, 1), `LogoRC` (0,16, 0,04, 0,8, 0,8933), `Logo1C` (0,03125, 0,01167, 0,19125, 0,225), `LogoCR`
  (0,80875, 0,01167, 0,96875, 0,225) i 41920 „Wczytywanie...” czcionką 0x1499d58 (0x4a17b7, siatka 25x28 jak Smash)
  w prostokącie 0, 390..640, 480 ekranu 640x480, flagi 0x110001 (0x10000 środek w poziomie, 0x100000 w pionie,
  0x4706a0). Obrazki są w `Language\<język>\` (`Desktop.csi` polski ma już małe loga 1C i Creat w rogu).
- **U nas** (`Intro`, `rc::Film` na pl_mpeg, `MainWindow::renderIntro`): film 4:3 na środku okna, dźwięk w miejscu
  muzyki; ekran startowy podczas wczytywania gry (`MainWindow::init`), loga w rogach przy krawędziach ekranu na
  szerokim ekranie. Intro nie gra też z `-l` (od razu wyścig). Czcionka napisu to Smash26.

## Opcje i twórcy

Przyciski menu głównego mają identyfikatory ekranów 0x641.. (0x4c9500), a 0x4ca290 wybiera ekran: 0x642 „Wyjdź”
pyta „Chcesz opuścić grę?” (10854, „Tak”/„Nie”, 0x4cb830), a „Tak” (0x64b) zamyka grę; 0x643
mistrzostwa (0x4be150), 0x645 wyścig duchów (0x4d3000), 0x646 gracze (0x4d1c10), 0x647 opcje (0x4d1980), 0x648
twórcy (0x4c3e40), 0x649 demo (komunikat 0x26d), 0x64a gra wielu graczy (0x4cd930).

- **Opcje** (0x4d1980, nagłówek 10044, „Menu główne” na dole): strony jako przyciski radio 0x6a7..0x6ad
  z tablicy 0x56f554. Każda strona czyta `RCCars.crs` przy pokazaniu i zapisuje przy zejściu (komunikat 0xf).
  Wiersze idą od góry co 25 jednostek wysokości z 10 przerwy (0.04167 i 0.01667 ekranu, 0x554758/0x55475c).
  Wyliczenie opcji ma etykietę 0..0.4 szerokości (drugie wywołanie 0x4b5920), a strzałki w środkowych 0.65 reszty
  (0x4b59e0). Wł./Wył. to 0x4b5bc0 („Wył.” 21 = 0, „Wł.” 20 = 1), Tak/Nie 0x4b5b60 („Nie” 13 = 0, „Tak” 12 = 1).
  - „Opcje wideo” (0x4c63f0, tablica 0x56eb10, od 0.14625, 0.225 do 0.67): rozdzielczość (tryby urządzenia od
    640x480, `VideoRace` `X/YResolution`), jakość kolorów (32 bpp 10088 / 16 bpp 10089, `VideoRace/BPP`), „Uruchom
    w oknie”, jakość tekstur (Niskie 2, Średnie 1, Wysokie 0), jakość renderingu i efektów (0, 1, 2), suwak
    „Gamma” 1..200 co 20 (tylko gdy urządzenie zmienia gammę). Pod nimi „Sprzętowy sterownik 3D” (0xd48)
    i nazwa sterownika (0xd49).
  - „Opcje kokpitu” (0x4c1b00, tablica 0x56e620, od 0.14, 0.2 do 0.65, etykieta 0.5, strzałki 0.6): `Cockpit`
    `ShowList`, `ShowMap`, `DirectArrow` i `Player/AutoChatPrior` (Krótki 10128 / Pełen 10129). Podgląd
    `opt_cock_all` w `shotlist` (290,280 256x196, górne 196 linii tekstury), nad nim części włączonych opcji
    (`opt_cock_l/m/a/c_0`), a skupionej `_1` migające (0x4c2090: okres 0,25 s, alfa sin·0.9+0.1).
    W wyścigu `ShowMap` pokazuje mapę (0x4b77e1), `DirectArrow` strzałkę i nawiasy (0x4af22e, 0x4b1d03),
    a `ShowList` okno listy graczy w grze sieciowej (0x4b0483).
  - „Efekty dźwiękowe” (0x4d7930, tablica 0x56f988, od 0.14625, 0.2 do 0.67125): `Use_Sound`, `SoundQuality`
    (Niskie 0, Średnie 1, Wysokie 1), suwak `SoundVolume` 0..100 co 10, `Use_BkSound`, suwak `SoundBkVolume`,
    `Snd_Style` (Rock 0, Techno 1, Oba -1), wiersz przerwy (0x27e4) i „Głośność ogólna” (mikser systemu). Bez
    dźwięku szare są jakość i głośności, bez muzyki jej głośność i styl. Styl wybiera utwory z `cycle` równym
    stylowi (0x486c00; -1 dowolne): z twórców wynika, że `cycle 1` to „Techno Deadushki”, `cycle 0` „Hard Rock
    Master”.
  - „Urządzenia wejściowe” (0x4c2470, `dlgCONTROL.ini`): dla gracza 1 i 2 (drugi 220 niżej) typ sterowania
    (`KbdJoyN`: Klaw. 0, Joystick/Kierownica 1, Gamepad 2), joystick („Brak” i urządzenia, `JoyNName`), suwaki
    czułości i martwej strefy (`JoyNSens`, `JoyNDZone`), szare bez joysticka.
  - „Dostosuj sterowanie” (0x4c2f20, `dlgCONTROL_PLAYER.ini`): „Pokaż sterowanie dla” (gracz 1 / 2), „Układ”
    (Użytk. 5, Klaw.1 0, Klaw.2 1, z joystickiem też 2..4) i osiem wierszy klawiszy (klasa HKY 0x4b67f0, tablica
    0x56e888: Gaz 10300, Wsteczny, Skręt w lewo, Skręt w prawo, Dopalacz, Skok, Hamulec, Zresetuj). Wiersz to
    etykieta do 0.34 szerokości i dwa klawisze (0.34..0.7, 0.7..1), nazwa 20 jednostek od lewej, „Brak” bez
    klawisza, czekający na klawisz czerwony; Delete czyści, Esc rezygnuje. Nowy klawisz znika z innych akcji
    gracza, a układ staje się „Użytk.” (0x4c32e0). Linie nad i pod wierszami to statyczny 0x2847 w `rectLines`.
    Pod nimi konflikt z drugim graczem dla skupionego wiersza (0x4c3b80): 10331/10332 z nazwą akcji albo „Brak
    konfliktów” (10330).
  - „Zaawansowane” (0x4c6de0, tablica 0x56eb70): potrójne buforowanie, suwak jaskrawości map 0..100, limit klatek
    („Bez limitu” 0, „%i FPS” 35..70 co 5). Pod nimi nazwa i opis skupionej opcji (0x277e, 0x277f; 10112..10114).
    Limit to zegar 1000 / limit ms, na który czeka każda klatka (0x48bc04).
  - „Wiadomości” (0x4bf6c0, `dlgOPT_CHATMSG.ini`): siedem pól tekstu (klasa EDT 0x4b3540) w
    `ChatMessages/Message1..7`, 60 znaków.
- **Klawisze w `RCCars.crs`**: sekcje `Player1`/`Player2`, rekordy typu 3 po 44 bajty (0x4e48a0): `[u32 1]`, potem
  dwa klawisze `[urządzenie][liczba kodów][3 kody]` (urządzenie 1 to klawiatura). Akcje 1..8 to Boost, Left,
  Right, Forw, Back, Jump, Brake, Reset (0x4e4180). Układ gracza w `Input/PlayerNLayout`. Układy Klaw.1 i Klaw.2
  (0x4e3110): Shift, strzałki, Spacja, B, R oraz Tab, A, D, W, S, Alt, Z, C. Kody klawiszy gry to nie kody
  DirectInput: tablica 0x49ca00 (Esc 1, F1.. 2, 1..0 0x12.., Q..P 0x20.., A..L 0x2e.., Z..M 0x3b.., Shift 0x3a,
  Ctrl 0x46, Alt 0x48, Spacja 0x49, strzałki 0x54..0x57). Prawe Shift/Ctrl/Alt dają kody lewych.
- **Twórcy** (0x4c3e40, nagłówek 43800 „Twórcy”): linie z `Language/Game` od 50001 (0x4c4160), strony kończą się
  linią „$”. Strona stoi 6 s, następna wchodzi na nią w 0,5 s (0x4c4210), po ostatniej wraca menu główne. Strona od
  (112, 92) w Smash26, linia co wysokość czcionki + 1 + 5; linia ze spacją na początku to nazwisko, inne są
  podkreślone (0x4c43c0). Logo w prawym górnym rogu (0x56e980: 0.84..1 × 0..0.213).
- **U nas**: rozdzielczość, kolory, tekstury, jakość, gamma, potrójne buforowanie, jaskrawość map, joysticki
  i wiadomości czatu są tylko zapisywane. Gra używa okna (przy następnym starcie), klawiszy gracza 1 według jego
  układu, głośności, muzyki i jej stylu, limitu klatek oraz mapy i strzałki kokpitu. „Głośność ogólna” zmienia
  głośność naszego miksera (nie systemu) i nie jest zapisywana. Lista trybów wideo jest stała, nazwy klawiszy są
  angielskie jak w DirectInput. Wartości spoza listy (np. `SoundQuality` 2) zostają w pliku, dopóki się ich nie
  zmieni.

## Dwóch graczy (podzielony ekran)

- **Menu**: „Wielu graczy” w menu głównym (0x4cd930, nagłówek 10131): „Utwórz grę” (0xedb, 0x4ce3e0) i „Dołącz do
  gry” (0xedc, 0x4cf960) to sieć (u nas wyszarzone), „Dwóch graczy” (0xedd, 10130) w slocie 3, „Wyścig” wyłączony
  (0xed9), panel gracza (0x4a7170). Pokaz (`dlgMULTIPLAYER.ini`): zdjęcia tras jak w menu głównym, następne co 4 s
  (0x5543f0), po czterech następna trasa; wtedy, jeśli pokazywane były zdjęcia, z szansą 1/5 (`rand() % 5 < 1`)
  w ich miejscu przez tyle samo kręci się auto (`animPrevCar` 188,197 320x240, seryjne, 0x50bb60) z nazwą i opisem
  (40100 + 4·auto, + 3; 0x4ce360), inaczej następne auto i zdjęcia z nazwą trasy i tekstem z menu głównego
  (40000 + t, 40040 + t; 0x4ce2d0).
- **„Dwóch graczy”** (0x4d8d30): „Dalej” 10069 (0x1069 → 0x4bfa80), „Wróć” 10003; strony jako przyciski radiowe:
  10015 „Podsumowanie” (0x4d8fe0), 10033 „Sterowanie” (0x4c23f0, strona opcji „Urządzenia wejściowe”), 10035
  „Dostosuj sterowanie” (0x4c2e60, ta z opcji). Wejście czyta `RCCars.crs` `TwoPlayers` (0x4a4d40, 0x4a4ff0), wyjście
  zapisuje (0x4a4e30, 0x4a5060).
- **Podsumowanie** (`dlgTWOPLAYERS_RACESUM.ini`, tablica 0x56fb18): trasa jak w szybkim wyścigu (`shotTrack`,
  `enumTrack`, nazwa, opis 40020 + t; 0x4d9710), `editRaceMode` (0x1324, 42800 „Tryb wyścigu:”: 42803 „Okrążenia” =
  2, 42804 „Opóźnienia” = 1), `editNLaps` (0x1325, 42801, „%i” z 1..5, 0x56fbd4) albo `editMaxDist` (0x1326, 42802,
  „%i m” z 200/400/600, 0x56fbc8) w tym samym miejscu, zależnie od trybu (0x4d95d0), `staticRaceMode` (Smash26,
  42803/42804) i `staticExplain` (42808/42809). Zapis: `lagType` 2 i `fixedNLaps` albo 1 i `lagMetre`; tryb gry 9 gdy
  `lagType` = 2, inaczej 10.
- **Auta** (0x4bfa80, `dlgTWOPLAYERS_CHOOSE2CARS.ini` + `dlgCARSCOMM_SPLIT.ini`, tablica 0x56e2c0): dla każdego gracza
  `enumCar` (nazwy aut), `enumBoost0`, `enumReson0`, `enumTires0` (43400..43402, wartości 43403..43406 „dom.”, „Poz.
  1..3”; dowolne, nie z garażu), podgląd `animCarPreviewSplit` i „Samochód” (41500); drugi gracz o `shift2` = 202
  niżej (wpisy `enum*1` z pliku są nieużywane: obaj biorą `*0`). „Następna skóra” (10012) w slotach 0 i 5: skóra
  (s + 1) % 4. `TwoPlayers` `car1/2`, `boost1/2`, `reson1/2`, `tires1/2`, `skin1/2`. „Wyścig” (0x15e1, 0x26d) zapisuje
  i wczytuje trasę ze skryptem `2PLEntry`; po wyścigu powrót na „Dwóch graczy” nad „Wielu graczy” (0x4e07c0).
- **Wyścig** (0x4a5320): dwa widoki (0, 0)–(640, 235) i (0, 245)–(640, 480) ekranu 640x480, czarny pas między nimi,
  dwa auta graczy (rodzaj 0, id 8 i 9), bez przeciwników. Kamera (0x4a5d90): `Camera.ini` `splitScrAngle` 62° (kąt
  pionowy) i `splitScrAspect` 45 (0,45 wys./szer. rzutu w widoku 235/640, 0x405e80). Tryb 9: `fixedNLaps` okrążeń
  (0x4a4bb0); tryb 10: okrążenia bez końca (0x7fffffff), a gdy |odstęp| po trasie (0x4ea7b0) ≥ `lagMetre`, licznik
  0x14b0990 rośnie o dt (zerowany tylko na starcie); po 7,5 s meta obu (0x4eab80), kolejność wg pozycji (0x4ead70).
  `LevelFinish` dopiero gdy obaj skończyli (0x4a5100). Bez strzałki 3D i listy.
- **Kokpit** (0x4afdc0 z widokiem, 0x4b09a0): pozycje `<nazwa>_0/_1` z `cockpit.ini`, `cockpit_time.ini`, `map.ini`
  (`mapPosY_1` = 401) i `cockpit_split.ini` (`msgOffsetY_0/_1`: komunikaty 0x4b11e0 w pełnym rozmiarze, środek −89 od 1/4
  i −70 od 3/4 wysokości ekranu, czyli 61 i 380 z 600); numer okrążenia „%i/%i”, w trybie 10 samo „%i”;
  czas całkowity tylko w kapsule `split_uzel` paska. Pasek: szary cały, czerwony 0..min, żółty min..max; tryb 9
  postęp = droga/(okrążenia · długość okrążenia) z kreskami okrążeń, tryb 10 f = odstęp/`lagMetre`, min/max =
  0,5 ∓ |f|/2, prowadzący na max; znacznik gracza 1 w dół, gracza 2 w górę.
- **Prędkościomierz** (0x4b1610, 0x4b3100, 0x4b32f0): tarcza `split_sp1` 255x255 z 640x480 przy lewej krawędzi,
  `indicatorShiftX/Y` − 200 (+300 lub +600 w jednostkach 800), lustrzana; `split_sp2` czerwona wartość, `split_sp3`
  żółty zasięg, `split_sp4` kapsel i obejma. Dopalacz od −34° do 126° (skala 100), prędkość od −54° do −213° (skala
  50 km/h), środek pierścienia (128, 128,5); napisy „dopalacz” (41902) i „km/h” (41901), liczby w `cock_num`.
- **Napisy na tarczy** (0x4b32f0): odstępy w pikselach ekranu (dzielone przez 0x5647f0/0x5647f4, szerokość
  i wysokość okna), nie w 640x480. Kreska 2 px pod środkiem tarczy, 70 px od jej boków; liczba dopalacza do prawej
  3 px za środkiem kreski, dołem na kresce (flagi 0x220000), „dopalacz” od 2 px za środkiem, dołem 3 px nad kreską
  (0x200001); prędkość (0x20000) i „km/h” (1) górą 9 px pod kreską. Flagi tekstu (0x4706a0): 0x10000/0x20000
  środek/prawo, 0x100000/0x200000 środek/dół. Wysokość wiersza = wiersze czcionki od–do (0x46ffa0, 0x4704c0):
  `cock_num` 16..55 komórki 64 (40 px), Smash26 5..25 (21 px), Smash20 0..27; litera rysowana od górnego wiersza.
- **Czas w kapsule** (0x4afdc0): `cock_num` wyśrodkowany w obie strony w x 4 px .. węzeł − 16 px, y ±32 px wokół
  środka ekranu + `timeTotalPosY_0`. Węzeł ±30x±32 px, pasek ±8 px, 32 px od prawej krawędzi, znaczniki ±16 px.
  Widoki: nad i pod 5 px wokół środka ekranu (0x4a5320, przy 640x480 to 235 i 245).
- **Tabela mety** (0x4a5970): „Gracz1” i „Gracz2” (40508, 40509) bez zdjęć, odstęp względem gracza 1.
- **U nas** (`game/twoplayers.cpp`, `game/menutwo.cpp`, `MainWindow::render`): drugi gracz ma własne auto, wyścig,
  kamerę, dźwięk, efekty i kokpit (`Hud::setView`), klawisze z `Player2Layout`/`Player2` (domyślnie Klaw.2: Tab, W A
  S D, Alt, Z, C); zderzenia aut graczy jak z przeciwnikami. Postęp na trasie liczy `RouteTracker`.

## Gra sieciowa

Oryginał gra przez DirectPlay 8 w trybie peer (`CoCreateInstance(CLSID_DirectPlay8Peer)`), z dostawcami TCP/IP
i łącza szeregowego. Kto zakłada grę („Utwórz grę”), jest w trybie 7, kto dołącza („Dołącz do gry”), w trybie 8.
RCCars.crs, sekcja `Network`: `portNum`, `maxPlayersNum` (6), `sessionName`. Nie ma serwera z listą gier: dołącza się
po adresie gospodarza, a przez internet gospodarz potrzebuje publicznego IP albo przekierowanych portów.

- Wiadomości: ok. 15 rodzajów (id 0x3ef..0x403) zarejestrowanych przez 0x491c80, wysyłane przez 0x491fb0/0x491f10
  do `netPlayerSendTo`. Rodzaj 0: stan własnego auta, 35 bajtów (0x4f4a60); 1: zderzenie (0x4f5010); 3: stan
  rekwizytów od gospodarza (0x52db29), stosowany przez klientów w trybie 8 (0x52db60); 6: czat, 76 bajtów.
- Każdy gracz rządzi swoim autem i wysyła jego stan 4, 6, 10, 15 albo 25 razy na sekundę, zależnie od wybranego
  łącza (0x571cac). Gospodarz rządzi rekwizytami.
- Gracz z opóźnieniem większym niż pozwala gospodarz (maks. opóźnienie 10..1000 ms w `dlgNETCREATE_OPTS`) jest
  odrzucany: 42453 „Lag time more than server access”. Utrata połączenia: 20233 „Connection lost”.
- Ekrany: `dlgNETCREATE_OPTS`, `dlgNETCREATE_PLRINFO`, `dlgNETJOIN_OPTS`, `dlgNETJOIN_SERVINFO`,
  `dlgWAITPLAYERS_RACESUM` / `_CARSETUP` / `_CARRESTR` / `_CHAT`, skrypt wyścigu `NetEntry`, tabela `dlgMULTI_FINISH`.
- Wiatr rekwizytów (0x50d3f0) działa tylko w trybach 7 i 8 (0x4a0b00).

U nas (`net/session.*`): własne sesje na ENet po UDP, bez zgodności z RCCars.exe. Gracze nie łączą się każdy z każdym,
tylko z gospodarzem, który przekazuje ich wiadomości dalej: jeden port do otwarcia na jednym komputerze. Wiadomości
niezawodne (poczekalnia, czat, zderzenia, meta) idą kanałem 0, szybkie (stan aut) kanałem 1, gdzie zgubiony pakiet
zastępuje następny. Gospodarz przez 2 s mierzy opóźnienie dołączającego (czas w obie strony) i odrzuca go jak
oryginał, gdy jest za duże. Domyślny port 2302 (pierwszy port sesji DirectPlay 8), gdy RCCars.crs go nie ma.
Test bez gry: `rcinfo . net`.

## Tryb deweloperski

`RCCars.exe -dev` (0x4883dd ustawia flagę 0x147d204, w logu „-dev mode on”): klawisz F9 (0x48c0e3) przełącza flagę
0x147d254, F11 zamyka okno; wczytuje skrypt `debug.gm`, pomija przy starcie kilka kroków (m.in. styl dźwięku
`Snd_Style`), dodaje polecenia skryptów (`GetInfo`, `GetStat`, `Give`, `Buy`, `EmRace`) i wpisywane kody (`stat`,
`dt0`–`dtx`, `burst`, `road`, `constr`, `C1`, „ZERO” `F`/`E1`/`E2`/`I`) i pozwala wczytać trasę wprost z instancją
`Player`. Klawisze u nas: [README](../README.md#tryb-deweloperski).

- **Lot F9**: auto gracza nie przechodzi przez fizykę (0x4f39e0 → 0x482900): w pierwszej klatce staje prosto, przodem
  tam, dokąd jechało po ziemi, a potem przesuwa się i obraca o `ZeroSpeed`·dt i `ZeroAngle`·dt (sekcja „Debug”,
  domyślnie 100 i 90, `Autoexec.gm` ustawia `ZeroSpeed 15`) klawiszami z 0x482320 (↑↓ przód/tył, ←→ obrót, Alt+↑↓
  góra/dół, Alt+←→ oraz `,`/`.` w bok, A/Z pochylenie, End poziomo, Shift ×2). Kamera ma wtedy sprężyny bez luzu:
  1,0443 m za autem i 0,409 m nad nim (0x500ce9), bez podnoszenia przez przeszkody (0x501183) i bez pilnowania wody;
  0x508160 nie odsyła auta na punkt kontrolny.
- **Kody** (0x47a660 je rejestruje, 0x47a6b0 sprawdza każdy klawisz): litery i cyfry wpisane po kolei, wielkość liter
  bez znaczenia, najwyżej 2 s między nimi (0x47a7a0); zły klawisz zaczyna kod od nowa. `dtN` (0x532490, 0x531cb8)
  trzyma teksturę brudu nr N (najwyżej ostatnią, u nas poziom 2), `dtx` puszcza. `constr` (0x4a4710) rysuje obrysy
  ścian trasy (0x4a40c4; przyjmujemy, że to siatka kolizji). Kamera „ZERO” (0x482cd0, 0x482eb0) używa tego samego
  sterowania co lot F9 (0x482900): `E1` daje je kamerze widoku 0, `E2` kamerze widoku 1, a widok 0 zostaje bez
  sterownika (u nas jeden widok, więc kamera stoi), `C1` (0x501a60) przywraca kamerę wyścigu `RAC$`, `I` tworzy osobną
  kamerę i zatrzymuje czas gry (licznik pauzy 0x710b74), `F` przy zatrzymanym czasie woła jedną klatkę (0x48bce0,
  `FixedTime` z trybem dev). `stat` (0x4eae30) przełącza flagę, według której 0x4eaf03 rysuje punkty kontrolne:
  korytarze trasy między znacznikami i kule wokół głównych punktów (1,6 m, którą auto musi dotknąć, czerwona, oraz
  4 m i 1,12 m); u nas włącza te same kule co G.
- **Kiedy kody działają**: w oryginale kody kamery sprawdzają flagę ZERO (0x1481fc4), którą system `ZERO` (0x482230)
  włącza na `GMP_INIT_MAP` i wyłącza na `GMP_TERM_MAP`, czyli przez cały czas wczytanej trasy; wiadomość 0x83
  z interfejsu (0x4c73c9, 0x4ca9a1) wyłącza ją na czas ekranów menu (przypuszczenie z kontekstu). U nas kody nie
  działają, gdy otwarte jest menu. `debug.gm` czytamy tylko po `Set ZeroSpeed/ZeroAngle/FixedTime`.
- **Licznik FPS** (0x4a3c3c, `game/fpscounter.*`): gdy zmienna skryptów `ShowFPS` (rejestrowana z 0 w 0x490383,
  ustawiana np. `Set ShowFPS 1` w `Config.gm` albo `Autoexec.gm`; działa też bez `-dev`) nie jest zerem, gra co klatkę
  wypisuje w warstwie napisów debugowania (0x148fae8, 9 miejsc na ekranie, 0x480020) przy lewej krawędzi w połowie
  wysokości `FPS:%5.1f TRS:%4.1f+%4.1f+%4.1f=%4.1fms P/S:%6.1fK`, w lewym górnym rogu
  `P/F:%4.1fK VVM:%5.2fM(%5.2fM) T/F:%5.2fM` i niżej `TVM:%5.2fM(%5.2fM)` (wielokąty na klatkę, pamięć wideo
  i tekstur). Liczby są średnimi z co najmniej 1 s (0x554390): klatki i wielokąty (licznik 0x14761f4, 0x462a40)
  zliczone, podzielone przez czas, liczone od nowa, gdy czas przekroczy sekundę; FPS idzie też do zmiennej
  `gmdShowFPS`. Czcionka warstwy to `albet16` (16x16, 0x48f2a7), której nie ma w plikach gry. U nas tylko pierwsza
  linia bez P/S, czcionką Smash20 na przyciemnionym pasku, a T, R i S to nasze czasy (logika gry; czekanie na klatkę
  w locie i nagranie rysowania; wysłanie i pokazanie klatki), bo znaczenie oryginalnych nie jest rozpracowane. F12
  przełącza licznik (nasz klawisz).
- **Nie przeniesione**: polecenia skryptów kariery (`GetInfo` i `GetStat` wypisują profil gracza: kasę, wynik, auta
  z ulepszeniami, otwarte wyścigi; `Give`, `Buy`, `EmRace`), bo nie ma jeszcze profili ani mistrzostw, oraz kody
  `burst`, `road` (i drugi `constr`), które przełączają flagi, których nikt nie czyta (`burst`: 0x4fd420 → 0x5725e8).
