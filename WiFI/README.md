# SmartGarden

| Supported Targets | ESP32 | ESP32-C2 | ESP32-C3 | ESP32-C5 | ESP32-C6 | ESP32-C61 | ESP32-S2 | ESP32-S3 | ESP32-P4 | ESP32-H2 |
| ----------------- | ----- | -------- | -------- | -------- | --------- | -------- | -------- | -------- | -------- | -------- |

Inteligentny system monitorowania i zarządzania ogrodem z wykorzystaniem ESP32.

## Funkcjonalności

- Łączenie z siecią WiFi w trybie Station
- Automatyczna rekonekcja przy utracie połączenia
- Wizualna informacja o stanie połączenia poprzez diodę LED
- Klient HTTP do komunikacji z serwerem
- Pobieranie danych co 30 sekund

## Konfiguracja

### Konfiguracja WiFi

Otwórz menu konfiguracji projektu (`idf.py menuconfig`).

W menu `SmartGarden Configuration`:

* Ustaw konfigurację WiFi:
    * `WiFi SSID` - nazwa sieci WiFi
    * `WiFi Password` - hasło do sieci WiFi
    * `Maximum retry` - maksymalna liczba prób połączenia (domyślnie nieskończona)

### Konfiguracja HTTP Client

Edytuj plik `main/http_client.c` aby zmienić:
* `HOST` - adres serwera
* `PORT` - port HTTP (domyślnie 80)
* `PATH` - ścieżka do zasobu
* `REQUEST_INTERVAL_SEC` - interwał między requestami (domyślnie 30s)

## Kompilacja i wgranie

Skompiluj projekt i wgraj go na płytkę, następnie uruchom monitor do podglądu logów:

```bash
idf.py -p PORT flash monitor
```

Aby wyjść z monitora serialnego, naciśnij `Ctrl+]`.

Więcej informacji w oficjalnej dokumentacji ESP-IDF:
* [ESP-IDF Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html)

## Przykładowe wyjście

Poniżej przykładowe logi z konsoli przy prawidłowym działaniu systemu.

Wyjście konsoli przy pomyślnym połączeniu z siecią WiFi:
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
```

Przy braku połączenia z siecią WiFi:
```
I (1621) wifi station: ESP_WIFI_MODE_STA
I (1621) wifi station: wifi_init_sta finished. WiFi will keep trying to connect in background.
I (2641) wifi station: retry to connect to the AP (attempt 1)
I (2641) wifi station: connect to the AP fail
I (4709) wifi station: retry to connect to the AP (attempt 2)
I (4709) wifi station: connect to the AP fail
...
```

System będzie próbował połączyć się w nieskończoność. LED będzie migać do momentu nawiązania połączenia.

## Więcej informacji

Dokładniejszy opis implementacji HTTP klienta znajduje się w pliku `HTTP_README.md`.

## Architektura

### Komponenty:
- **WiFi Station** - moduł połączenia WiFi z automatyczną rekonekcją
- **LED Controller** - wizualna informacja o stanie połączenia
- **HTTP Client** - komunikacja z serwerem zewnętrznym

### Zadania FreeRTOS:
- **led_blink_task** - mruganie diodą LED przy braku połączenia
- **http_client_task** - okresowe wysyłanie requestów HTTP
- **wifi tasks** - zarządzanie połączeniem WiFi (wbudowane w ESP-IDF)
