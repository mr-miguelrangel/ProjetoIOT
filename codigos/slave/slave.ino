/**
 * ============================================================================
 * NÓ SLAVE - TELEMETRIA DE TEMPERATURA E UMIDADE (DHT11) VIA LORA MESH
 * ============================================================================
 * @file        slave.ino
 * Lê o DHT11 e envia temperatura/umidade pela malha LoRa até o Master.
 * O protocolo mesh é o mesmo do Master: a medida (temperatura e umidade)
 * vai nos 4 bytes de medida de cada slot (ver medida_sensor.h).
 * ============================================================================
 */

// --- 1. BIBLIOTECAS NATIVAS DO SISTEMA ---
#include <Arduino.h>
#include "LoRaWan_APP.h"     // Driver de rádio oficial Heltec
#include "HT_SSD1306Wire.h"  // Driver de tela oficial Heltec
#include <Preferences.h>
#include "config_radio.h"

// --- 2. MÓDULOS ISOLADOS DO PROJETO ---
#include "interface_portal.h"
#include "perfil_sono.h"
#include "mesh_lora.h"
#include "sensor_dht.h"

// --- 3. CONFIGURAÇÕES E DEFINIÇÕES DE HARDWARE ---
#define BOTAO_PGR_PIN 0
#define VEXT_PIN 21  // Pino de energia dos periféricos (Vext) na V3

// --- 4. ALOCAÇÃO DE VARIÁVEIS GLOBAIS OPERACIONAIS ---
uint16_t MEU_ID = 1;
String parentID = "";
uint8_t loraSF = ConfigRadio::SF_PADRAO;
uint32_t rfFrequency = ConfigRadio::FREQUENCIA_PADRAO;
String criterioConexao = "RSSI";
int16_t rssiMinimo = -120;
uint32_t timeoutPaiMs = 86400000UL; // CORRIGIDO: Padrão 24 horas (em ms)
uint16_t tempoAtivoMin = PerfilSono::ATIVO_PADRAO;
uint16_t tempoSleepMin = PerfilSono::SONO_PADRAO;
bool modoCampoAtivo = false;
bool pontaComSono = false;
extern const unsigned long TEMPO_CICLO_MS = 60UL * 1000UL;

static unsigned long ultimoFiltroSincronismo = 0;
// Instanciação nativa usando os pinos corretos de fábrica da Heltec V3
SSD1306Wire displayPrincipal(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n[MAIN] --- Inicializando Firmware " VERSAO_SLAVE " Heltec V3 (DHT11) ---");

    // Inicialização automática nativa da Heltec V3
    Mcu.begin(21, 0); // Ajustado com IDs numéricos estáveis

    // Configuração do botão PGR físico
    pinMode(BOTAO_PGR_PIN, INPUT_PULLUP);

    // Inicialização da tela OLED nativa
    displayPrincipal.init();
    displayPrincipal.clear();
    displayPrincipal.setFont(ArialMT_Plain_10);
    displayPrincipal.drawString(0, 0, "BOOT HELTEC V3 " VERSAO_SLAVE);
    displayPrincipal.display();

    // --- Recuperação de parâmetros com as chaves do Portal ---
    Preferences prefs;
    prefs.begin("config_params", true);
    
    if (prefs.isKey("id_no"))      MEU_ID = prefs.getUShort("id_no", 102);
    if (prefs.isKey("critConex"))  criterioConexao = prefs.getString("critConex", "RSSI");
    if (prefs.isKey("rssiMin"))    rssiMinimo = prefs.getShort("rssiMin", -120);
    if (prefs.isKey("tPaiMs"))     timeoutPaiMs = prefs.getUInt("tPaiMs", 86400000UL); // Padrão 24h
    if (criterioConexao != "RSSI" && criterioConexao != "Saltos") criterioConexao = "RSSI";
    if (rssiMinimo < -150 || rssiMinimo > 0) rssiMinimo = -120;
    if (timeoutPaiMs < 60000 || timeoutPaiMs > INT32_MAX) timeoutPaiMs = 86400000UL;
    if (prefs.isKey("mCampo"))     modoCampoAtivo = prefs.getBool("mCampo", false);
    PerfilSono::carregar(prefs, pontaComSono, tempoAtivoMin, tempoSleepMin);
    
    prefs.end();

    String imagemFirmware = ESP.getSketchMD5();
    if (!ConfigRadio::carregar(prefs, "config_params", "loraSF", loraSF, rfFrequency, imagemFirmware.c_str())) {
        Serial.println("[NVS] Nao foi possivel confirmar o perfil de radio. Nova tentativa no proximo boot.");
    }

    Serial.printf("[MAIN] Papel LoRa: %s.\n", pontaComSono ? "ponta com sono" : "repetidor");

    // --- Inicialização do Rádio Mesh LoRa ---
    Serial.println("[MAIN] Inicializando rádio Mesh...");
    lora_mesh_inicializar(&displayPrincipal);

    // --- Inicialização do Sensor de Temperatura e Umidade ---
    Serial.println("[MAIN] Inicializando sensor DHT11...");
    sensor_inicializar();

    Serial.println("[MAIN] Setup Concluido com sucesso!");
}

void loop() {
  if (modoConfiguracaoAtivo) {
    interface_processarPortal();
    return;
  }

  if (!lora_mesh_radio_dormindo()) Radio.IrqProcess();
  
  sensor_processar();
  
  lora_mesh_processar();
}
