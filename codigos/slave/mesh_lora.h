#ifndef MESH_LORA_H
#define MESH_LORA_H

#include <Arduino.h>
#include "LoRaWan_APP.h"     // Driver de rádio oficial Heltec
#include "HT_SSD1306Wire.h"  // Driver de tela oficial Heltec
#include <Preferences.h>

// --- Configurações Físicas de Hardware ---
#define PIN_BOTAO_PGR 0
#define MAX_SLOTS_BANCO 5

// Versão exibida no boot, na tela SISTEMA e no Serial.
#define VERSAO_SLAVE "v5.6.0"

// 🔹 BO-3: struct LoRaPacket (antiga, não usada) removida daqui. O pacote real
// de 25 bytes trafegado hoje é o SlotTelemetria, definido e usado só dentro de
// mesh_lora.cpp.

// --- Funções do Módulo ---
void lora_mesh_inicializar(SSD1306Wire* displayExterno);
void lora_mesh_processar();
bool lora_mesh_radio_dormindo();
void lora_mesh_receber_pacote(uint8_t* payload, uint16_t size, int16_t rssi, int8_t snr);

#endif // MESH_LORA_H
