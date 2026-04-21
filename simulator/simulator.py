import json
import time
import random
import os
import math
import paho.mqtt.client as mqtt
from dotenv import load_dotenv

def calculate_wet_bulb(ta, rh):
    """Stull's formula for wet-bulb temperature approximation"""
    tw = ta * math.atan(0.151977 * math.pow(rh + 8.313659, 0.5)) + \
         math.atan(ta + rh) - math.atan(rh - 1.676331) + \
         0.00391838 * math.pow(rh, 1.5) * math.atan(0.023101 * rh) - 4.686035
    return tw

def get_risk_level(wbgt, di):
    if wbgt >= 30 or di >= 32:
        return 2
    elif (23 <= wbgt < 30) or (24 <= di < 32):
        return 1
    return 0

def simulate_data():
    device_id = os.getenv('DEVICE_ID', 'HW_GRP10_001')
    heat_stress = os.getenv('SIMULATE_HEAT_STRESS', 'false').lower() == 'true'
    fall_detected = os.getenv('SIMULATE_FALL', 'false').lower() == 'true'
    
    if heat_stress:
        hr = random.randint(100, 145)
        skin_temp = round(random.uniform(37.0, 39.5), 1)
        ambient_temp = round(random.uniform(35.0, 45.0), 1)
        humidity = round(random.uniform(60.0, 85.0), 1)
    else:
        hr = random.randint(65, 95)
        skin_temp = round(random.uniform(33.0, 36.5), 1)
        ambient_temp = round(random.uniform(22.0, 32.0), 1)
        humidity = round(random.uniform(30.0, 55.0), 1)

    pressure = round(random.uniform(1010.0, 1020.0), 1)
    tw = calculate_wet_bulb(ambient_temp, humidity)
    wbgt = round(0.7 * tw + 0.3 * ambient_temp, 1)
    di = round(0.5 * (ambient_temp + tw), 1)
    risk_level = get_risk_level(wbgt, di)
    battery = float(os.getenv('BATTERY_LEVEL', '0.9'))
    
    payload = {
        "deviceId": device_id,
        "timestamp": int(time.time() * 1000),
        "values": {
            "heartRateAvg": hr,
            "skinTemp": skin_temp,
            "ambientTemp": ambient_temp,
            "humidity": humidity,
            "pressure": pressure,
            "HeatStressIndex": wbgt,
            "discomfortIndex": di,
            "riskLevel": risk_level,
            "fallDetected": fall_detected,
            "battery": battery
        }
    }
    return payload

def main():
    env_path = os.path.join(os.path.dirname(__file__), '.env')
    load_dotenv(env_path)
    
    broker = os.getenv('MQTT_BROKER', 'broker.hivemq.com')
    port = int(os.getenv('MQTT_PORT', '1883'))
    topic = os.getenv('MQTT_TOPIC', 'v1/devices/me/telemetry')
    username = os.getenv('MQTT_USER', '')
    password = os.getenv('MQTT_PASS', '')
    send_interval = int(os.getenv('SEND_INTERVAL', '5'))
    
    client = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION2)
    
    if username:
        client.username_pw_set(username, password)
        
    print(f"--- HeatWatch MQTT Simulator Started ---")
    print(f"Broker: {broker}:{port}")
    print(f"Topic: {topic}")
    print(f"Interval: {send_interval}s")
    print(f"----------------------------------------")
    
    try:
        client.connect(broker, port, 60)
        client.loop_start()
        
        while True:
            # Reload env every cycle to allow dynamic flag changes
            load_dotenv(env_path, override=True)
            
            data = simulate_data()
            message = json.dumps(data)
            
            result = client.publish(topic, message)
            status = result[0]
            if status == 0:
                print(f"[{time.strftime('%H:%M:%S')}] Published to {topic}: HR={data['values']['heartRateAvg']}, Risk={data['values']['riskLevel']}")
            else:
                print(f"Failed to send message to topic {topic}")
            
            time.sleep(send_interval)
            
    except KeyboardInterrupt:
        print("\nSimulator stopped.")
    except Exception as e:
        print(f"Error: {e}")
    finally:
        client.loop_stop()
        client.disconnect()

if __name__ == "__main__":
    main()
