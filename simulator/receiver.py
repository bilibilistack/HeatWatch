import time
import os
import paho.mqtt.client as mqtt
from dotenv import load_dotenv

def on_message(client, userdata, message):
    print(f"\n[{time.strftime('%H:%M:%S')}] Received message on topic {message.topic}:")
    print(message.payload.decode('utf-8'))

def main():
    env_path = os.path.join(os.path.dirname(__file__), '.env')
    load_dotenv(env_path)
    
    broker = os.getenv('MQTT_BROKER', 'broker.hivemq.com')
    port = int(os.getenv('MQTT_PORT', '1883'))
    topic = os.getenv('MQTT_TOPIC', 'v1/devices/me/telemetry')
    username = os.getenv('MQTT_USER', '')
    password = os.getenv('MQTT_PASS', '')
    
    client = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION2)
    client.on_message = on_message
    
    if username:
        client.username_pw_set(username, password)
        
    print(f"--- MQTT Receiver Listening on {broker}:{port} ---")
    print(f"Subscribing to: {topic}")
    
    try:
        client.connect(broker, port, 60)
        client.subscribe(topic)
        client.loop_forever()
    except KeyboardInterrupt:
        print("\nReceiver stopped.")
    except Exception as e:
        print(f"Error: {e}")
    finally:
        client.disconnect()

if __name__ == "__main__":
    main()
