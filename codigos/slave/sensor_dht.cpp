#include "sensor_dht.h"
#include "dht11_module.h"
#include "medida_sensor.h"

extern bool modoCampoAtivo;

// Uma tentativa por vez: se falhar, tenta de novo no próximo ciclo curto,
// em vez de ficar esperando dentro do loop (o rádio precisa do loop livre).
static DHT11Module dht11(DHT_PIN, 1, 1100);

static const unsigned long INTERVALO_LEITURA_MS = 10000;  // Leitura normal
static const unsigned long INTERVALO_FALHA_MS = 3000;     // Nova tentativa após falha

static unsigned long ultimaTentativaMs = 0;
static unsigned long proximaEsperaMs = 2000;  // DHT11 precisa de ~1 s após energizar
static unsigned long ultimaLeituraValidaMs = 0;
static bool temLeitura = false;
static bool ultimaFalhou = false;

void sensor_inicializar() {
  dht11.begin();
  ultimaTentativaMs = millis();
  ultimaLeituraValidaMs = millis();
  if (!modoCampoAtivo) Serial.printf("[SENSOR] DHT11 no pino %d.\n", DHT_PIN);
}

void sensor_processar() {
  if (millis() - ultimaTentativaMs < proximaEsperaMs) return;
  ultimaTentativaMs = millis();

  if (dht11.read() == SENSOR_OK) {
    temLeitura = true;
    ultimaFalhou = false;
    ultimaLeituraValidaMs = millis();
    proximaEsperaMs = INTERVALO_LEITURA_MS;
    if (!modoCampoAtivo) dht11.printLast(Serial);
  } else {
    ultimaFalhou = true;
    proximaEsperaMs = INTERVALO_FALHA_MS;
    if (!modoCampoAtivo) Serial.println("[SENSOR] Falha na leitura do DHT11. Nova tentativa em 3 s.");
  }
}

bool sensor_tem_leitura() { return temLeitura; }
bool sensor_ultima_falhou() { return ultimaFalhou; }
float sensor_temperatura() { return temLeitura ? dht11.temperature() : NAN; }
float sensor_umidade() { return temLeitura ? dht11.humidity() : NAN; }

uint32_t sensor_medida() {
  return temLeitura ? MedidaSensor::codificar(dht11.temperature(), dht11.humidity())
                    : MedidaSensor::semLeitura();
}

uint32_t sensor_idade_s() {
  return (millis() - ultimaLeituraValidaMs) / 1000UL;
}
