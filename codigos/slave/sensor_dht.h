#pragma once
#include <Arduino.h>

// Leitura do DHT11 do nó Slave.
// Usa o dht11_module.h (driver do DHT11); aqui só cuidamos do ritmo
// das leituras, sem nunca bloquear o loop do rádio por mais de uma leitura.

#define DHT_PIN 4  // GPIO de dados do DHT11

void sensor_inicializar();
void sensor_processar();           // Chamar no loop: lê o DHT11 quando chega a hora.

bool sensor_tem_leitura();         // Já houve pelo menos uma leitura válida?
bool sensor_ultima_falhou();       // A última tentativa falhou (valor exibido é antigo)?
float sensor_temperatura();        // °C da última leitura válida (NAN se nenhuma)
float sensor_umidade();            // % da última leitura válida (NAN se nenhuma)
uint32_t sensor_medida();          // Valor codificado para o slot (ver medida_sensor.h)
uint32_t sensor_idade_s();         // Segundos desde a última leitura válida (ou desde o boot)
