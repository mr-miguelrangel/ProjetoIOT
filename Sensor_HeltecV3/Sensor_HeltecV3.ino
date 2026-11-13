/**
 * Nó transmissor: Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262) + DHT11
 * Biblioteca: "Heltec_ESP32_LoRa_v3" (Rop Gonggrijp) -> traz o objeto `radio` (RadioLib).
 * Também instale: "DHT sensor library" e "Adafruit Unified Sensor".
 * Placa na IDE: "Heltec WiFi LoRa 32(V3) / Wireless shell(V3) / ..." (ver README da lib).
 */
#include <heltec_unofficial.h>
#include "DHT11Module.h"

#define NODE_ID        0x01
#define DHT_PIN        4          // confira no pinout da V3 se está livre
#define SEND_INTERVAL  30000UL    // ms

// Parâmetros de rádio: devem ser IGUAIS no transmissor e no receptor
#define LORA_FREQ      915.0      // MHz (Brasil: 915-928 MHz)
#define LORA_BW        125.0      // kHz
#define LORA_SF        9
#define LORA_TX_POWER  14         // dBm

DHT11Module dht11(DHT_PIN);
FramePacker packer(NODE_ID);

// Para adicionar sensores: basta incluir aqui (ISensor*).
ISensor *sensors[] = { &dht11 };
const size_t NUM_SENSORS = sizeof(sensors) / sizeof(sensors[0]);

uint32_t lastSend = 0;

void setup() {
  heltec_setup();                       // Serial 115200, display, botão, LED
  Serial.println("\n=== No LoRa + DHT11 (Heltec V3) ===");

  RADIOLIB_OR_HALT(radio.begin());
  RADIOLIB_OR_HALT(radio.setFrequency(LORA_FREQ));
  RADIOLIB_OR_HALT(radio.setBandwidth(LORA_BW));
  RADIOLIB_OR_HALT(radio.setSpreadingFactor(LORA_SF));
  RADIOLIB_OR_HALT(radio.setOutputPower(LORA_TX_POWER));

  for (size_t i = 0; i < NUM_SENSORS; i++) sensors[i]->begin();
}

void loop() {
  heltec_loop();                        // trata o botão PRG etc.

  if (millis() - lastSend >= SEND_INTERVAL) {
    lastSend = millis();

    for (size_t i = 0; i < NUM_SENSORS; i++) {
      SensorStatus st = sensors[i]->read();
      if (st != SENSOR_OK) Serial.printf("Sensor %u: erro %u\n", (unsigned)i, st);
      sensors[i]->printLast(Serial);

      uint8_t frame[64];
      size_t len = packer.pack(*sensors[i], frame, sizeof(frame));
      if (len == 0) continue;

      heltec_led(50);                   // LED aceso durante a transmissão
      int state = radio.transmit(frame, len);
      heltec_led(0);

      if (state == RADIOLIB_ERR_NONE) Serial.printf("TX OK, %u bytes\n", (unsigned)len);
      else                            Serial.printf("TX falhou, codigo %d\n", state);
    }
  }
}
