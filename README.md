# Flight Viewer: wizualizacja rosbagów dla ludzi nietechnicznych

Prosta aplikacja, w której przeciągasz/wybierasz nagranie lotu (rosbag ROS 2) i
oglądasz lot w 3D z osią czasu, trybami lotu, podglądem kamer i opisem „po ludzku”,
bez ROS-a, RViza i terminala.

**Status:** działający prototyp (wersja deweloperska w przeglądarce). Czyta bagi z lotów
w hali (MAVROS, bez GPS). Aplikacja desktopowa (AppImage) i mapa dla lotów z GPS są
jeszcze do zrobienia, szczegóły w [Co zostało](#co-zostało).

---

## Spis treści
1. [Co to robi](#co-to-robi)
2. [Szybki start](#szybki-start)
3. [Jak to działa](#jak-to-działa)
4. [Jakie dane są czytane z bagu](#jakie-dane-są-czytane-z-bagu)
5. [Pliki i konfiguracja](#pliki-i-konfiguracja)
6. [Praca z dużymi bagami i z pendrive'a](#praca-z-dużymi-bagami-i-z-pendrivea)
7. [Testy](#testy)
8. [Co zostało](#co-zostało)
9. [Podjęte decyzje](#podjęte-decyzje)
10. [Znane ograniczenia i pułapki](#znane-ograniczenia-i-pułapki)

---

## Co to robi

| Funkcja | Stan |
|---|---|
| Lista nagrań z folderu, z czasem trwania i informacją, czy mają pozycję | działa |
| Scena 3D: siatka, kompas N/E/S/W, model drona z orientacją, ścieżka kolorowana trybem lotu | działa |
| Odtwarzanie: play/pause, prędkość 0,5×–8×, suwak czasu, pasek trybów lotu | działa |
| Panel po ludzku: tryb lotu słowami, wysokość, prędkość, odległość od startu, bateria, krok misji | działa |
| Kamery (dolna/przednia) zsynchronizowane z czasem | działa |
| Detekcje YOLO jako tekst w panelu | działa (tylko napis, bez ramek) |
| Zadany cel (setpoint) jako żółta kula, ścieżka LIO (opcjonalnie) | działa |
| Widoki kamery: swobodny / podążanie / z góry | działa |
| Czytelne błędy zamiast stack trace’ów (pusty, uszkodzony, bez metadanych) | działa |
| Ostrzeżenia: brak pozycji, dron się nie ruszył, LIO zgubiło śledzenie | działa |
| Bagi z PX4 (`/fmu/out/*`, mcap): pozycja NED→ENU, orientacja, GPS, tryby z `vehicle_status` | działa (sprawdzone na nagraniu z symulatora) |
| Mapa CesiumJS + Google Photorealistic 3D Tiles (przez Cesium ion) dla lotów z GPS | napisana, **nieprzetestowana w przeglądarce** |
| Aplikacja desktopowa (Electron → AppImage) | **nie ma** |

## Szybki start

Wymagania: Ubuntu 22.04/24.04, Python 3.12. Node.js **nie jest** potrzebny do uruchomienia
(będzie dopiero do aplikacji desktopowej).

```bash
cd rosbag-viewer
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt

# serwer, wskaż folder z bagami (każdy bag to podfolder z metadata.yaml i plikiem .db3)
.venv/bin/python backend/server.py --bags /ścieżka/do/bagów
```

Otwórz w przeglądarce **http://127.0.0.1:8765** i wybierz nagranie z listy po lewej.
Link do konkretnego momentu: `http://127.0.0.1:8765/?bag=<nazwa_folderu>&t=<sekundy>`.

Skróty: **spacja** = play/pause.

Domyślny folder bagów to `../bags` (obok katalogu projektu) albo zmienna `ROSBAG_DIR`.
Lokalny folder `bags/` został usunięty (brak miejsca na laptopie): wszystkie bagi leżą
na pendrivie w `ROSBAGS/IMAV-26/`, więc zawsze podawaj `--bags`.
Serwer słucha tylko na `127.0.0.1` i odmawia dostępu do ścieżek spoza wskazanego folderu.

## Jak to działa

```
 bag (sqlite3 / mcap)                                    przeglądarka
        │                                                      ▲
        ▼                                                      │ JSON + JPEG
 backend/bag_reader.py ──► backend/server.py (FastAPI) ────────┘
   rosbags: czyta tylko              /api/bags    lista nagrań (z metadata.yaml)
   potrzebne topiki                  /api/flight  cały lot jako mały JSON
        │                            /api/frame   jedna klatka z kamery (JPEG)
        ▼                            /            frontend (statyczne pliki)
 ~/.cache/rosbag-viewer/flights/*.json   ← cache wyniku (kilkadziesiąt KB na bag)
```

- **Backend (Python)** otwiera bag, wybiera potrzebne topiki i zapisuje lot jako zwarty JSON
  (pozycja zredukowana do ≤20 Hz, zmiany trybów, bateria, prędkość, detekcje,
  znaczniki czasu klatek). Obrazy **nie** są w JSON-ie, pobiera się je pojedynczo
  na żądanie.
- **Frontend (three.js, zwykłe moduły ES, bez bundlera)** rysuje scenę. Dane zostają w układzie
  ENU z MAVROS (x=Wschód, y=Północ, z=Góra), a obraca się tylko grupa główna, więc nie ma
  przeliczania punktów. three.js jest wgrany lokalnie w `frontend/vendor/`, więc aplikacja
  działa bez internetu.
- **Wydajność:** klatki są czytane prosto z SQLite po indeksie czasu (0,1 ms po rozgrzaniu),
  przeglądarka wysyła najwyżej jedno żądanie na kamerę naraz, a wyniki są cache’owane
  w pamięci i na dysku. Szczegóły i historia problemu: sekcja [Ograniczenia](#znane-ograniczenia-i-pułapki).

## Jakie dane są czytane z bagu

Tylko **standardowe typy ROS** (bez `mavros_msgs` i `px4_msgs`, więc nie trzeba ich definicji).
Dla każdej grupy używany jest pierwszy topic, który ma dane.

| Co | Topiki (w kolejności) | Typ |
|---|---|---|
| Pozycja + orientacja | `/mavros/local_position/pose`, `/mavros/local_position/odom`; zapasowo PX4: `/fmu/out/vehicle_local_position[_vN]` + `vehicle_attitude` | PoseStamped / Odometry / px4_msgs |
| GPS (mapa) | `/mavros/global_position/global` (NavSatFix) albo `/fmu/out/vehicle_global_position[_vN]` | NavSatFix / px4_msgs |
| Tryb lotu (PX4) | `/fmu/out/vehicle_status[_vN]` (`nav_state`, `arming_state`) | px4_msgs |
| LIO (drugi tor, opcjonalny) | `/Odometry` | Odometry |
| Tryb lotu | `/flight_mode`, `/drone/flight_mode`, `/diagnostics/flight_mode` | String |
| Krok misji | `/drone/course_state` | String |
| Zadany cel | `/drone/setpoint_pose` | PoseStamped |
| Prędkość | `/mavros/local_position/velocity_local` | TwistStamped |
| Bateria | `/mavros/battery` | BatteryState |
| Detekcje YOLO | `/mission_2/yolo/detections` (JSON w polu `data`) | String |
| Kamery | każdy topic kończący się na `image_raw/compressed` | CompressedImage |

Uwagi o danych:
- Tryb lotu bywa publikowany jako `"OFFBOARD\nArming denied: …"`. Pierwsza linia to tryb,
  reszta idzie do osobnej listy komunikatów statusu.
- Czas na osi to czas zapisu w bagu, liczony od początku nagrania.
- Wszystkie bagi, na których testowaliśmy, są **bez GPS-u** (loty w hali, LIO/FAST-LIO).
  Czy w surowym MAVLinku (`/uas1/mavlink_source`) nie ma ukrytych ramek GPS, **nie sprawdzono**.

## Pliki i konfiguracja

```
rosbag-viewer/
├── backend/
│   ├── bag_reader.py    czytnik bagów → JSON lotu, klatki z kamer, cache, komunikaty błędów
│   ├── server.py        API HTTP + serwowanie frontendu (tylko 127.0.0.1)
│   └── warm_cache.py    jednorazowe przetworzenie wszystkich bagów z folderu
├── frontend/
│   ├── index.html, style.css, main.js   widok 3D, HUD, oś czasu, kamery
│   └── vendor/three/                    three.js 0.160.0 (lokalnie)
├── tests/               29 testów (pytest), syntetyczne bagi, żadnych danych z lotów
├── requirements.txt
├── .env.example         szablon kluczy (prawdziwe klucze → .env.local, ignorowany przez git)
└── README.md
```

Zmienne środowiskowe:

| Zmienna | Znaczenie |
|---|---|
| `ROSBAG_DIR` | domyślny folder z bagami (nadpisywany przez `--bags`) |
| `ROSBAG_VIEWER_CACHE` | gdzie trzymać cache lotów (domyślnie `~/.cache/rosbag-viewer/flights`) |
| `VIEWER_TOKEN` | jeśli ustawiony, API wymaga nagłówka `X-Viewer-Token` (dla Electrona) |
| `GOOGLE_MAPS_API_KEY`, `CESIUM_ION_TOKEN` | **na razie nieużywane**, przygotowane dla warstwy mapy |

**Klucze:** trzymaj je w `.env.local` (uprawnienia 600, w `.gitignore`). Nigdy w repozytorium
ani w AppImage. Do zdjęć fotorealistycznych wystarczy token Cesium ion (asset `2275207`);
klucz Google jest opcjonalny.

## Praca z dużymi bagami i z pendrive'a

Bagi mają od kilkuset MB do kilkunastu GB (głównie obrazy), więc **nie trzeba ich kopiować na laptop**.

1. Włóż pendrive i uruchom serwer ze wskazaniem folderu na nim:
   `python backend/server.py --bags /media/<user>/ROSBAGS/IMAV-26`
2. Pierwsze otwarcie bagu czyta cały plik sekwencyjnie. Zmierzone z pendrive'a: od kilku
   sekund do ok. minuty (bag 18 GB: 57 s; 1,2 GB zimny odczyt: 27 s, bo zależy od cache
   systemu plików). Wynik trafia do cache na laptopie (kilkadziesiąt KB) i **kolejne
   otwarcia są natychmiastowe**.
3. Żeby nie czekać przy pierwszym otwarciu, przetwórz wszystko z góry:
   `python backend/warm_cache.py /media/<user>/ROSBAGS/IMAV-26`
   (można w tle: `nice -n 15 ionice -c3 …`).
4. Pendrive musi zostać włożony: klatki z kamer są czytane z oryginalnych bagów.

Cache jest unieważniany, gdy zmieni się `metadata.yaml` albo rozmiar plików bagu.
Wersję formatu podnosi stała `CACHE_VERSION` w `bag_reader.py` (zrób to przy każdej
zmianie kształtu danych lotu).

## Testy

```bash
.venv/bin/python -m pytest        # 29 testów, ~2 s
```

Testy budują własne, malutkie bagi (`tests/conftest.py`), więc nie potrzebują prawdziwych nagrań.
Pokrywają: pozycję, rozdzielanie trybów od statusów, ostrzeżenia, pusty/uszkodzony/niekompletny
bag, klatki z kamer (w tym czasy poza zakresem), cache na dysku, serwer (zakaz ścieżek spoza folderu,
token, kody błędów).

**Nie ma testów frontendu.** `main.js` był sprawdzany tylko ręcznie w przeglądarce.

## Co zostało

Kolejność od najważniejszego.

**1. Prawdziwe loty (blokuje punkt 3).** Wszystkie dostępne bagi to loty w hali bez GPS-u.
Potrzebny jest co najmniej jeden bag z lotu zewnętrznego, najlepiej z GPS-em, żeby sprawdzić,
czy parser radzi sobie z innym zestawem topików (np. `px4_msgs` `/fmu/out/*` zamiast MAVROS).

**2. Aplikacja desktopowa.** Electron, który sam uruchamia serwer i otwiera okno,
przeciąganie bagu na okno oraz paczka AppImage dla zespołu. Wymaga `nodejs`/`npm`
(`sudo apt install nodejs npm`), których nie ma na tej maszynie. Do zrobienia przy tym:
wygenerować `VIEWER_TOKEN` przy starcie i przekazać go frontendowi (`window.VIEWER_TOKEN`),
spakować serwer Pythona (PyInstaller), pierwsze uruchomienie z ekranem na klucze
(Electron `safeStorage`).

**3. Mapa dla lotów z GPS: kod jest, brakuje testu w przeglądarce.** Przycisk „Map” pojawia się
przy bagach z GPS (`frontend/map.js`, Cesium ładowany z przypiętego CDN, wersja 1.146.0).
Sprawdzone: token ion działa, asset `2275207` to Google Photorealistic 3D Tiles, API zwraca tor GPS,
a składnia JS jest poprawna. **Nie sprawdzone wizualnie:** czy ścieżka leży we właściwym miejscu,
czy dron obraca się w dobrą stronę (znak nagłówka), czy wysokość nie wchodzi pod teren.
Zostało do zrobienia:
- domyślnie Cesium World Terrain + OSM Buildings, opcjonalnie Google Photorealistic
  3D Tiles przez Cesium ion (asset `2275207`, wystarczy sam token ion);
- tryb offline: Google nie zezwala na zapis kafelków, więc offline = zbuforowane dane
  otwarte dla stałych miejsc testów;
- loty bez GPS, ale na zewnątrz: ręczne ustawienie punktu startu i kierunku na mapie
  (z zapisem obok bagu) lub lista stałych miejsc;
- do sprawdzenia: cennik i limity ion dla tego zasobu, układ wysokości (elipsoida vs AMSL).

**4. Odzyskiwanie uszkodzonych bagów.** Bag z pustym lub brakującym `metadata.yaml`
(np. `wp_follower_…21_50_42`, pełny lot 92 s) daje dziś tylko komunikat błędu. Dane w bazie
są, więc dałoby się odtworzyć metadane z samej bazy SQLite.

**5. Postęp ładowania.** Pierwsze otwarcie dużego bagu z USB to minuty, a UI pokazuje tylko
„Loading recording…”. Potrzebny pasek postępu lub informacja „pierwsze czytanie może potrwać”.

**6. Rozszerzenia widoku** (do ustalenia priorytetów z zespołem):
- trasa planowana obok faktycznej (`/drone/current_segment_path`, `/mavros/mission/waypoints`);
- ramki detekcji YOLO na obrazie kamery (format pola `detections` trzeba jeszcze zbadać);
- chmura lidarowa Livox (`/livox/lidar`, ciężka);
- porównanie dwóch lotów obok siebie;
- tekstura/plan podłogi hali (obecny `floor_imav.jpg` jest wzorem, nie planem, więc się nie nadaje);
- eksport zrzutu ekranu / wideo.

**7. Jakość.** Testy frontendu, wsparcie dla bagów `.mcap` w ścieżce klatek (jest wolna
ścieżka zapasowa), tłumaczenia tekstów (teksty są zebrane w `MODE_LABELS` i stałych
na górze `main.js`), obsługa bagów podzielonych na wiele plików `.db3` (kod jest, brak testu).

## Nagrywanie lotu z symulatora PX4 z GPS-em

Przepis, który zadziałał (PX4 SITL + Gazebo w Dockerze z `base_image`):

1. Przed `make px4_sitl gz_x500` ustaw miejsce: `export PX4_HOME_LAT=48.2655 PX4_HOME_LON=11.6710 PX4_HOME_ALT=480`
   (TUM Garching, wartości orientacyjne).
2. W QGroundControl: Plan → waypointy z różnymi wysokościami → **Upload** → Fly → Takeoff → Start Mission.
3. W kontenerze, **przed** startem: `ros2 bag record -o tum_test /fmu/out/vehicle_global_position
   /fmu/out/vehicle_local_position_v1 /fmu/out/vehicle_attitude /fmu/out/vehicle_status_v1`
   i zatrzymaj Ctrl+C dopiero po lądowaniu (bez tego nie powstanie `metadata.yaml`).
   Uwaga: poprzednie nagranie nie zawierało `vehicle_status`, bo tego topicu nie złapało polecenie,
   więc nie ma w nim trybów lotu. Sprawdź `ros2 topic list | grep fmu`.
4. Bag ląduje w `~/Repos/` (= `/ros2_ws` w kontenerze), ROS 2 Jazzy zapisuje domyślnie **mcap**.

W symulatorze `alt` i `alt_ellipsoid` są identyczne (brak geoidy), więc ten bag nie sprawdza różnicy
między wysokością n.p.m. a nad elipsoidą (w Monachium ok. 47 m). Dlatego mapa ma opcję „Put takeoff on the ground”.

## Podjęte decyzje

| Decyzja | Powód |
|---|---|
| three.js zamiast Cesium dla MVP | loty w hali bez GPS, nie potrzeba globu; prostsze i działa offline |
| Cesium jako warstwa mapy później | gotowy glob, kafelki, CZML; potrzebny tylko dla lotów z GPS |
| Python + `rosbags` (bez instalacji ROS) | czytanie bagów na dowolnym laptopie, bez Dockera |
| Electron (nie Tauri) na desktop | Chromium w pakiecie ma stabilny WebGL na Linuksie; WebKitGTK w Tauri bywa zawodny |
| Standardowe typy ROS, bez `mavros_msgs` | nie trzeba rejestrować definicji wiadomości; wystarczy do MVP |
| Dane w ENU, obracana tylko grupa sceny | żadnej konwersji osi punkt po punkcie |
| Bagi czytane z pendrive'a + cache wyniku | mało miejsca na laptopach, bagi mają do kilkunastu GB |
| Zdjęcia Google przez Cesium ion | jeden token zamiast konta rozliczeniowego Google Cloud (token działa, asset `2275207` dostępny; cennik do sprawdzenia). Klucz Google **nie jest** wysyłany do przeglądarki |
| Cesium z CDN na razie, lokalna kopia później | pobranie binariów wymaga zgody; serwer sam przełączy się na `frontend/vendor/cesium/`, jeśli ją dodasz |

## Znane ograniczenia i pułapki

- **Wolne pierwsze otwarcie dużego bagu z USB.** Odczyt jest sekwencyjny, nie da się go
  skrócić bez indeksu w pliku bagu (a nie zapisujemy nic na nośniku z bagami).
- **Pamięć podręczna lotu nie zna zmian w kodzie.** Po zmianie formatu danych podnieś
  `CACHE_VERSION`, inaczej użytkownik zobaczy stare dane.
- **Historia: serwer „zawieszał się”.** Przyczyna: każda klatka z kamery otwierała bag od nowa
  (~0,5 s), a przeglądarka prosiła o ~16 klatek na sekundę, więc kolejka rosła w nieskończoność
  i zasłaniała nawet listę bagów (Firefox: 6 połączeń na host). Naprawa: połączenia z bazą
  i indeks czasu po stronie serwera, plus „najwyżej jedno żądanie na kamerę” po stronie
  przeglądarki. **Nie wprowadzaj zmian, które wysyłają wiele żądań klatek naraz.**
- **LIO a pozycja MAVROS** mogą mieć różne ramki odniesienia (`camera_init` vs `map`).
  Ścieżka LIO jest rysowana bez dopasowania do pozycji i domyślnie ukryta.
- **Uwaga przy ubijaniu serwera:** `pkill -f server.py` trafia też we własną powłokę,
  jeśli komenda zawiera tę frazę. Szukaj procesu po porcie: `ss -ltnp | grep 8765`.
- **Headless Firefox** zawiesza się w tym środowisku, więc zrzuty ekranu do testów
  automatycznych trzeba robić inaczej (Playwright po instalacji Node).
