# Smart Garden Project

System do inteligentnego zarządzania ogrodem oparty o ESP32 i Spring Boot.

## Wymagania

*   **Docker** i **Docker Compose**
*   **ESP-IDF v5.x** (do zbudowania firmware na ESP32)
*   Urządzenie **ESP32** (np. ESP32 DevKit V1)

## Uruchomienie Środowiska (Backend + Baza + MQTT)

Całość backendu (Java Spring Boot, PostgreSQL, Mosquitto MQTT Broker) jest skonteneryzowana.

1.  Otwórz terminal w katalogu projektu:
    ```powershell
    cd final_project/java_backend
    ```

2. Uruchom kontenery:

   Wybierz komendę odpowiednią dla swojego systemu operacyjnego. Pamiętaj, aby zamiast `{IP}` wpisać swój adres lokalny (np. `192.168.1.15`).

   **Linux / macOS (Bash):**
   ```bash
   MQTT_BROKER_PUBLIC_URL=mqtt://{IP}:1883 docker-compose up -d --build
   ```
   **Windows (PowerShell):**
   ```powershell
   $env:MQTT_BROKER_PUBLIC_URL="mqtt://{IP}:1883"; docker-compose up -d --build
   ```
    *   To polecenie zbuduje obraz backendu i uruchomi bazę danych oraz brokera MQTT.
    *   **Backend API** będzie dostępne pod adresem: `http://localhost:8080`
    *   **MQTT Broker** będzie dostępny na porcie: `1883`

3.  Aby sprawdzić logi backendu:
    ```powershell
    docker-compose logs -f backend
    ```

## Uruchomienie Urządzenia (ESP32)

1.  Przejdź do katalogu firmware:
    ```powershell
    cd final_project/esp32
    ```

2.  Skonfiguruj, zbuduj i wgraj firmware:
    ```powershell
    idf.py set-target esp32
    idf.py build
    idf.py flash monitor
    ```

3.  **Pierwsze uruchomienie (Web Provisioning):**
    *   Po wgraniu firmware, urządzenie wejdzie w tryb oczekiwania na Provisioning (dioda LED zacznie migać).
    *   Otwórz aplikację webową: http://localhost:3000
    *   Zaloguj się (domyślnie lub zarejestruj nowe konto).
    *   Kliknij przycisk **"Add Device"** (lub wejdź na `/provision`).
    *   Postępuj zgodnie z instrukcjami na ekranie:
        1.  Wybierz urządzenie z listy (SmartGarden_XXXXXX).
        2.  Kliknij "Pair" i potwierdź (na urządzeniu naciśnij przycisk BOOT, jeśli zostaniesz o to poproszony - w obecnej wersji autoryzacja jest zazwyczaj automatyczna lub wymaga fizycznego potwierdzenia).
        3.  Wybierz sieć WiFi dla urządzenia i wpisz hasło.
        4.  (Opcjonalnie) Wybierz profil rośliny.
    *   Po zakończeniu, urządzenie połączy się z WiFi i MQTT, a Ty zostaniesz przekierowany do Dashboardu.

## Funkcjonalność Aplikacji Webowej

Aplikacja dostępna jest pod adresem: `http://localhost:3000`.

### 1. Rejestracja i Logowanie

Aby korzystać z systemu Smart Garden, wymagane jest konto użytkownika.
1.  Na ekranie logowania wybierz opcję rejestracji.
2.  Podaj unikalną nazwę użytkownika oraz hasło.
3.  Po utworzeniu konta nastąpi automatyczne logowanie.

### 2. Dashboard

Główny widok aplikacji (Dashboard) prezentuje listę wszystkich przypisanych do użytkownika urządzeń.
*   **Kafelki urządzeń**: Każde urządzenie wyświetlane jest jako karta zawierająca nazwę, status (online/offline) oraz ostatni odczyt wilgotności.
*   **Dodawanie urządzeń**: Przycisk "Add Device" (lub "Provision Device") w górnym rogu pozwala na sparowanie nowego modułu ESP32.

### 3. Dodawanie Nowego Urządzenia (Provisioning)

Proces łączenia urządzenia ESP32 z siecią WiFi i przypisywania do konta użytkownika odbywa się przez przeglądarkę (Web Bluetooth / Web Serial - zależnie od implementacji, tutaj standardowo przez formularz provisioning).
1.  Kliknij **"Add Device"**.
2.  Wyślij żądanie wyszukania urządzeń (Browser poprosi o uprawnienia Bluetooth/Serial jeśli dotyczy, lub po prostu wprowadź dane). *W tym projekcie korzystamy z mechanizmu Provisioning API.*
3.  Postępuj zgodnie z kreatorem:
    *   Wybierz wykryte urządzenie.
    *   Podaj dane do sieci WiFi (SSID i hasło), z którą ma się połączyć ESP32.
    *   (Opcjonalnie) Wybierz profil rośliny, aby wstępnie skonfigurować ustawienia.

### 4. Szczegóły Urządzenia

Kliknięcie w kafel urządzenia na Dashboardzie przenosi do widoku szczegółowego.

#### Telemetria i Wykresy
*   Bieżące odczyty: Wilgotność gleby (%), Temperatura (°C), Poziom sygnału WiFi (RSSI).
*   Wykresy historyczne: Wizualizacja zmian wilgotności i temperatury w czasie.

#### Sterowanie Ręczne (Manual Override)
*   **Water Now**: Przycisk pozwalający na natychmiastowe uruchomienie pompy na określony czas (np. 5 sekund). Służy do doraźnego nawadniania lub testowania pompy.

#### Ustawienia (Settings)
W zakładce ustawień możesz skonfigurować automatykę:
*   **Progi wilgotności (Min/Max)**:
    *   Jeśli wilgotność spadnie poniżej **Min**, system może wysłać powiadomienie lub uruchomić podlewanie (zależnie od logiki firmware).
    *   Jeśli wilgotność wzrośnie powyżej **Max**, podlewanie zostanie wstrzymane.
*   **Czas podlewania**: Jak długo pompa ma pracować podczas jednego cyklu nawadniania.
*   **Interwał pomiarów**: Jak często urządzenie ma się wybudzać, mierzyć wilgotność i wysyłać dane do serwera.
*   **Factory Reset**: Opcja przywrócenia ustawień domyślnych urządzenia.

