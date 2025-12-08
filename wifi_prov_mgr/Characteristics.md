Searched for text `wifi_prov_mgr_endpoint_create`, 5 results

Read [](file:///c%3A/Users/bajor/Desktop/Semestr%205%20-%20local/SmartGarden/wifi_prov_mgr/sdkconfig#800-800), lines 800 to 1000

Searched for text `CONFIG_WIFI_PROV`, 20 results

Searched for text `ff51`, 3 results

W tym projekcie używany jest standardowy komponent `wifi_provisioning` z domyślną konfiguracją (brak niestandardowych UUID w sdkconfig ani w kodzie).

Oznacza to, że ESP32 udostępnia **Serwis Provisioningu** o UUID:
`0000ffff-0000-1000-8000-00805f9b34fb`

W ramach tego serwisu dostępne są następujące charakterystyki:

1.  **ProvScan** (Skanowanie sieci WiFi)
    *   **UUID:** `0000ff50-0000-1000-8000-00805f9b34fb`
    *   **Opis:** Służy do zlecenia skanowania sieci przez ESP32 i pobrania listy wykrytych punktów dostępowych.

2.  **ProvSession** (Sesja / Bezpieczeństwo)
    *   **UUID:** `0000ff51-0000-1000-8000-00805f9b34fb`
    *   **Opis:** Służy do nawiązania bezpiecznej sesji (wymiana kluczy, Proof of Possession - w Twoim przypadku hasło "Haslo123").

3.  **ProvConfig** (Konfiguracja WiFi)
    *   **UUID:** `0000ff52-0000-1000-8000-00805f9b34fb`
    *   **Opis:** Służy do przesłania docelowych danych logowania do WiFi (SSID i hasło) oraz zatwierdzenia konfiguracji.

4.  **ProtoVer** (Wersja protokołu)
    *   **UUID:** `0000ff53-0000-1000-8000-00805f9b34fb`
    *   **Opis:** Informuje aplikację mobilną o wersji protokołu używanego przez urządzenie.

Każda z tych charakterystyk obsługuje operacje zapisu (Write) i odczytu (Read) w celu wymiany danych w formacie Protocol Buffers (Protobuf).