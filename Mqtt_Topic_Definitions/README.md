# Definicje Tematów MQTT projektu Smart Garden

W tym folderze znajdują się pliki źródłowe (`mqtt_app.c` oraz `mqtt_app.h`) z projektu ESP32, które zawierają implementację logiki MQTT.

## Struktura Tematów (Topic Namespace)

Wszystkie tematy w systemie są izolowane per użytkownik i per urządzenie. 
Schemat główny:
`garden/{user_id}/{device_id}/{funkcja}`

Gdzie:
- `{user_id}`: Unikalny, losowy identyfikator użytkownika generowany przez backend (np. `ab12-cd34-ef56`).
- `{device_id}`: Adres MAC urządzenia ESP32 w formacie HEX bez separatorów (np. `A0B1C2D3E4F5`).

Definicja ta zapewnia, że użytkownik ma dostęp tylko do swoich urządzeń (dzięki listom kontroli dostępu ACL na brokerze).

## Tematy Publikowane (Up-stream)
Urządzenie wysyła dane na następujące tematy:

| Temat (Suffix) | Opis | Retained | QoS |
| :--- | :--- | :---: | :---: |
| `/telemetry` | Okresowe pomiary z czujników (wilgotność, temperatura, światło) wysyłane w formacie JSON. | Nie | 1 |
| `/alert` | Krytyczne powiadomienia i błędy (np. brak wody, awaria czujnika). | Nie | 2 |
| `/capabilities` | Lista dostępnych czujników na danym urządzeniu. Wysyłana raz po starcie. | **Tak** | 1 |
| `/settings/state` | Aktualna konfiguracja urządzenia (progi alarmowe, czasy podlewania). | Nie | 1 |

*Zdefiniowane w `mqtt_app.c` (funkcje `mqtt_app_send_telemetry`, `publish_alert_record`, `mqtt_app_publish_capabilities`).*

## Tematy Subskrybowane (Down-stream)
Urządzenie nasłuchuje poleceń na następujących tematach:

| Temat (Suffix) | Opis | Akcja Urządzenia |
| :--- | :--- | :--- |
| `/command/water` | Manualne uruchomienie podlewania. | Włącza pompę na zadany czas (payload JSON: `{"duration": 5}`). |
| `/command/read` | Wymuszenie pomiaru. | Natychmiastowy odczyt czujników i publikacja na temat `/telemetry`. |
| `/settings` | Zmiana konfiguracji. | Aktualizuje progi i zapisuje je w pamięci NVS. |
| `/settings/get` | Żądanie pobrania ustawień. | Publikuje obecną konfigurację na temat `/settings/state`. |
| `/settings/reset` | Przywracanie ustawień domyślnych. | Resetuje progi alarmowe do wartości fabrycznych. |

*Zdefiniowane w `mqtt_app.c` (funkcja `mqtt5_event_handler` - sekcja `MQTT_EVENT_CONNECTED`).*

## Kod Źródłowy
Szczegóły implementacji, w tym formatowanie JSON oraz obsługa kolejek offline, znajdują się w załączonym pliku `mqtt_app.c`.
