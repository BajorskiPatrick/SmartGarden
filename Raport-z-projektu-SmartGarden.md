# **Raport z projektu SmartGarden**

## **Autorzy**

- Patrick Bajorski

- Jan Banasik

- Gabriel Filipowicz

## **Spis treści**

- [Założenia Projektu i Kluczowe Funkcjonalności](#założenia-projektu-i-kluczowe-funkcjonalności)
  - [Monitoring Środowiskowy (Telemetria)](#monitoring-środowiskowy-telemetria)
  - [Automatyzacja i Inteligentne Podlewanie](#automatyzacja-i-inteligentne-podlewanie)
  - [Zdalne Sterowanie i Interwencja](#zdalne-sterowanie-i-interwencja)
  - [System Powiadomień i Alertów](#system-powiadomień-i-alertów)
  - [Zarządzanie Profilami Roślin](#zarządzanie-profilami-roślin)
  - [Provisioning i Łatwe Parowanie (Bluetooth Low Energy)](#provisioning-i-łatwe-parowanie-bluetooth-low-energy)
  - [Bezpieczeństwo i Architektura](#bezpieczeństwo-i-architektura)
- [Scenariusze Użycia (Use Cases)](#scenariusze-użycia-use-cases)
- [Wykorzystane Czujniki i Elementy Wykonawcze](#wykorzystane-czujniki-i-elementy-wykonawcze)
  - [Komunikacja Cyfrowa (Magistrala I2C)](#komunikacja-cyfrowa-magistrala-i2c)
  - [Komunikacja Analogowa (ADC)](#komunikacja-analogowa-adc)
  - [Komunikacja Cyfrowa (GPIO)](#komunikacja-cyfrowa-gpio)
- [Wykorzystanie Bluetooth Low Energy (BLE)](#wykorzystanie-bluetooth-low-energy-ble)
  - [Architektura Rozwiązania (GATT Server)](#architektura-rozwiązania-gatt-server)
  - [Mechanizm Bezpieczeństwa (Physical Proof of Presence)](#mechanizm-bezpieczeństwa-physical-proof-of-presence)
  - [Sygnalizacja Stanu](#sygnalizacja-stanu)
- [Komunikacja MQTT (Struktura i Protokół)](#komunikacja-mqtt-struktura-i-protokół)
  - [Tematy Publikowane przez Urządzenie (Up-stream)](#tematy-publikowane-przez-urządzenie-up-stream)
  - [Tematy Subskrybowane przez Urządzenie (Down-stream)](#tematy-subskrybowane-przez-urządzenie-down-stream)
- [Funkcjonalności Aplikacji Serwerowej](#funkcjonalności-aplikacji-serwerowej)
  - [Backend (Spring Boot)](#backend-spring-boot)
  - [Frontend (React + Vite)](#frontend-react--vite)
- [Funkcjonalności Aplikacji Mobilnej (React Native)](#funkcjonalności-aplikacji-mobilnej-react-native)
  - [Kluczowe Moduły Aplikacji:](#kluczowe-moduły-aplikacji)
- [Schemat Elektryczny i Wygląd Stacji](#schemat-elektryczny-i-wygląd-stacji)

<div style="page-break-after: always;"></div>

## **Założenia Projektu i Kluczowe Funkcjonalności**
Celem projektu jest stworzenie kompleksowego systemu IoT (Internet of Things) wspomagającego opiekę nad roślinami doniczkowymi i ogrodowymi. System "Smart Garden" integruje warstwę sprzętową (sensorową i wykonawczą) z warstwą serwerową oraz aplikacjami klienckimi (webową i mobilną), zapewniając użytkownikowi pełną zdalną kontrolę oraz automatyzację procesów nawadniania.

Projekt został podzielony na cztery główne moduły współpracujące ze sobą:

- Moduł Sprzętowy (Firmware ESP32): Odpowiedzialny za bezpośrednią interakcję z fizycznym środowiskiem.

- Backend (Spring Boot): Serce systemu, odpowiedzialne za logikę biznesową, autoryzację i składowanie danych.

- Frontend aplikacji webowej (React): Panel administracyjny dla użytkownika dostępny przez przeglądarkę.

- Aplikacja Mobilna (React Native): Panel administracyjny dla użytkownika dostępny w formie aplikacji mobilnej

### **Monitoring Środowiskowy (Telemetria)**
System realizuje pomiar kluczowych parametrów roślin oraz ich otoczenia w zadanym interwale czasowym (domyślnie 60s). Dane są zbierane przez mikrokontroler ESP32 i przesyłane w czasie rzeczywistym na serwer (przez protokół MQTT), gdzie są archiwizowane i udostępniane użytkownikowi w formie wykresów historycznych oraz wskaźników "na żywo". Mierzone parametry to:

- Wilgotność gleby (%): Podstawowy wskaźnik decydujący o nawadnianiu.

- Temperatura powietrza (°C): Monitorowanie warunków termicznych.

- Wilgotność powietrza (%): Kontrola mikroklimatu.

- Ciśnienie atmosferyczne (hPa): Kontrola mikroklimatu

- Natężenie światła (lux): Weryfikacja nasłonecznienia rośliny.

- Poziom wody w zbiorniku: Czujnik binarny ostrzegający przed brakiem wody w rezerwuarze systemu nawadniania.


### **Automatyzacja i Inteligentne Podlewanie**
System został zaprojektowany z naciskiem na autonomię.

- Automatyczne nawadnianie: Urządzenie ESP32 posiada zaimplementowaną logikę decyzyjną na obrzeżach sieci (Edge Computing). Jeśli wilgotność gleby spadnie poniżej zdefiniowanego przez użytkownika progu (soil_min), system automatycznie uruchomi pompę wodną na określony czas. Mechanizm ten posiada zabezpieczenie przed "przelaniem" (cooldown), zapobiegające zbyt częstemu podlewaniu.

<div style="page-break-after: always;"></div>

- Praca w trybie offline: Algorytmy automatyzacji działają niezależnie od dostępności połączenia WiFi. Urządzenie nadal monitoruje glebę i podlewa roślinę nawet przy braku internetu, a zebrane dane telemetryczne są buforowane w pamięci RAM i wysyłane zbiorczo po odzyskaniu połączenia.


### **Zdalne Sterowanie i Interwencja**
Użytkownik posiada możliwość ręcznej ingerencji w działanie systemu z poziomu aplikacji webowej lub mobilnej:

- Manualne Podlewanie (Manual Override): Zdalne uruchomienie pompy na żądanie (np. w celu testu lub dodatkowego nawilżenia).

- Wymuszenie Pomiaru: Natychmiastowe odświeżenie danych z czujników bez oczekiwania na kolejny cykl pomiarowy.

- Konfiguracja Zdalna: Zmiana interwałów pomiarowych, czasu trwania podlewania oraz progów alarmowych bez fizycznego dostępu do urządzenia.


### **System Powiadomień i Alertów**
System aktywnie monitoruje stan zdrowia rośliny i infrastruktury, generując powiadomienia (zapisywane w historii zdarzeń i wysyłane przez WebSocket) w sytuacjach krytycznych:

- Przekroczenie bezpiecznych zakresów temperatury, wilgotności lub nasłonecznienia (konfigurowalne progi Min/Max).

- Krytycznie niski poziom wilgotności gleby.

- Brak wody w zbiorniku ("Refill water tank").


### **Zarządzanie Profilami Roślin**
Aby dać użytkownikowi jeszcze większą kontrolę nad systemem i swoimi roślinami, system wprowadza pojęcie Profili Roślin (Plant Profiles). Użytkownik może wybrać gotowy zestaw ustawień (np. "Kaktus", "Paproć", "Bazylia"), który automatycznie narzuca odpowiednie progi nawadniania i alarmów dla danego urządzenia. Użytkownik może również definiować własne profile z poziomu aplikacji webowej lub mobilnej.


### **Provisioning i Łatwe Parowanie (Bluetooth Low Energy)**
Proces dodawania nowego urządzenia do sieci WiFi został maksymalnie uproszczony dzięki wykorzystaniu technologii Bluetooth Low Energy (BLE) zaimplementowanej w aplikacji mobilnej (React Native) oraz na ESP32 (wifi_prov). Użytkownik wyszukuje nowe urządzenie w pobliżu, bezpiecznie przekazuje dane logowania do sieci WiFi i przypisuje je do swojego konta, co eliminuje skomplikowaną konfigurację przez porty szeregowe.

<div style="page-break-after: always;"></div>

### **Bezpieczeństwo i Architektura**
- Uwierzytelnianie: Dostęp do API zabezpieczony jest tokenami JWT (JSON Web Token), a każdy użytkownik ma dostęp tylko do swoich urządzeń.

- Bezpieczny MQTT: Broker wiadomości (Mosquitto) zintegrowany jest z bazą danych użytkowników, wymuszając autoryzację każdego urządzenia i izolację tematów (ACL), co zapobiega nieuprawnionemu sterowaniu cudzymi urządzeniami.

- Skalowalność: Architektura oparta o kontenery Docker pozwala na łatwe wdrażanie i skalowanie backendu.

<div style="page-break-after: always;"></div>

## **Scenariusze Użycia (Use Cases)**
Analiza rzeczywistych przypadków użycia systemu przez użytkownika końcowego:

### **UC1: Pierwsze Uruchomienie i Provisioning (Onboarding)**
- **Aktorzy**: Użytkownik, Aplikacja Mobilna, Urządzenie.
- **Opis**: Proces dodawania nowego urządzenia do domowej sieci WiFi.
- **Przebieg**:
  1. Użytkownik włącza urządzenie (dioda miga w trybie parowania).
  2. Aplikacja mobilna wykrywa urządzenie przez Bluetooth Low Energy (BLE).
  3. Użytkownik wybiera sieć WiFi i wpisuje hasło.
  4. W celu weryfikacji bezpieczeństwa (Proof of Presence), użytkownik fizycznie naciska przycisk "BOOT" na obudowie stacji.
  5. Urządzenie otrzymuje poświadczenia, łączy się z siecią i automatycznie rejestruje w systemie backendowym.

### **UC2: Automatyczne Utrzymanie Wilgotności**
- **Aktorzy**: System (Autonomiczny).
- **Opis**: Zapewnienie roślinie wody bez ingerencji człowieka.
- **Przebieg**:
  1. Czujnik wykrywa spadek wilgotności gleby poniżej progu "Minimum" (zdefiniowanego w profilu).
  2. System sprawdza, czy w zbiorniku jest woda (czujnik poziomu).
  3. Jeśli warunki są spełnione, pompa zostaje uruchomiona na określony czas.
  4. Urządzenie wysyła zdarzenie telemetryczne o podlaniu do chmury.

### **UC3: Alert "Brak Wody" i Obsługa Awarii**
- **Aktorzy**: System, Użytkownik.
- **Opis**: Reakcja na wyczerpanie się zapasów wody.
- **Przebieg**:
  1. Poziom wody spada, czujnik pływakowy rozwiera obwód.
  2. System natychmiast blokuje pompę (ochrona przed spaleniem).
  3. Użytkownik otrzymuje powiadomienie Push na telefon: "Zbiornik pusty! Uzupełnij wodę".
  4. Po dolaniu wody, system automatycznie wznawia normalną pracę.

### **UC4: Zmiana Przeznaczenia (Zmiana Profilu Rośliny)**
- **Aktorzy**: Użytkownik.
- **Opis**: Rekonfiguracja urządzenia dla innej rośliny.
- **Przebieg**:
  1. Użytkownik przenosi czujnik do innej doniczki (np. z Bazylii do Kaktusa).
  2. W aplikacji wybiera profil "Kaktus".
  3. System automatycznie aktualizuje parametry (rzadsze podlewanie, wyższy próg nasłonecznienia) i przesyła je do urządzenia.

<div style="page-break-after: always;"></div>

## **Wykorzystane Czujniki i Elementy Wykonawcze**
Warstwa sprzętowa projektu Smart Garden została oparta na platformie SoC ESP32, która pełni rolę jednostki centralnej zarządzającej odczytami z sensorów oraz sterowaniem elementami wykonawczymi. Komunikacja z peryferiami odbywa się z wykorzystaniem trzech głównych interfejsów: magistrali cyfrowej I2C, przetwornika analogowo-cyfrowego (ADC) oraz cyfrowych portów wejścia/wyjścia (GPIO).


### **Komunikacja Cyfrowa (Magistrala I2C)**
Do precyzyjnych pomiarów parametrów środowiskowych wykorzystano dedykowaną magistralę I2C (piny SDA: 21, SCL: 22), do której podłączone są zaawansowane czujniki cyfrowe:

#### BME280 (Bosch Sensortec):
- Funkcja: Zintegrowany czujnik środowiskowy mierzący trzy parametry jednocześnie: temperaturę, wilgotność względną powietrza oraz ciśnienie atmosferyczne.

- Zastosowanie: Dostarcza kluczowych danych o mikroklimacie panującym wokół rośliny. Dzięki interfejsowi cyfrowemu pomiary są wolne od zakłóceń analogowych i fabrycznie skalibrowane.

#### VEML7700 (Vishay):
- Funkcja: Wysokiej precyzji czujnik natężenia światła otoczenia (Ambient Light Sensor).

- Zastosowanie: Mierzy rzeczywiste natężenie oświetlenia w luksach (lux). Umożliwia weryfikację, czy roślina znajduje się w miejscu o odpowiednim nasłonecznieniu (np. wykrywanie zbyt ciemnych stanowisk). Sensor skonfigurowany jest w trybie oszczędzania energii (Power Saving Mode).


### **Komunikacja Analogowa (ADC)**
Do pomiaru wilgotności gleby zastosowano interfejs analogowy, wykorzystując wbudowany w ESP32 przetwornik ADC (Analog-to-Digital Converter) z 12-bitową rozdzielczością.

#### Pojemnościowy Czujnik Wilgotności Gleby (Capacitive Soil Moisture Sensor v1.2):
- Zasada działania: Mierzy zmiany pojemności elektrycznej dielektryka (gleby) w zależności od zawartości wody, zwracając sygnał napięciowy proporcjonalny do wilgotności. W przeciwieństwie do tańszych czujników rezystancyjnych, jest odporny na korozję elektrod.

- Implementacja (Power Gating): W projekcie zastosowano unikalny mechanizm oszczędzania energii i ochrony czujnika. Zasilanie sensora (sterowane przez GPIO 27) jest włączane tylko na czas trwania pomiaru (ok. 50ms), a następnie natychmiast odcinane. Zapobiega to elektrolizie elektrod przy stałym zasilaniu.

- Konwersja: Surowy sygnał z przetwornika jest mapowany programowo na zakres procentowy (0-100%).

<div style="page-break-after: always;"></div>

### **Komunikacja Cyfrowa (GPIO)**
Proste sygnały dwu-stanowe (włącz/wyłącz) obsługiwane są przez standardowe piny GPIO.

#### Czujnik Poziomu Cieczy (Pływakowy):
- Typ: Cyfrowe wejście (Input).

- Funkcja: Prosty przełącznik kontaktronowy umieszczony w zbiorniku z wodą.
- Zasada działania: Gdy poziom wody jest wysoki, obwód jest zamknięty. Gdy poziom spada poniżej minimum, pływak opada, rozwierając obwód. ESP32 wykrywa ten stan (wykorzystując wewnętrzny rezystor podciągający Pull-Up), co skutkuje wygenerowaniem alertu "Refill water tank" i blokadą pompy, aby nie pracowała "na sucho".

#### Pompa Wodna (DC 3V-5V):
- Typ: Cyfrowe wyjście (Output).

- Funkcja: Element symulujący pracę pompy nawadniającej. W projekcie wykorzystano wbudowaną w moduł ESP32 niebieską diodę LED (On-Board LED, GPIO 2).

- Sterowanie: Stan wysoki na pinie GPIO powoduje zaświecenie diody, co sygnalizuje użytkownikowi, że w rzeczywistym systemie w tym momencie pracowałaby pompa wodna. Rozwiązanie to pozwala na bezpieczne testowanie logiki automatyzacji i sterowania bez konieczności podłączania zewnętrznych obciążeń hydraulicznych.

<div style="page-break-after: always;"></div>

## **Wykorzystanie Bluetooth Low Energy (BLE)**
W projekcie Smart Garden technologia BLE została wykorzystana wyłącznie w procesie Provisioningu, czyli wstępnej konfiguracji urządzenia. Implementacja ta (zdefiniowana w plikach wifi_prov.c/h) nie służy do ciągłej transmisji danych telemetrycznych, lecz stanowi bezpieczny kanał "rozruchowy", umożliwiający przekazanie urządzeniu danych niezbędnych do połączenia z siecią WiFi i serwerem MQTT.

### **Architektura Rozwiązania (GATT Server)**
Urządzenie ESP32 działa w trybie GATT Server, rozgłaszając dedykowany serwis o unikalnym UUID. Aplikacja mobilna (GATT Client) łączy się z tym serwisem, aby przeprowadzić konfigurację. Zdefiniowano następujące charakterystyki operacyjne:

#### Dane Sieciowe:
- SSID (Read/Write): Nazwa sieci WiFi.

- PASS (Write-only): Hasło do sieci (zapisywane bezpiecznie, brak możliwości odczytu przez BLE).

#### Konfiguracja MQTT:
-  BROKER_URI, MQTT_LOGIN, MQTT_PASS: Adres brokera i dane uwierzytelniające, niezbędne, aby urządzenie po podłączeniu do WiFi mogło autoryzować się w systemie backendowym.

#### Identyfikacja:
- USER_ID: Unikalny identyfikator użytkownika przypisujący urządzenie do konkretnego konta w bazie danych.

- DEVICE_ID (Read-only): Adres MAC urządzenia, pozwalający aplikacji mobilnej rozpoznać model i unikalność sensora.

#### Sterowanie (Control):
- CTRL: Specjalna charakterystyka "zapalnik". Zapisanie odpowiedniej wartości kończy proces provisioningu, powoduje zapisanie danych w pamięci nieulotnej (NVS) i restart urządzenia.

### **Mechanizm Bezpieczeństwa (Physical Proof of Presence)**
Aby zapobiec nieautoryzowanemu przeprogramowaniu urządzenia przez osoby trzecie znajdujące się w pobliżu, zaimplementowano mechanizm fizycznego uwierzytelniania:

- Domyślnie wszystkie charakterystyki zapisywalne (SSID, Hasła) są zablokowane (Write Protected).

- Aplikacja (webowa lub mobilna) po połączeniu nasłuchuje na charakterystyce AUTH.

- Użytkownik musi fizycznie wcisnąć przycisk BOOT na obudowie ESP32.

- Wciśnięcie przycisku zmienia stan wewnętrznej flagi s_is_authorized i wysyła powiadomienie. Dopiero wtedy odblokowywana jest możliwość nadpisania konfiguracji.

### **Sygnalizacja Stanu**
Proces provisioningu jest dodatkowo sygnalizowany użytkownikowi poprzez diodę LED (GPIO 2), która miga w określonym rytmie (500ms), informując o gotowości do parowania i aktywności modułu radiowego Bluetooth. Okno czasowe na konfigurację jest ograniczone (domyślnie 2 minuty), po czym moduł BLE jest wyłączany w celu oszczędzania energii.

<div style="page-break-after: always;"></div>

## **Komunikacja MQTT (Struktura i Protokół)**
System opiera się na protokole MQTT 5.0, wykorzystując model komunikacji asynchronicznej typu Publish/Subscribe. Sercem wymiany danych jest broker (Mosquitto), a każde urządzenie (ESP32) posiada własną, unikalną ścieżkę tematów (Topic Namespace), co zapewnia izolację bezpieczeństwa.

Wszystkie tematy mają strukturę hierarchiczną: garden/{user_id}/{device_id}/{funkcja} gdzie:

- {user_id} – identyfikator użytkownika (weryfikowany przez ACL).

- {device_id} – adres MAC urządzenia (np. A0B1C2D3E4F5).

### **Tematy Publikowane przez Urządzenie (Up-stream)**
Urządzenie wysyła dane do serwera na następujących kanałach:

#### Telemetria (.../telemetry)
- Częstotliwość: Cykliczna (domyślnie co 60s) lub na żądanie.

- QoS: 1 (At least once).

- Funkcja: Przesyłanie kompletnego zestawu pomiarów.

- Format Danych (JSON):
    ```
    {
        "device": "A0B1C2D3E4F5",
        "user": "user123",
        "timestamp": 1706198400000,
        "sensors": {
            "soil_moisture_pct": 45,
            "air_temperature_c": 23.5,
            "air_humidity_pct": 60.2,
            "pressure_hpa": 1013.25,
            "light_lux": 540.0,
            "water_tank_ok": true
        }
    }
    ```
    Mechanizm Offline: W przypadku braku połączenia, dane są gromadzone w wewnętrznej kolejce (FreeRTOS Queue) i wysyłane zbiorczo (Bulk Publish) natychmiast po ponownym połączeniu.


#### Alerty i Zdarzenia (.../alert)
- Częstotliwość: Zdarzeniowa (asynchroniczna).

- QoS: 2 (Exactly once) – krytyczne powiadomienia nie mogą zginąć.

- Funkcja: Zgłaszanie przekroczeń progów, awarii czujników czy problemów systemowych.

- Format Danych (JSON):
    ```
    {
        "code": "soil_moisture_low",
        "severity": "warning",        // info, warning, error
        "subsystem": "sensor",
        "message": "Soil 15% < min 20%",
        "timestamp": 2026-01-21T15:30:00
    }
    ```


#### Stan Ustawień (.../settings/state)
- Funkcja: Odpowiedź na zapytanie o aktualną konfigurację. Wysyła obecne wartości progów (Min/Max), czasów podlewania i interwałów. Umożliwia synchronizację interfejsu aplikacji ze stanem faktycznym urządzenia.


#### Możliwości Urządzenia (.../capabilities)
- Flaga: Retained (Zatrzymana).

- Funkcja: Wysyłana raz przy starcie. Informuje backend, jakie czujniki są fizycznie dostępne na danym modelu urządzenia (np. czy posiada czujnik światła), co pozwala dynamicznie budować interfejs użytkownika.


### **Tematy Subskrybowane przez Urządzenie (Down-stream)**
Urządzenie nasłuchuje komend z backendu/aplikacji mobilnej:

#### Sterowanie Podlewaniem (.../command/water)
- Payload: {"duration": 5} (czas w sekundach).

- Działanie: Uruchamia procedurę manualnego podlania (lub symulację LED). Jeśli czas trwania przekracza limit bezpieczeństwa (np. 60s), jest on przycinany przez firmware ("Clamping").

#### Wymuszenie Pomiaru (.../command/read)
- Działanie: Wybudza czujniki i natychmiast wykonuje pomiar poza harmonogramem, publikując wynik na temat telemetry.

#### Konfiguracja (.../settings)
- Działanie: Aktualizuje progi alarmowe, interwały i logikę automatyki. Parametry są walidowane (np. czy min < max) i zapisywane w pamięci nieulotnej (NVS). Błędna konfiguracja jest odrzucana, a urządzenie generuje alert settings.rejected.

<div style="page-break-after: always;"></div>

#### Zarządzanie (.../settings/get oraz .../settings/reset)
- Działanie:
    - get: Wymusza wysłanie obecnych ustawień na temat settings/state.
    - reset: Przywraca fabryczne wartości domyślne wszystkich progów (wartością domyślną jest brak progów).

<div style="page-break-after: always;"></div>

## **Funkcjonalności Aplikacji Serwerowej**
Aplikacja serwerowa stanowi centralny punkt systemu Smart Garden, integrując dane z urządzeń IoT i udostępniając je użytkownikowi poprzez interfejsy API oraz GUI. Składa się z dwóch głównych komponentów: backendu (Java Spring Boot) oraz frontendu (React).

### **Backend (Spring Boot)**
Warstwa serwerowa została zaprojektowana w architekturze mikroserwisowej (konteneryzowanej), zapewniając skalowalność i separację logiki biznesowej.

#### Zarządzanie Użytkownikami i Bezpieczeństwo (AuthController):
- Rejestracja i Logowanie: Umożliwia tworzenie nowych kont oraz bezpieczne logowanie.

- JWT (JSON Web Token): Autoryzacja każdego zapytania API odbywa się poprzez bezstanowe tokeny, co zwiększa wydajność i bezpieczeństwo.

- Hasłowanie: Hasła użytkowników są bezpiecznie haszowane przy użyciu algorytmu w bibliotece Spring Security.

#### Obsługa Urządzeń (DeviceController):
- Rejestr Urządzeń: Przechowuje listę sparowanych urządzeń wraz z ich stanem (online/offline, ostatnia aktywność).

- Zarządzanie: Pozwala na zmianę nazwy ("Friendly Name"), usuwanie urządzeń z systemu, zarządzanie ich ustawieniami, a także ręczne wywoływanie podlewania i pomiarów.

- Dostęp do Danych: Udostępnia endpointy REST API do pobierania historii pomiarów (/telemetry) oraz listy alertów (/alerts) z możliwością stronicowania i filtrowania po dacie.

#### Inteligentne Profile Roślin (PlantProfileController):
- Katalog Profili: Umożliwia użytkownikowi tworzenie, edycję i usuwanie definicji wymagań dla różnych gatunków roślin (np. "Kaktus", "Paproć").

- Struktura Profilu: Każdy profil przechowuje zestaw parametrów granicznych:
    - Zakresy temperatury (Min/Max).
    - Zakresy wilgotności powietrza i gleby.
    - Wymagane nasłonecznienie.
    - Parametry automatycznego podlewania (czas trwania).

- Zastosowanie: Profile służą do szybkiej rekonfiguracji urządzeń ("One-click config") bez konieczności ręcznego wpisywania wartości liczbowych dla każdego sensora.

<div style="page-break-after: always;"></div>

#### Provisioning i Rejestracja (ProvisioningController):
- Generowanie Poświadczeń: Endpoint /api/provisioning/device odpowiada za bezpieczne wygenerowanie unikalnych danych logowania do brokera MQTT dla nowo dodawanego urządzenia.

- Wiązanie: Proces ten wiąże fizyczny adres MAC urządzenia bezpośrednio z kontem użytkownika, tworząc wpis w bazie danych i nadając odpowiednie uprawnienia (ACL) w brokerze MQTT.

#### Bridge MQTT-REST:
- Backend pełni funkcję "tłumacza" pomiędzy protokołem HTTP (używanym przez aplikację webową) a MQTT (używanym przez ESP32).

- Komendy wysyłane przez użytkownika (np. "Podlej teraz") są zamieniane na wiadomości MQTT publikowane na odpowiednie tematy.


### **Frontend (React + Vite)**
Interfejs użytkownika, zbudowany jako SPA (Single Page Application), zapewnia responsywność i dynamiczne odświeżanie danych bez konieczności przeładowywania strony.

#### Dashboard (Pulpit Główny):
- Wyświetla kafelkową listę wszystkich urządzeń użytkownika.

- Prezentuje kluczowe wskaźniki "na pierwszy rzut oka": status połączenia, aktualną wilgotność gleby i temperaturę.

#### Szczegóły Urządzenia (Device Details):
- Wykresy Interaktywne: Wizualizacja trendów wilgotności, temperatury i nasłonecznienia w czasie.

- Dziennik Zdarzeń: Lista alertów i błędów zgłoszonych przez urządzenie.

- Panel Sterowania: Przyciski do manualnego uruchomienia podlewania ("Water Now") oraz wymuszenia wykonania pomiarów.

#### Konfiguracja i Ustawienia:
- Formularze pozwalające na zdalną zmianę parametrów pracy urządzenia, takich jak: min/max wilgotność gleby, progi temperatury czy czas trwania podlewania.

- Możliwość przywrócenia ustawień fabrycznych (Factory Reset).

<div style="page-break-after: always;"></div>

## **Funkcjonalności Aplikacji Mobilnej (React Native)**
Aplikacja mobilna "Smart Garden Mobile" pełni rolę przenośnego centrum sterowania, dając użytkownikowi natychmiastowy dostęp do parametrów ogrodu w dowolnym miejscu. Została zbudowana w oparciu o framework React Native (Expo), co zapewnia natywną wydajność na systemach Android i iOS.

### **Kluczowe Moduły Aplikacji:**

#### Ekran Logowania i Rejestracji
- Bezpieczeństwo: Aplikacja wymusza logowanie przy każdym uruchomieniu (chyba że token jest wciąż ważny), chroniąc dostęp do ogrodu przed niepowołanymi osobami.

- Integracja: Komunikuje się z endpointem /api/auth backendu, wymieniając dane na bezpieczny token JWT.

#### Dashboard (Lista Urządzeń)
- Widok zbiorczy: Prezentuje listę wszystkich przypisanych urządzeń w formie przejrzystych kart.

- Status Online/Offline: Wykorzystuje ikonografię i kolory (Zielony/Czerwony) do natychmiastowej sygnalizacji, czy dane urządzenie jest aktualnie połączone z siecią.

- Szybkie Akcje: Umożliwia łatwe przejście do szczegółów lub dodanie nowego urządzenia (przycisk "+").

#### Moduł Provisioning (Dodawanie Urządzeń BLE)
Jest to najbardziej zaawansowany technicznie element aplikacji, realizujący proces "Onboardingu" nowego sprzętu.

- Skanowanie BLE: Aplikacja wykorzystuje moduł Bluetooth Low Energy do wykrywania w pobliżu urządzeń rozgłaszających serwis "SmartGarden".

- Konfiguracja WiFi: Pozwala użytkownikowi wprowadzić nazwę sieci i hasło, które są następnie szyfrowane i przesyłane bezpośrednio do pamięci ESP32 przez charakterystykę BLE.

- Automatyczna Rejestracja: Aplikacja automatycznie pobiera z backendu unikalne poświadczenia MQTT (Login/Hasło) dla nowego urządzenia i wgrywa je do mikrokontrolera.

- Weryfikacja "Physical Presence": Interfejs informuje użytkownika o konieczności fizycznego wciśnięcia przycisku BOOT na urządzeniu w celu potwierdzenia parowania (oczekuje na notyfikację AUTH z charakterystyki BLE), co zabezpiecza przed przypadkową lub złośliwą rekonfiguracją.

#### Szczegóły Urządzenia
Centrum dowodzenia pojedynczą rośliną:

- Karty Pomiarowe (Live Data): Wyświetla bieżące odczyty (Wilgotność gleby, Temperatura, Światło, Wilgotność powietrza). Karty dynamicznie zmieniają kolor (np. na czerwony), jeśli odczyt wykracza poza zdefiniowane w profilu granice normy.

- Stan Wody: Monitoruje poziom wody w zbiorniku, wyświetlając ostrzeżenie "Water Level LOW!" w przypadku jej braku.

- Zarządzanie Profilem: Umożliwia wybór "Profilu Rośliny" (np. Kaktus, Paproć) z listy, co automatycznie aktualizuje ustawienia progowe urządzenia.

- Zmiana Nazwy: Pozwala nadać urządzeniu przyjazną nazwę (np. "Bazylia w kuchni").

- Usuwanie Urządzenia: Funkcja "Delete Device" nie tylko usuwa wpis z bazy danych, ale próbuje również wysłać komendę "Factory Reset" do urządzenia, aby wyczyścić jego pamięć WiFi.

#### Sterowanie Manualne
- Water Now: Przycisk pozwalający na zdalne uruchomienie podlewania. Użytkownik może wybrać czas trwania (np. 10 sekund).

- Measure Now: Wymusza natychmiastowy odczyt sensorów, odświeżając dane na ekranie.

## **Schemat Elektryczny i Wygląd Stacji**

### **Schemat Połączeń (Obwód)**
*(W tym miejscu należy wstawić schemat ideowy połączeń ESP32 z czujnikami, sterownikiem pompy i zasilaniem)*

<br/><br/><br/><br/><br/><br/>
<div style="text-align: center; border: 1px dashed #ccc; padding: 20px;">
  [Miejsce na Rysunek / Schemat Obwodu]
</div>
<br/><br/><br/>

### **Zdjęcie Stacji (Gotowe Urządzenie)**
*(W tym miejscu należy wstawić zdjęcie zmontowanego urządzenia w obudowie oraz sposób montażu w doniczce)*

<br/><br/><br/><br/><br/><br/>
<div style="text-align: center; border: 1px dashed #ccc; padding: 20px;">
  [Miejsce na Zdjęcie Stacji]
</div>
<br/><br/><br/>