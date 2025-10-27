# ESP32 WiFi Station + HTTP Client

Projekt realizujący dwa zadania:
1. Łączenie z WiFi w trybie STATION z obsługą LED
2. Pobieranie stron WWW przez HTTP

## 📋 Funkcjonalności

### Zadanie 1: WiFi Station
- ✅ Łączenie z WiFi (SSID i hasło konfigurowalne)
- ✅ Mruganie diody LED gdy brak połączenia
- ✅ Zmienna globalna `wifi_connected` wskazująca stan połączenia
- ✅ Automatyczne ponowne łączenie (nieskończone próby)

### Zadanie 2: HTTP Client
- ✅ Pobieranie stron WWW przez protokół HTTP
- ✅ Połączenie TCP na port 80
- ✅ Wysyłanie zapytania GET
- ✅ Odbieranie i wyświetlanie kodu HTML na konsoli
- ✅ Automatyczne pobieranie co 30 sekund (konfigurowalne)

## 🏗️ Architektura projektu

### Struktura plików:
```
main/
├── station_example_main.c  # Główny plik - WiFi + LED + koordynacja
├── http_client.c           # Implementacja HTTP client
├── http_client.h           # Nagłówek HTTP client
└── CMakeLists.txt          # Konfiguracja kompilacji
```

### Zadania FreeRTOS:
1. **WiFi Task** (wbudowane w ESP-IDF)
   - Zarządza połączeniem WiFi
   - Wysyła eventy o zmianie stanu

2. **LED Blink Task** (priorytet 5)
   - Uruchamiane gdy `wifi_connected = false`
   - Mruga diodą LED co 500ms
   - Zatrzymywane gdy WiFi się połączy

3. **HTTP Client Task** (priorytet 5)
   - Sprawdza stan `wifi_connected`
   - Pobiera stronę WWW gdy WiFi połączone
   - Czeka 30 sekund między requestami

4. **Main Task**
   - Inicjalizuje wszystkie komponenty
   - Nieskończona pętla utrzymująca program

## ⚙️ Konfiguracja

### WiFi (w pliku `station_example_main.c`):
```c
#define EXAMPLE_ESP_WIFI_SSID "TwojeSSID"
#define EXAMPLE_ESP_WIFI_PASS "TwojeHaslo"
```

### HTTP Client (w pliku `http_client.c`):
```c
const char *HOST = "example.com";        // Serwer do pobierania
const int PORT = 80;                     // Port HTTP
const char *PATH = "/";                  // Ścieżka (strona główna)
const int REQUEST_INTERVAL_SEC = 30;    // Interwał między requestami
```

### LED (w pliku `station_example_main.c`):
```c
#define LED_GPIO_PIN 2  // Domyślny pin GPIO dla LED
```

## 🔧 Kompilacja i flashowanie

```bash
# Kompilacja
idf.py build

# Flashowanie
idf.py flash

# Monitor (logi)
idf.py monitor

# Wszystko razem
idf.py build flash monitor
```

## 📊 Jak to działa?

### 1. Start systemu:
```
app_main() → Inicjalizacja NVS
           → Inicjalizacja LED
           → wifi_init_sta()
           → http_client_init()
           → Nieskończona pętla
```

### 2. Event flow:

**Gdy WiFi się łączy:**
```
WIFI_EVENT_STA_START → esp_wifi_connect()
```

**Gdy WiFi się rozłącza:**
```
WIFI_EVENT_STA_DISCONNECTED → wifi_connected = false
                            → Uruchom LED blink task
                            → esp_wifi_connect() (ponownie)
```

**Gdy WiFi się połączy:**
```
IP_EVENT_STA_GOT_IP → wifi_connected = true
                    → Zatrzymaj LED blink task
                    → Wyłącz LED
```

**HTTP Client (w pętli):**
```
while(1):
    if wifi_connected:
        http_get_request()
        delay(30 sekund)
    else:
        delay(5 sekund)
```

### 3. Jeden firmware = wszystkie funkcje:
- Wszystkie pliki `.c` kompilują się do jednego `Test.bin`
- Ładujesz na ESP32 raz
- Wszystkie zadania działają równolegle dzięki FreeRTOS

## 📝 Przykładowe logi

```
I (1621) wifi station: ESP_WIFI_MODE_STA
I (1621) wifi station: wifi_init_sta finished. WiFi will keep trying to connect in background.
I (1631) http_client: HTTP client initialized
I (2641) wifi station: retry to connect to the AP (attempt 1)
I (5051) wifi:new:<11,0>, old:<1,0>, ap:<255,255>, sta:<11,0>, prof:1
I (5051) wifi:state: init -> auth (0xb0)
I (5061) wifi:state: auth -> assoc (0x0)
I (5071) wifi:state: assoc -> run (0x10)
I (5101) wifi station: got ip:192.168.1.123
I (5101) wifi station: wifi_connected = true
I (5101) http_client: WiFi is connected, performing HTTP request...
I (5111) http_client: Starting HTTP GET request to http://example.com:80/
I (5721) http_client: Connected to server
I (5721) http_client: --- HTTP REQUEST ---
GET / HTTP/1.1
Host: example.com
User-Agent: ESP32-HTTP-Client
Connection: close

I (5731) http_client: --- HTTP RESPONSE ---
HTTP/1.1 200 OK
Content-Type: text/html; charset=UTF-8
...
<!doctype html>
<html>
<head>
    <title>Example Domain</title>
...
```

## 🎯 Testowanie

1. **Test WiFi + LED:**
   - Uruchom ESP32
   - LED powinno mrugać
   - Połącz się z WiFi → LED przestanie mrugać
   - Wyłącz WiFi → LED znowu mruga

2. **Test HTTP:**
   - Poczekaj aż ESP32 się połączy z WiFi
   - W logach zobaczysz request HTTP i odpowiedź (kod HTML)
   - Request powtarza się co 30 sekund

## 🔍 Debugowanie

- Jeśli LED nie mruga - sprawdź GPIO pin (domyślnie 2)
- Jeśli nie łączy się z WiFi - sprawdź SSID i hasło
- Jeśli HTTP nie działa - sprawdź czy masz połączenie z internetem
- Jeśli DNS nie działa - spróbuj użyć IP zamiast hostname

## 📚 Więcej informacji

- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/)
- [FreeRTOS Documentation](https://www.freertos.org/Documentation/RTOS_book.html)
