#ifndef INTERFACE_PORTAL_H
#define INTERFACE_PORTAL_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

// --- Variáveis Globais de Controle de Estado ---
extern bool modoConfiguracaoAtivo; // Sinaliza se o portal AP + DNS estão rodando

// --- Parâmetros Operacionais do Sistema (Definidos no main.cpp / .ino) ---
// 1. Sensor: DHT11, sem parâmetros configuráveis (ver sensor_dht.h)

// 2. Conectividade LoRa Mesh
// 🔹 CORREÇÃO: Declaração externa de MEU_ID adicionada para resolver o erro de compilação do .cpp
extern uint16_t MEU_ID; 
extern String parentID;
extern uint8_t loraSF;
extern uint32_t rfFrequency;
extern String criterioConexao;
extern int16_t rssiMinimo;
extern uint32_t timeoutPaiMs;

// 3. Gerenciamento de Energia e Operação
extern uint16_t tempoAtivoMin;
extern uint16_t tempoSleepMin;
extern bool modoCampoAtivo;
extern bool pontaComSono;

// --- Funções do Módulo ---

/**
 * @brief Inicializa o Access Point e o Servidor DNS para o Modo Configuração.
 */
void interface_iniciarModoConfiguracao();

/**
 * @brief Processa as requisições de rede do Portal. Deve ser chamada no loop principal.
 */
void interface_processarPortal();

/**
 * @brief Carrega os parâmetros operacionais da Flash (Preferences).
 */
void interface_carregarConfiguracoes();

#endif  // INTERFACE_PORTAL_H
