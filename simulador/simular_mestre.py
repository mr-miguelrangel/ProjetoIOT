"""Simula o nó mestre: publica o status e leituras aleatórias de no1, no2 e no3.

Uso:  python simular_mestre.py [endereco_do_broker]      (padrão: localhost)
"""
import json
import random
import sys
import time

import paho.mqtt.client as mqtt

BROKER = sys.argv[1] if len(sys.argv) > 1 else "localhost"
TOPICO_STATUS = "idc/mestre/status"
NOS = ["no1", "no2", "no3"]
INTERVALO_S = 5

cliente = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="simulador-mestre")
# Se o simulador cair sem se despedir, o broker publica "offline" por ele.
cliente.will_set(TOPICO_STATUS, "offline", qos=1, retain=True)
cliente.connect(BROKER, 1883)
cliente.loop_start()
cliente.publish(TOPICO_STATUS, "online", qos=1, retain=True)
print(f"Conectado a {BROKER}:1883. Ctrl+C para encerrar.")

try:
    while True:
        for no in NOS:
            leitura = {
                "temperatura": round(random.uniform(20, 30), 1),
                "umidade": round(random.uniform(40, 80), 1),
                "rssi": random.randint(-110, -60),
            }
            topico = f"idc/sensores/{no}/leituras"
            cliente.publish(topico, json.dumps(leitura))
            print(topico, leitura)
        time.sleep(INTERVALO_S)
except KeyboardInterrupt:
    cliente.publish(TOPICO_STATUS, "offline", qos=1, retain=True).wait_for_publish()
finally:
    cliente.loop_stop()
    cliente.disconnect()
