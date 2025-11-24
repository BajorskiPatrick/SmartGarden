import paho.mqtt.client as mqtt
import json
import time

# Konfiguracja
BROKER_ADDRESS = "172.20.10.2" # Zmień na IP swojego komputera/brokera
TOPIC_ROOT = "garden/+/+/telemetry" # '+' to wildcard - odbiera od wszystkich userów i urządzeń

def on_connect(client, userdata, flags, rc):
    print(f"Połączono z brokerem! Kod: {rc}")
    client.subscribe(TOPIC_ROOT)
    print(f"Nasłuchiwanie na: {TOPIC_ROOT}")

def on_message(client, userdata, msg):
    try:
        # Dekodowanie JSON
        payload_str = msg.payload.decode('utf-8')
        data = json.loads(payload_str)
        
        print("-" * 40)
        print(f"Odebrano z tematu: {msg.topic}")
        print(f"Użytkownik: {data.get('user')}")
        print(f"Urządzenie: {data.get('device')}")
        print("Sensory:")
        sensors = data.get('sensors', {})
        print(f"  Wilgotność gleby: {sensors.get('soil_moisture_pct')}%")
        print(f"  Światło:          {sensors.get('light_lux')} lx")
        print(f"  Temperatura:      {sensors.get('air_temperature_c')} C")
        print("-" * 40)
        
    except Exception as e:
        print(f"Błąd parsowania danych: {e}")

client = mqtt.Client(protocol=mqtt.MQTTv5)
# Jeśli ustawiłeś hasło w Mosquitto:
client.username_pw_set("admin", "admin")

client.on_connect = on_connect
client.on_message = on_message

client.connect(BROKER_ADDRESS, 1883, 60)
client.loop_forever()