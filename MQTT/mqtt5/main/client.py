import paho.mqtt.client as mqtt
import json
import time
import random

BROKER_ADDRESS = "172.20.10.2"
PORT = 1883
USER = "admin"
PASSWORD = "admin"

TARGET_USER_ID = "user_jan_banasik"
TARGET_DEVICE_ID = "stacja_salon_01"

# Tematy do subskrypcji (odbieranie)
# Używamy '+' aby odbierać od wszystkich urządzeń danego użytkownika
TOPIC_TELEMETRY = f"garden/{TARGET_USER_ID}/+/telemetry"
TOPIC_ALERT = f"garden/{TARGET_USER_ID}/+/alert"

# Temat do publikacji (wysyłanie komend do konkretnego urządzenia)
TOPIC_COMMAND = f"garden/{TARGET_USER_ID}/{TARGET_DEVICE_ID}/command"

def on_connect(client, userdata, flags, rc, properties=None):
    if rc == 0:
        print(f"Połączono z brokerem! Subskrypcja tematów...")
        client.subscribe([(TOPIC_TELEMETRY, 0), (TOPIC_ALERT, 2)])
    else:
        print(f"Błąd połączenia, kod: {rc}")

def on_message(client, userdata, msg):
    try:
        payload_str = msg.payload.decode('utf-8')
        data = json.loads(payload_str)
        
        # --- OBSŁUGA ALERTÓW ---
        if "alert" in msg.topic:
            print("\n" + "!" * 50)
            print(f"OTRZYMANO ALERT Z URZĄDZENIA: {data.get('device')}")
            print(f"Typ: {data.get('type')}")
            print(f"Wiadomość: {data.get('msg')}")
            print("!" * 50 + "\n")
            
        # --- OBSŁUGA TELEMETRII ---
        elif "telemetry" in msg.topic:
            print(f"[{time.strftime('%H:%M:%S')}] Telemetria od {data.get('device')}:")
            sensors = data.get('sensors', {})
            
            # Wyświetlanie stanu wody (nowe pole)
            water_ok = sensors.get('water_tank_ok', 1)
            water_status = "OK" if water_ok else "BRAK WODY!"
            
            print(f"Wilgotność: {sensors.get('soil_moisture_pct')}%")
            print(f"Temperatura:       {sensors.get('air_temperature_c')} C")
            print(f"Wilgotność powietrza: {sensors.get('air_humidity_pct')}%")
            print(f"Ciśnienie:        {sensors.get('air_pressure_hpa')} hPa")
            print(f"Natężenie światła: {sensors.get('light_lux')} lx")
            print(f"Zbiornik:   {water_status}")
            print("-" * 30)
            
    except Exception as e:
        print(f"Błąd parsowania: {e}")

# Inicjalizacja klienta MQTT
client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, protocol=mqtt.MQTTv5)
client.username_pw_set(USER, PASSWORD)
client.on_connect = on_connect
client.on_message = on_message

print(f"Łączenie z {BROKER_ADDRESS}...")
client.connect(BROKER_ADDRESS, PORT, 60)

# Uruchamiamy pętlę sieciową w tle (non-blocking)
client.loop_start()

# Główna pętla symulująca wysyłanie komend przez użytkownika co pewien czas
try:
    cycle_counter = 0
    while True:
        time.sleep(1)
        cycle_counter += 1
        
        # Co 20 sekund wysyła komendę "odczytaj dane" (wymuszenie pomiaru)
        if cycle_counter % 20 == 0:
            print(f"\n[{time.strftime('%H:%M:%S')}] Wysyłam komendę: read_data")
            cmd_payload = json.dumps({"cmd": "read_data"})
            client.publish(TOPIC_COMMAND, cmd_payload)

        # Co 35 sekund wysyła komendę "podlej"
        if cycle_counter % 35 == 0:
            duration = random.randint(3, 8)
            print(f"\n[{time.strftime('%H:%M:%S')}] Wysyłam komendę: water_on ({duration}s)")
            cmd_payload = json.dumps({
                "cmd": "water_on", 
                "duration": duration
            })
            client.publish(TOPIC_COMMAND, cmd_payload)

except KeyboardInterrupt:
    print("\nZatrzymywanie klienta...")
    client.loop_stop()
    client.disconnect()