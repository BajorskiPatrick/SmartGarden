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

2.  Uruchom kontenery:
    ```powershell
    docker-compose up -d --build
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

3.  **Pierwsze uruchomienie (Provisioning):**
    *   Po wgraniu, jeśli urządzenie nie ma zapisanych WiFi/MQTT w NVS, wejdzie w tryb Provisioningu (lub wstrzyma działanie).
    *   Postępuj zgodnie z instrukcjami w logach `monitor` (np. użyj aplikacji mobilnej ESP BLE Provisioning lub wpisz dane na sztywno jeśli tak skonfigurowano w kodzie testowym).
    *   W naszym setupie deweloperskim, ESP32 domyślnie łączy się z MQTT na adresie IP hosta (należy upewnić się, że `CONFIG_ESP_MQTT_URI` w `sdkconfig` lub kodzie wskazuje na poprawne IP Twojego komputera, np. `mqtt://192.168.x.x:1883`, a nie localhost, bo localhost na ESP32 to... ESP32).

## Scenariusz Prezentacji (Request Flow)

Poniżej znajduje się sekwencja komend `curl`, która demonstruje pełny przepływ: rejestrację użytkownika, logowanie, obsługę urządzenia i zmianę ustawień.

> **Uwaga**: W poniższych komendach:
> *   Zamień `TWOJ_MAC_ADRES` na rzeczywisty MAC adres urządzenia (odczytany z logów ESP32 lub endpointu `/api/devices`).
> *   Token JWT (`TOKEN`) uzyskasz w kroku 2 (Logowanie). Należy go podstawić do nagłówka `Authorization` w kolejnych zapytaniach.

### 1. Rejestracja Użytkownika

Tworzymy nowego użytkownika w systemie.

```powershell
curl -X POST http://localhost:8080/api/auth/register `
  -H "Content-Type: application/json" `
  -d "{\"username\": \"patrick\", \"password\": \"password123\"}"
```

### 2. Logowanie (Pobranie Tokena)

Logujemy się, aby uzyskać token JWT potrzebny do autoryzacji.

```powershell
curl -X POST http://localhost:8080/api/auth/login `
  -H "Content-Type: application/json" `
  -d "{\"username\": \"patrick\", \"password\": \"password123\"}"
```
*   **Odpowiedź**: Ciąg znaków (Token JWT). Skopiuj go!

---
**W kolejnych krokach podmieniaj `TWOJ_TOKEN` na skopiowany token.**
---

### 3. Pobranie Listy Urządzeń

Sprawdzamy, czy ESP32 podłączyło się do systemu. ESP32 automatycznie rejestruje się w backendzie po wysłaniu pierwszej telemetrii.

```powershell
curl -G http://localhost:8080/api/devices `
  -H "Authorization: Bearer TWOJ_TOKEN"
```
*   Skopiuj `macAddress` (np. `78:EE:4C:00:06:5C`) z odpowiedzi.

### 4. Pobranie Aktualnych Ustawień (GET)

Pobieramy bieżącą konfigurację z urządzenia. Jeśli są to ustawienia domyślne, odpowiedź może zawierać tylko podstawowe pola.

```powershell
curl -G http://localhost:8080/api/devices/TWOJ_MAC_ADRES/settings `
  -H "Authorization: Bearer TWOJ_TOKEN"
```

### 5. Aktualizacja Ustawień (Zmiana Progów)

Ustawiamy nowe progi wilgotności (np. min 30%, max 60%) i czas podlewania.

```powershell
curl -X POST http://localhost:8080/api/devices/TWOJ_MAC_ADRES/settings `
  -H "Content-Type: application/json" `
  -H "Authorization: Bearer TWOJ_TOKEN" `
  -d "{\"hum_min\": 30.0, \"hum_max\": 60.0, \"watering_duration_sec\": 10, \"measurement_interval_sec\": 15}"
```
*   Obserwuj logi ESP32 (`idf.py monitor`) – powinieneś zobaczyć komunikat o otrzymaniu nowych ustawień.
*   Sprawdź ponownie GET (krok 4), aby potwierdzić zmiany.

### 6. Reset Ustawień (Factory Reset)

Przywracamy ustawienia urządzenia do wartości domyślnych/fabrycznych.

```powershell
curl -X POST http://localhost:8080/api/devices/TWOJ_MAC_ADRES/settings/reset `
  -H "Authorization: Bearer TWOJ_TOKEN"
```
*   ESP32 potwierdzi odebranie komendy RESET i przywróci progi domyślne (np. brak alertów, domyślny czas podlewania).
*   Ponowny GET zwróci tylko podstawowe parametry (bez `hum_min`/`hum_max`, które zostały wyczyszczone).

### 7. Zlecenie Podlewania (Manualne)

Wymuszamy uruchomienie pompy na 5 sekund.

```powershell
curl -X POST "http://localhost:8080/api/devices/TWOJ_MAC_ADRES/water?duration=5" `
  -H "Authorization: Bearer TWOJ_TOKEN"
```

### 8. Odczyt Historii Pomiarów (Telemetria)

Pobieramy ostatnie pomiary z bazy danych.

```powershell
curl -G http://localhost:8080/api/devices/TWOJ_MAC_ADRES/telemetry `
  -H "Authorization: Bearer TWOJ_TOKEN"
```
