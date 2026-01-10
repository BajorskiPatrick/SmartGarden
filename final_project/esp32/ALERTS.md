# SmartGarden ESP32 – MQTT Alerts

Ten projekt publikuje alerty na topic:

- `garden/{user_id}/{device_id}/alert`

## Format (JSON)

Alert jest pojedynczym JSON-em. Dla kompatybilności wstecznej utrzymywane są pola `type` i `msg`, ale nowy „kontrakt” to pola `code/severity/subsystem/message/details`.

Minimalny przykład:

```json
{
  "device": "78EE4C00065C",
  "user": "alice",
  "timestamp": 123456,
  "type": "wifi.disconnected",
  "msg": "WiFi disconnected. Retrying...",
  "code": "wifi.disconnected",
  "severity": "warning",
  "subsystem": "wifi",
  "message": "WiFi disconnected. Retrying...",
  "details": {"reason": 201}
}
```

### Pola

- `device` (string) – `device_id` (MAC hex)
- `user` (string) – `user_id`
- `timestamp` (number) – ms od startu (esp_log_timestamp)
- `code` (string) – stabilny identyfikator zdarzenia (słownik poniżej)
- `severity` (string) – `debug` | `info` | `warning` | `error` | `critical`
- `subsystem` (string) – np. `wifi`, `mqtt`, `sensor`, `command`, `thresholds`, `system`, `telemetry`
- `message` (string) – opis tekstowy
- `details` (object|null) – opcjonalne dane diagnostyczne (np. `reason`, `suppressed`, `dropped`)

Pola kompatybilności:
- `type` = `code`
- `msg` = `message`

## Rate-limit (zasady)

Alerty mogą wystąpić w pętli (np. dropy kolejki, rozłączenia). Żeby nie spamować backendu:

- **Cooldown per `code`**: alert o danym `code` jest emitowany najczęściej raz na `cooldown_ms`.
- **Suppression count**: jeśli alert był blokowany przez cooldown, licznik trafia do `details.suppressed` przy kolejnym emit.
- **Edge-trigger**: dla zdarzeń stanowych (np. sensor FAIL/OK) emitujemy tylko na przejściu w stan błędu i na przejściu w stan OK (osobne `code` dla recovery).
- **Once-per-boot**: niektóre alerty (np. `provisioning.incomplete`) emitujemy tylko raz po starcie.

## Słownik kodów (`code`)

Łączność/MQTT:
- `connection.mqtt_connected` (info)
- `connection.mqtt_disconnected` (warning)
- `connection.mqtt_error` (error)
- `mqtt.inbound_oom_drop` (error)

Telemetria/buforowanie:
- `telemetry.buffering_started` (warning)
- `telemetry.buffer_full_dropped` (error)
- `alert.buffer_full_dropped` (error)

WiFi/Provisioning:
- `wifi.disconnected` (warning)
- `wifi.got_ip` (info)
- `provisioning.incomplete` (warning)
- `provisioning.timeout` (warning)
- `provisioning.save_failed` (error)
- `system.factory_reset` (warning)

Sensory:
- `sensor.soil_read_failed` (warning)
- `sensor.soil_recovered` (info)
- `sensor.bme280_read_failed` (warning)
- `sensor.bme280_recovered` (info)
- `sensor.veml7700_read_failed` (warning)
- `sensor.veml7700_recovered` (info)

Komendy/Progi:
- `command.invalid_json` (warning)
- `command.watering_started` (info)
- `command.watering_finished` (info)
- `command.watering_duration_clamped` (warning)
- `thresholds.invalid_json` (warning)
- `thresholds.rejected` (warning)
- `thresholds.applied` (info)

Alerty progowe (istniejące; traktuj jako element słownika):
- `temperature_low`, `temperature_high`
- `humidity_low`, `humidity_high`
- `soil_moisture_low`, `soil_moisture_high`
- `light_low`, `light_high`
- `water_level_critical`
