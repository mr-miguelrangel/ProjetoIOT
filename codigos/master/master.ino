/**
 * ============================================================================
 * CÓDIGO MASTER / GATEWAY DEDICADO - v5.6.0-MASTER-WEB
 * Baseado no Master LoRa original (recepção multi-slot + ACK), agora com:
 *  - Temperatura e umidade (DHT11) de cada nó, decodificadas do slot LoRa
 *  - Publicação MQTT para a etapa de nuvem (Mosquitto + Node-RED), seguindo
 *    o contrato do README: idc/sensores/no<ID>/leituras e idc/mestre/status
 *  - Endereço do broker MQTT configurável na página /wifi
 *  - Tabela dinâmica de nós (qualquer nó novo entra sozinho na lista)
 *  - Dashboard Web sempre ativo (WiFiServer puro, AJAX auto-refresh)
 *  - Rota /iot: JSON estrito com temperatura e umidade de todos os nós
 *  - Rota /info: telemetria completa (para alimentar o dashboard)
 *  - Painel de configuração (LoRa SF/Freq/TTL) via /ajustar
 *  - Configuração de Wi-Fi via /wifi (form) -> /wifi_salvar
 *  - AP de recuperação sempre ativo (WIFI_AP_STA), pois o Master não tem
 *    chave física de configuração como os Slaves — se a rede STA cair
 *    ou as credenciais estiverem erradas, dá pra entrar por
 *    "MASTER_CONFIG" / 192.168.4.1 e reconfigurar sem precisar tocar no
 *    hardware.
 * ============================================================================
 */

#include <Arduino.h>
#include "LoRaWan_APP.h"     // Driver de rádio oficial Heltec
#include "HT_SSD1306Wire.h"  // Driver de tela oficial Heltec
#include <WiFi.h>
#include <Preferences.h>
#include <base64.h>
#include "portal_seguro.h"
#include "perfil_wifi.h"
#include "config_radio.h"
#include "protocolo_mesh.h"
#include <atomic>
#include "http_master.h"
#include "medida_sensor.h"
#include <PubSubClient.h>

#define VERSAO "v5.6.0-MASTER-WEB"

// ================================================================
// --- CONFIGURAÇÕES FIXAS DO MASTER ---
// ================================================================
#define MEU_ID        101      // ID fixo do Master/Gateway (raiz da árvore mesh)
#define VEXT_PIN      21       // Pino de energia dos periféricos (rádio + OLED)
#define MAX_NOS       100      // Capacidade máxima da tabela de nós

#define RF_FREQUENCY_DEFAULT ConfigRadio::FREQUENCIA_PADRAO
#define LORA_SF_DEFAULT      ConfigRadio::SF_PADRAO

// O Master é sempre a raiz: nunca busca pai, está sempre "online" e com HB=0.
bool dispositivoOnline = true;

// ================================================================
// --- PARÂMETROS AJUSTÁVEIS (persistidos em NVS) ---
// ================================================================
uint8_t  loraSF          = LORA_SF_DEFAULT;
uint32_t rfFrequency     = RF_FREQUENCY_DEFAULT;
uint32_t ttlLimpezaS     = 86400;    // Tempo p/ remover nó "sumido" da tabela (24h padrão)
uint32_t onlineTimeoutMs = 180000;   // Nó é considerado ONLINE se ouvido há menos que isso (3 min padrão)

// ================================================================
// --- Wi-Fi (rede local / STA) ---
// ================================================================
WiFiServer server(80);
String wifiSsid, wifiPass;
IPAddress ip(10, 126, 12, 90), gw(10, 126, 12, 206), mask(255, 255, 255, 0), dns(8, 8, 8, 8);

// --- AP de recuperação (sempre ativo em paralelo, WIFI_AP_STA) ---
const char* AP_SSID = "MASTER_CONFIG";
static String senhaAp, credencialAdmin, tokenPortal;
IPAddress apIP(192, 168, 4, 1);

Preferences preferencias;

// ================================================================
// --- MQTT (etapa de nuvem: Mosquitto + Node-RED) ---
// ================================================================
// Broker vazio = MQTT desligado. Configurável em /wifi, gravado em NVS ("mqtt_config").
String mqttBroker;
uint16_t mqttPorta = 1883;
const char* MQTT_CLIENT_ID    = "idc-mestre";
const char* MQTT_TOPICO_STATUS = "idc/mestre/status";

// Uma leitura nova de um nó, a caminho do broker. O loop do rádio enfileira,
// a tarefa de rede publica: o rádio nunca espera por socket.
struct LeituraMqtt {
  uint16_t id;
  uint32_t medida;
  int16_t  rssi;
};
static QueueHandle_t filaMqtt = nullptr;
enum EstadoMqtt : uint8_t { MQTT_SEM_BROKER, MQTT_DESCONECTADO, MQTT_CONECTADO };
static std::atomic<uint8_t> estadoMqtt{MQTT_SEM_BROKER};

// ================================================================
// --- OLED ---
// ================================================================
SSD1306Wire telaMaster(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);

// ================================================================
// --- Formato de rádio V5.5.7 (idêntico ao Slave) ---
// ================================================================
struct __attribute__((__packed__)) SlotTelemetria {
  uint16_t idOrigem;
  uint16_t idDestinatario;
  uint32_t medida;          // Temperatura e umidade (medida_sensor.h)
  uint8_t  valoresPinos[5]; // Tipo e identificador no primeiro slot.
  uint8_t  hb;
  uint8_t  tempoDesdeUltimaMudanca[3];
  uint64_t revisaoOrigem;
};
static_assert(sizeof(SlotTelemetria) == 25, "Formato de slot LoRa inesperado");

// ================================================================
// --- Tabela Dinâmica de Nós Conhecidos pelo Master ---
// ================================================================
struct NodeInfo {
  uint16_t      id;
  uint32_t      medida;            // Temperatura e umidade (medida_sensor.h)
  uint8_t       hb;
  int16_t       rssi;
  int8_t        snr;
  uint32_t      latenciaSeg;       // "tempoDesdeUltimaMudanca" decodificado
  unsigned long ultimoContatoMs;   // millis() da última vez que este nó foi ouvido
  uint64_t      revisaoOrigem;
};
NodeInfo nos[MAX_NOS];
int totalNos = 0;

// O loop publica a tabela inteira. A tarefa de rede atende cada resposta
// com uma cópia própria, sem manter o lock durante operações de socket.
static portMUX_TYPE muxRede = portMUX_INITIALIZER_UNLOCKED;
static NodeInfo nosPublicados[MAX_NOS];
static int totalNosPublicados = 0;
static NodeInfo nosWeb[MAX_NOS];
static int totalNosWeb = 0;
static char enderecoStaOled[16] = "sem STA";

static void publicarNos() {
  portENTER_CRITICAL(&muxRede);
  totalNosPublicados = totalNos;
  memcpy(nosPublicados, nos, totalNos * sizeof(NodeInfo));
  portEXIT_CRITICAL(&muxRede);
}

static void copiarNosParaWeb() {
  portENTER_CRITICAL(&muxRede);
  totalNosWeb = totalNosPublicados;
  memcpy(nosWeb, nosPublicados, totalNosWeb * sizeof(NodeInfo));
  portEXIT_CRITICAL(&muxRede);
}

static ProtocoloMesh::CacheRecepcao cacheRecepcao;
static ProtocoloMesh::OfertaAgendada<SlotTelemetria> ofertaAgendada;
static std::atomic<uint32_t> timeoutTxPendente{0};
static RadioEvents_t RadioEvents;
static bool enviandoRespostaAuxiliar = false; // true enquanto envia ACK/resposta de SOS

// ================================================================
// --- Helpers de codificação 24 bits (idêntico ao restante do projeto) ---
// ================================================================
uint32_t decodificarUint24(const uint8_t* bytes) {
  return ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[1] << 8) | bytes[0];
}

// ================================================================
// --- Tabela de nós: inserir/atualizar e limpar inativos ---
// ================================================================
static int indiceNo(uint16_t id) {
  for (int i = 0; i < totalNos; ++i) if (nos[i].id == id) return i;
  return -1;
}

void atualizarOuInserirNo(uint16_t id, uint32_t medida, uint8_t hb, int16_t rssi, int8_t snr,
                         uint32_t latenciaSeg, uint64_t revisaoOrigem) {
  for (int i = 0; i < totalNos; i++) {
    if (nos[i].id == id) {
      nos[i].medida         = medida;
      nos[i].hb             = hb;
      nos[i].rssi           = rssi;
      nos[i].snr            = snr;
      nos[i].latenciaSeg    = latenciaSeg;
      nos[i].ultimoContatoMs = millis();
      nos[i].revisaoOrigem   = revisaoOrigem;
      return;
    }
  }
  // Nó novo: entra sozinho na lista, sem precisar de nenhuma configuração manual
  if (totalNos < MAX_NOS) {
    nos[totalNos].id             = id;
    nos[totalNos].medida         = medida;
    nos[totalNos].hb             = hb;
    nos[totalNos].rssi           = rssi;
    nos[totalNos].snr            = snr;
    nos[totalNos].latenciaSeg    = latenciaSeg;
    nos[totalNos].ultimoContatoMs = millis();
    nos[totalNos].revisaoOrigem   = revisaoOrigem;
    totalNos++;
    Serial.printf("[MASTER] Novo no detectado e registrado: %u (total=%d)\n", id, totalNos);
  } else {
    Serial.printf("[MASTER] Tabela cheia (%d nos). No %u descartado.\n", MAX_NOS, id);
  }
}

void limparNosInativos() {
  const int totalAnterior = totalNos;
  uint32_t limiteMs = ttlLimpezaS * 1000UL;
  for (int i = 0; i < totalNos; i++) {
    if (millis() - nos[i].ultimoContatoMs >= limiteMs) {
      Serial.printf("[MASTER] No %u removido da tabela por inatividade (TTL).\n", nos[i].id);
      for (int j = i; j < totalNos - 1; j++) nos[j] = nos[j + 1];
      totalNos--;
      i--;
    }
  }
  if (totalNos != totalAnterior) publicarNos();
}

// ================================================================
// --- Callbacks de Rádio LoRa ---
// ================================================================
void OnRxDone(uint8_t* payload, uint16_t size, int16_t rssi, int8_t snr) {
  if (enviandoRespostaAuxiliar) return;
  if (payload == nullptr || size < sizeof(SlotTelemetria) ||
      size > 6 * sizeof(SlotTelemetria) || size % sizeof(SlotTelemetria) != 0) {
    Radio.Rx(0);
    return;
  }

  const int quantidade = size / sizeof(SlotTelemetria);
  const auto* slots = (const SlotTelemetria*)payload;
  const auto& primeiro = slots[0];
  const uint16_t remetente = primeiro.idOrigem;
  if (!ProtocoloMesh::idValido(remetente) || remetente == MEU_ID) {
    Radio.Rx(0);
    return;
  }

  SlotTelemetria resposta = {};
  resposta.idOrigem = MEU_ID;
  resposta.idDestinatario = remetente;
  if (quantidade == 1 && primeiro.valoresPinos[0] == ProtocoloMesh::Busca &&
      primeiro.idDestinatario == 0 && primeiro.hb == 0 && primeiro.medida == 0 &&
      decodificarUint24(primeiro.tempoDesdeUltimaMudanca) == 0 && dispositivoOnline &&
      primeiro.revisaoOrigem > 0 && primeiro.revisaoOrigem <= UINT32_MAX) {
    ProtocoloMesh::marcar(resposta.valoresPinos, ProtocoloMesh::Oferta);
    resposta.revisaoOrigem = primeiro.revisaoOrigem;
    ofertaAgendada.agendar(resposta, millis(), random(70, ProtocoloMesh::limiteSorteioOfertaMs(loraSF)));
    Radio.Rx(0);
    return;
  } else {
    const uint32_t numero = ProtocoloMesh::sequencia(primeiro.valoresPinos);
    if (primeiro.valoresPinos[0] != ProtocoloMesh::Dados || numero == 0 ||
        primeiro.idDestinatario != MEU_ID || primeiro.hb != 1) {
      Radio.Rx(0);
      return;
    }

    if (!cacheRecepcao.repetido(remetente, numero)) {
      if (!ProtocoloMesh::aceitarBloco(slots, quantidade, MEU_ID, MAX_NOS - totalNos,
          [](uint16_t id) { return indiceNo(id) >= 0; })) {
        Radio.Rx(0);
        return;
      }
      for (int i = 0; i < quantidade; ++i) {
        const int existente = indiceNo(slots[i].idOrigem);
        if (existente >= 0 && slots[i].revisaoOrigem <= nos[existente].revisaoOrigem) continue;
        uint32_t latencia = decodificarUint24(slots[i].tempoDesdeUltimaMudanca);
        const uint32_t medida = slots[i].medida;
        if (MedidaSensor::valida(medida)) {
          Serial.printf("[TELEMETRIA] No %u | T=%.1f C | U=%.1f %% | HB=%u | RSSI=%d dBm | Idade=%us\n",
                        slots[i].idOrigem, MedidaSensor::temperatura(medida), MedidaSensor::umidade(medida),
                        slots[i].hb, rssi, latencia);
          // Fila cheia (broker lento ou fora): a leitura não é publicada, o rádio segue.
          const LeituraMqtt leitura = {slots[i].idOrigem, medida, rssi};
          if (filaMqtt && xQueueSend(filaMqtt, &leitura, 0) != pdTRUE)
            Serial.println("[MQTT] Fila cheia. Leitura descartada.");
        } else {
          Serial.printf("[TELEMETRIA] No %u | sem leitura do sensor | HB=%u | RSSI=%d dBm\n",
                        slots[i].idOrigem, slots[i].hb, rssi);
        }
        atualizarOuInserirNo(slots[i].idOrigem, medida, slots[i].hb, rssi, snr,
                            latencia, slots[i].revisaoOrigem);
      }
      publicarNos();
      cacheRecepcao.registrar(remetente, numero);
    }

    ProtocoloMesh::marcar(resposta.valoresPinos, ProtocoloMesh::Ack, numero);
    delay(random(10, 60));
  }

  enviandoRespostaAuxiliar = true;
  Radio.Send((uint8_t*)&resposta, sizeof(resposta));
}

static void processarOfertaMaster() {
  if (enviandoRespostaAuxiliar || !ofertaAgendada.pronta(millis(), loraSF)) return;
  SlotTelemetria resposta = ofertaAgendada.pacote;
  ofertaAgendada.cancelar();
  enviandoRespostaAuxiliar = true;
  Radio.Send((uint8_t*)&resposta, sizeof(resposta));
}

void OnTxDone(void) {
  timeoutTxPendente.store(0);
  enviandoRespostaAuxiliar = false;
  Radio.Rx(0);
}

void OnTxTimeout(void) {
  timeoutTxPendente.store(1);
}

void OnRxTimeout(void) {}
void OnRxError(void) {}

// ================================================================
// --- NVS: carregar/salvar configurações ---
// ================================================================
void carregarConfiguracoesNVS() {
  preferencias.begin("wifi_config", true);
  PerfilWifi perfil = {};
  const bool perfilValido = preferencias.isKey("perfil") &&
      preferencias.getBytes("perfil", &perfil, sizeof(perfil)) == sizeof(perfil) && perfil.camposValidos();
  wifiSsid = perfilValido ? String(perfil.ssid) : preferencias.getString("ssid", "");
  wifiPass = perfilValido ? String(perfil.senha) : preferencias.getString("pass", "");
  String ipStr   = perfilValido ? String(perfil.ip) : preferencias.getString("ip", "10.126.12.90");
  String gwStr   = perfilValido ? String(perfil.gateway) : preferencias.getString("gw", "10.126.12.206");
  String maskStr = perfilValido ? String(perfil.mascara) : preferencias.getString("mask", "255.255.255.0");
  ip.fromString(ipStr);
  gw.fromString(gwStr);
  mask.fromString(maskStr);
  preferencias.end();

  preferencias.begin("mqtt_config", true);
  mqttBroker = preferencias.getString("broker", "");
  mqttPorta  = preferencias.getUShort("porta", 1883);
  preferencias.end();
  if (!mqttPorta) mqttPorta = 1883;

  preferencias.begin("mesh_config", true);
  ttlLimpezaS     = preferencias.getUInt("ttl", 86400);
  onlineTimeoutMs = preferencias.getUInt("online_ms", 180000);
  preferencias.end();
  if (!ttlLimpezaS || ttlLimpezaS > INT32_MAX / 1000UL) ttlLimpezaS = 86400;
  if (!onlineTimeoutMs || onlineTimeoutMs > INT32_MAX) onlineTimeoutMs = 180000;

  String imagemFirmware = ESP.getSketchMD5();
  if (!ConfigRadio::carregar(preferencias, "mesh_config", "sf", loraSF, rfFrequency, imagemFirmware.c_str())) {
    Serial.println("[NVS] Nao foi possivel confirmar o perfil de radio. Nova tentativa no proximo boot.");
  }
  Serial.println("[NVS] Configurações carregadas.");
}

// ================================================================
// --- Rádio: inicializar/aplicar parâmetros mesh ---
// ================================================================
void iniciarRadioMesh() {
  RadioEvents.RxDone    = OnRxDone;
  RadioEvents.TxDone    = OnTxDone;
  RadioEvents.TxTimeout = OnTxTimeout;
  RadioEvents.RxTimeout = OnRxTimeout;
  RadioEvents.RxError   = OnRxError;

  Radio.Init(&RadioEvents);
  Radio.SetChannel(rfFrequency);

  // Parâmetros espelhados com o Slave (mesh_lora.cpp): CR(4/8=4) e SymbTimeout(0) idênticos
  Radio.SetRxConfig(MODEM_LORA, 0, loraSF, 4, 0, 8, 0, false, 0, true, 0, 0, false, true);
  Radio.SetTxConfig(MODEM_LORA, 14, 0, 0, loraSF, 4, 8, false, true, 0, 0, false, 12000);

  Serial.printf("[MASTER] Radio iniciado. SF=%u | Freq=%lu Hz. Escutando malha...\n", loraSF, rfFrequency);
  Radio.Rx(0);
}

// ================================================================
// --- Wi-Fi: conectar STA + manter AP de recuperação sempre ativo ---
// ================================================================
bool iniciarAPRecuperacao() {
  if (!senhaAp.length() || !WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0)) ||
      !WiFi.softAP(AP_SSID, senhaAp.c_str())) {
    WiFi.enableAP(false);
    return false;
  }
  Serial.printf("[WIFI] AP de recuperacao ativo: SSID='%s' | IP=%s\n", AP_SSID, apIP.toString().c_str());
  return true;
}

void processarWiFi() {
  constexpr unsigned long INTERVALO_MS = 15000;
  static unsigned long ultimaTentativa = millis() - INTERVALO_MS;
  static bool estavaConectado = false;
  const bool conectado = WiFi.status() == WL_CONNECTED;

  if (conectado != estavaConectado) {
    char endereco[sizeof(enderecoStaOled)] = "sem STA";
    if (conectado) {
      snprintf(endereco, sizeof(endereco), "%s", WiFi.localIP().toString().c_str());
      Serial.printf("[WIFI] Conectado em %s. RSSI: %d dBm\n", endereco, WiFi.RSSI());
    } else {
      Serial.println("[WIFI] STA desconectada. AP de recuperação disponível.");
    }
    portENTER_CRITICAL(&muxRede);
    memcpy(enderecoStaOled, endereco, sizeof(endereco));
    portEXIT_CRITICAL(&muxRede);
    estavaConectado = conectado;
  }

  if (!conectado && wifiSsid.length() && millis() - ultimaTentativa >= INTERVALO_MS) {
    ultimaTentativa = millis();
    Serial.printf("[WIFI] Tentando conexão STA com %s.\n", wifiSsid.c_str());
    WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());
  }
}

// ================================================================
// --- MQTT: conexão com o broker e publicação das leituras ---
// ================================================================
// Roda só na tarefa de rede. Contrato (README do repositório):
//   idc/sensores/no<ID>/leituras  {"temperatura":25.0,"umidade":61.0,"rssi":-87}
//   idc/mestre/status             "online" (retain) / Last Will "offline" (retain)
static WiFiClient clienteMqtt;
static PubSubClient mqtt(clienteMqtt);

static void descartarFilaMqtt() {
  if (!filaMqtt) return;
  LeituraMqtt leitura;
  while (xQueueReceive(filaMqtt, &leitura, 0) == pdTRUE) {}
}

static void publicarLeitura(const LeituraMqtt& leitura) {
  char topico[40], payload[80];
  snprintf(topico, sizeof(topico), "idc/sensores/no%u/leituras", leitura.id);
  snprintf(payload, sizeof(payload), "{\"temperatura\":%.1f,\"umidade\":%.1f,\"rssi\":%d}",
           MedidaSensor::temperatura(leitura.medida), MedidaSensor::umidade(leitura.medida), leitura.rssi);
  if (mqtt.publish(topico, payload)) Serial.printf("[MQTT] %s %s\n", topico, payload);
  else Serial.printf("[MQTT] Falha ao publicar em %s.\n", topico);
}

void processarMqtt() {
  constexpr unsigned long INTERVALO_RECONEXAO_MS = 10000;
  static unsigned long ultimaTentativa = millis() - INTERVALO_RECONEXAO_MS;

  if (!mqttBroker.length()) {
    estadoMqtt.store(MQTT_SEM_BROKER);
    descartarFilaMqtt();
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    if (mqtt.connected()) mqtt.disconnect();
    estadoMqtt.store(MQTT_DESCONECTADO);
    descartarFilaMqtt();  // Sem rede: não acumula leituras velhas para publicar depois.
    return;
  }

  if (!mqtt.connected()) {
    estadoMqtt.store(MQTT_DESCONECTADO);
    if (millis() - ultimaTentativa < INTERVALO_RECONEXAO_MS) {
      descartarFilaMqtt();
      return;
    }
    ultimaTentativa = millis();
    Serial.printf("[MQTT] Conectando em %s:%u...\n", mqttBroker.c_str(), mqttPorta);
    mqtt.setServer(mqttBroker.c_str(), mqttPorta);
    mqtt.setSocketTimeout(3);
    // Last Will: se o Master cair, o broker publica "offline" (retain) por ele.
    if (!mqtt.connect(MQTT_CLIENT_ID, nullptr, nullptr, MQTT_TOPICO_STATUS, 1, true, "offline")) {
      Serial.printf("[MQTT] Falha na conexao (estado %d). Nova tentativa em 10 s.\n", mqtt.state());
      descartarFilaMqtt();
      return;
    }
    mqtt.publish(MQTT_TOPICO_STATUS, "online", true);
    Serial.println("[MQTT] Conectado. Status 'online' publicado.");
  }

  estadoMqtt.store(MQTT_CONECTADO);
  mqtt.loop();
  LeituraMqtt leitura;
  while (filaMqtt && mqtt.connected() && xQueueReceive(filaMqtt, &leitura, 0) == pdTRUE) publicarLeitura(leitura);
}

static const char* textoEstadoMqtt() {
  switch (estadoMqtt.load()) {
    case MQTT_CONECTADO:    return "conectado";
    case MQTT_DESCONECTADO: return "desconectado";
    default:                return "sem broker";
  }
}

// ================================================================
// --- HTTP: helpers de parsing (mesmo padrão usado no restante do projeto) ---
// ================================================================
static bool numeroFormulario(const String& corpo, const char* nome, uint32_t minimo,
                             uint32_t maximo, uint32_t& resultado) {
  String valor;
  bool presente;
  return HttpMaster::campo(corpo, nome, valor, presente) &&
         (!presente || ConfigRadio::numeroValido(valor.c_str(), minimo, maximo, resultado));
}

void responderOk(WiFiClient &c, const char* tipo = "application/json") {
  c.println("HTTP/1.1 200 OK");
  c.printf("Content-Type: %s\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n", tipo);
}

String formatarUptime() {
  unsigned long s = millis() / 1000;
  unsigned long h = s / 3600;
  unsigned long m = (s % 3600) / 60;
  unsigned long seg = s % 60;
  char buf[32];
  sprintf(buf, "%luh %lum %lus", h, m, seg);
  return String(buf);
}

// ================================================================
// --- Montagem do JSON de nós (reutilizado por /iot e /info) ---
// ================================================================
// Temperatura/umidade do nó, ou null se ele ainda não mandou leitura válida.
static void escreverMedidaJson(WiFiClient &c, uint32_t medida) {
  if (MedidaSensor::valida(medida)) {
    c.printf("\"temperatura\":%.1f,\"umidade\":%.1f",
             MedidaSensor::temperatura(medida), MedidaSensor::umidade(medida));
  } else {
    c.print("\"temperatura\":null,\"umidade\":null");
  }
}

void escreverJsonNos(WiFiClient &c, bool apenasMedidas) {
  c.print("[");
  for (int i = 0; i < totalNosWeb; i++) {
    if (i > 0) c.print(",");
    unsigned long ultimoContatoS = (millis() - nosWeb[i].ultimoContatoMs) / 1000UL;
    bool online = (millis() - nosWeb[i].ultimoContatoMs) < onlineTimeoutMs;

    c.printf("{\"id\":%u,", nosWeb[i].id);
    escreverMedidaJson(c, nosWeb[i].medida);
    if (apenasMedidas) {
      c.print("}");
    } else {
      c.printf(
        ",\"hb\":%u,\"rssi\":%d,\"snr\":%d,"
        "\"idade_leitura_s\":%u,\"ultimo_contato_s\":%lu,\"online\":%s}",
        nosWeb[i].hb, nosWeb[i].rssi, nosWeb[i].snr,
        nosWeb[i].latenciaSeg, ultimoContatoS, online ? "true" : "false"
      );
    }
  }
  c.print("]");
}

// ================================================================
// --- Página HTML do Dashboard ---
// ================================================================
String gerarPaginaDashboard(bool configurar = false) {
  String html = R"rawliteral(
  <!DOCTYPE html><html><head><meta charset='UTF-8'>
  <meta name='viewport' content='width=device-width, initial-scale=1.0'>
  <title>IDC | Master/Gateway )rawliteral" + String(VERSAO) + R"rawliteral(</title>
  <style>
    body { font-family: 'Segoe UI', Arial, sans-serif; background: #f0f2f5; margin: 0; padding: 20px; color: #333; }
    .container { max-width: 1000px; margin: 0 auto; }
    .header { background: #002d62; color: white; padding: 20px; border-radius: 8px; margin-bottom: 20px; display: flex; justify-content: space-between; align-items: center; box-shadow: 0 4px 6px rgba(0,0,0,0.1); flex-wrap: wrap; gap: 10px; }
    .header h1 { margin: 0; font-size: 24px; }
    .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(180px, 1fr)); gap: 15px; margin-bottom: 20px; }
    .card { background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.05); text-align: center; border-top: 4px solid #0056b3; }
    .card.green { border-top-color: #28a745; }
    .card.yellow { border-top-color: #ffc107; }
    .card h3 { margin: 0 0 10px 0; color: #666; font-size: 13px; text-transform: uppercase; letter-spacing: 0.5px; }
    .card .val { font-size: 28px; font-weight: bold; color: #111; }
    .panel { background: white; padding: 25px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.05); margin-bottom: 20px; }
    .panel h2 { margin-top: 0; color: #002d62; font-size: 18px; border-bottom: 2px solid #f0f2f5; padding-bottom: 10px; }
    table { width: 100%; border-collapse: collapse; font-size: 14px; }
    th, td { text-align: left; padding: 8px 10px; border-bottom: 1px solid #f0f2f5; }
    th { color: #666; text-transform: uppercase; font-size: 11px; }
    .badge { padding: 3px 8px; border-radius: 10px; font-size: 11px; font-weight: bold; color: white; }
    .badge.on { background: #28a745; }
    .badge.off { background: #dc3545; }
    .form-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(180px, 1fr)); gap: 15px; align-items: end; }
    .form-group { display: flex; flex-direction: column; }
    label { font-size: 13px; font-weight: bold; margin-bottom: 6px; color: #555; }
    input[type='number'], input[type='text'], input[type='password'] { padding: 10px; border: 1px solid #ccc; border-radius: 4px; font-size: 15px; }
    .btn { background: #0056b3; color: white; border: none; padding: 11px 20px; border-radius: 4px; font-size: 15px; font-weight: bold; cursor: pointer; }
    .btn:hover { background: #004085; }
    small.muted { color: #888; }
  </style></head><body>
  <div class='container'>
    <div class='header'>
      <div><h1>IDC Master / Gateway</h1><small>Dashboard Central da Mesh LoRa • )rawliteral" + String(VERSAO) + R"rawliteral(</small></div>
      <div>Uptime: <strong id='lbl-uptime'>--</strong></div>
    </div>

    <div class='grid'>
      <div class='card green'><h3>Nós Online</h3><div class='val' id='lbl-online'>0</div></div>
      <div class='card'><h3>Nós Totais</h3><div class='val' id='lbl-total'>0</div></div>
      <div class='card'><h3>IP do Master</h3><div class='val' id='lbl-ip' style='font-size:18px;'>--</div></div>
      <div class='card yellow'><h3>SF LoRa</h3><div class='val' id='lbl-sf'>--</div></div>
      <div class='card'><h3>MQTT</h3><div class='val' id='lbl-mqtt' style='font-size:18px;'>--</div></div>
    </div>

    <div class='panel'>
      <h2>Nós da Rede</h2>
      <table>
        <thead><tr><th>ID</th><th>Temperatura</th><th>Umidade</th><th>HB</th><th>RSSI</th><th>Último contato</th><th>Status</th></tr></thead>
        <tbody id='tbody-nos'><tr><td colspan='7'>Aguardando dados...</td></tr></tbody>
      </table>
      <p style='font-size:12px; color:#888; margin-top:15px;'>
        Endpoints: <a href='/iot' target='_blank'>/iot</a> (temperatura e umidade de todos os nós, JSON simples) |
        <a href='/info' target='_blank'>/info</a> (telemetria completa) |
        <a href='/config'>Configurações do Master</a>
      </p>
    </div>

    )rawliteral";
  if (configurar) html += R"rawliteral(
    <div class='panel'>
      <h2>Configurações da Mesh LoRa</h2>
      <form action='/ajustar' method='POST'>
        <input type='hidden' name='token' value=')rawliteral" + tokenPortal + R"rawliteral('>
        <div class='form-grid'>
          <div class='form-group'><label>Spreading Factor (SF)</label>
            <input type='number' name='sf' min='7' max='12' value=')rawliteral" + String(loraSF) + R"rawliteral('></div>
          <div class='form-group'><label>Frequência (Hz)</label>
            <input type='number' name='freq' min='902000000' max='928000000' value=')rawliteral" + String(rfFrequency) + R"rawliteral('></div>
          <div class='form-group'><label>TTL Limpeza de Nó (min)</label>
            <input type='number' name='ttl_min' value=')rawliteral" + String(ttlLimpezaS / 60) + R"rawliteral('></div>
          <div class='form-group'><label>Timeout Online (s)</label>
            <input type='number' name='online_s' value=')rawliteral" + String(onlineTimeoutMs / 1000) + R"rawliteral('></div>
          <input type='submit' class='btn' value='Salvar e Reiniciar'>
        </div>
      </form>
      <p><a href='/wifi'>Configurar rede Wi-Fi</a></p>
      <p><small class='muted'>Alterar SF/Frequência exige que os nós estejam com os mesmos valores, ou eles deixam de se comunicar com o Master.</small></p>
    </div>
    )rawliteral";
  html += R"rawliteral(
  </div>

  <script>
    function atualizarDados() {
      fetch('/info').then(r => r.json()).then(data => {
        document.getElementById('lbl-uptime').innerText = data.uptime_str;
        document.getElementById('lbl-ip').innerText = data.master_ip;
        document.getElementById('lbl-sf').innerText = data.sf;
        document.getElementById('lbl-total').innerText = data.total_nos;
        document.getElementById('lbl-online').innerText = data.nos_online;
        document.getElementById('lbl-mqtt').innerText = data.mqtt;

        var tbody = document.getElementById('tbody-nos');
        if (!data.nodes || data.nodes.length === 0) {
          tbody.innerHTML = '<tr><td colspan="7">Nenhum no registrado ainda.</td></tr>';
          return;
        }
        var linhas = '';
        data.nodes.forEach(function(n) {
          var badge = n.online ? '<span class="badge on">ONLINE</span>' : '<span class="badge off">OFFLINE</span>';
          var t = n.temperatura === null ? '--' : n.temperatura.toFixed(1) + ' °C';
          var u = n.umidade === null ? '--' : n.umidade.toFixed(0) + ' %';
          linhas += '<tr><td>' + n.id + '</td><td>' + t + '</td><td>' + u + '</td><td>' + n.hb + '</td><td>' + n.rssi + ' dBm</td><td>' + n.ultimo_contato_s + 's atrás</td><td>' + badge + '</td></tr>';
        });
        tbody.innerHTML = linhas;
      }).catch(err => console.log('Sincronizando...'));
    }
    setInterval(atualizarDados, 2000);
    atualizarDados();
  </script>
  </body></html>)rawliteral";

  return html;
}

// Endereço do broker: IP (ex.: 192.168.0.10) ou nome de host. Vazio = MQTT desligado.
static bool brokerValido(const String& host) {
  if (host.length() > 63) return false;
  for (size_t i = 0; i < host.length(); ++i) {
    const char c = host[i];
    if (!isalnum((unsigned char)c) && c != '.' && c != '-') return false;
  }
  return true;
}

// ================================================================
// --- Página HTML de configuração de Wi-Fi ---
// ================================================================
String gerarPaginaWifi() {
  String ssidSeguro = wifiSsid;
  ssidSeguro.replace("&", "&amp;");
  ssidSeguro.replace("'", "&#39;");
  ssidSeguro.replace("<", "&lt;");
  ssidSeguro.replace(">", "&gt;");
  String html = R"rawliteral(
  <!DOCTYPE html><html><head><meta charset='UTF-8'>
  <meta name='viewport' content='width=device-width, initial-scale=1.0'>
  <title>IDC | Config Wi-Fi Master</title>
  <style>
    body{font-family:'Segoe UI',sans-serif;background:#f4f6f9;margin:0;padding:20px;display:flex;justify-content:center;align-items:center;min-height:100vh;}
    .card{background:#fff;padding:30px;border-radius:8px;box-shadow:0 4px 12px rgba(0,0,0,0.1);width:100%;max-width:400px;}
    .header{text-align:center;margin-bottom:25px;border-bottom:2px solid #0056b3;padding-bottom:10px;}
    .header h2{color:#0056b3;margin:0;}
    label{display:block;margin-top:12px;font-weight:bold;font-size:14px;color:#555;}
    input{width:100%;padding:10px;margin-top:6px;border:1px solid #ccc;border-radius:4px;box-sizing:border-box;}
    input[type='submit']{width:100%;background:#0056b3;color:white;border:none;padding:12px;border-radius:4px;font-weight:bold;cursor:pointer;margin-top:20px;}
  </style></head><body>
  <div class='card'>
    <div class='header'><h2>IDC Master</h2><span>Configuração de Rede Wi-Fi</span></div>
    <form action='/wifi_salvar' method='POST'>
      <input type='hidden' name='token' value=')rawliteral" + tokenPortal + R"rawliteral('>
      <label>SSID</label><input name='ssid' value=')rawliteral" + ssidSeguro + R"rawliteral(' required>
      <label>Nova senha</label><input name='pass' type='password' autocomplete='new-password'>
      <small>Para o mesmo SSID, deixe em branco para manter a senha atual.</small>
      <label><input name='aberta' type='checkbox' value='1'> Rede sem senha</label>
      <label>IP Fixo</label><input name='ip' value=')rawliteral" + ip.toString() + R"rawliteral('>
      <label>Gateway</label><input name='gw' value=')rawliteral" + gw.toString() + R"rawliteral('>
      <label>Subnet</label><input name='mask' value=')rawliteral" + mask.toString() + R"rawliteral('>
      <label>Broker MQTT (IP do computador com o Docker)</label><input name='broker' maxlength='63' placeholder='ex.: 192.168.0.10' value=')rawliteral" + mqttBroker + R"rawliteral('>
      <small>Deixe em branco para desligar o envio MQTT.</small>
      <label>Porta MQTT</label><input name='porta' type='number' min='1' max='65535' value=')rawliteral" + String(mqttPorta) + R"rawliteral('>
      <input type='submit' value='Salvar e Reiniciar'>
    </form>
  </div></body></html>)rawliteral";
  return html;
}

String gerarPaginaSucesso(const char* titulo, const char* mensagem) {
  String html = R"rawliteral(
  <!DOCTYPE html><html><head><meta charset='UTF-8'>
  <meta name='viewport' content='width=device-width, initial-scale=1.0'>
  <title>)rawliteral" + String(titulo) + R"rawliteral(</title>
  <style>
    body { font-family: 'Segoe UI', Arial, sans-serif; background: #f0f2f5; display: flex; justify-content: center; align-items: center; height: 100vh; margin: 0; }
    .card-sucesso { background: white; padding: 40px; border-radius: 8px; box-shadow: 0 4px 12px rgba(0,0,0,0.1); text-align: center; max-width: 420px; width: 90%; border-top: 6px solid #28a745; }
    .icone { font-size: 50px; color: #28a745; margin-bottom: 15px; font-weight: bold; }
    h2 { margin: 0 0 12px 0; color: #002d62; font-size: 22px; }
    p { color: #555; font-size: 14px; line-height: 1.6; margin: 0 0 25px 0; }
    .spinner { border: 4px solid #f3f3f3; border-top: 4px solid #002d62; border-radius: 50%; width: 30px; height: 30px; animation: spin 1s linear infinite; margin: 0 auto; }
    @keyframes spin { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }
  </style></head><body>
    <div class='card-sucesso'>
      <div class='icone'>✓</div>
      <h2>)rawliteral" + String(titulo) + R"rawliteral(</h2>
      <p>)rawliteral" + String(mensagem) + R"rawliteral(</p>
      <div class='spinner'></div>
    </div>
  </body></html>)rawliteral";
  return html;
}

// ================================================================
// --- Roteador HTTP principal ---
// ================================================================
void atenderCliente(WiFiClient &c) {
  if (!c) return;

  String req, body;
  String autorizacao;
  const auto leitura = HttpMaster::ler(c, req, body, autorizacao);
  if (leitura != HttpMaster::Resultado::Completa) {
    if (leitura == HttpMaster::Resultado::Expirada) {
      c.println("HTTP/1.1 408 Request Timeout\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
    } else if (leitura == HttpMaster::Resultado::Grande) {
      c.println("HTTP/1.1 413 Content Too Large\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
    } else if (leitura == HttpMaster::Resultado::Invalida) {
      c.println("HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
    }
    c.stop();
    return;
  }

  const bool post = req.startsWith("POST ");
  const int inicioAlvo = post ? 5 : 4;
  const String alvo = req.substring(inicioAlvo, req.indexOf(' ', inicioAlvo));
  const int separadorQuery = alvo.indexOf('?');
  const String rota = separadorQuery < 0 ? alvo : alvo.substring(0, separadorQuery);
  const bool salva = rota == "/ajustar" || rota == "/wifi_salvar";
  const bool protegido = salva || rota == "/config" || rota == "/wifi";
  if (salva && !post) {
    c.print("HTTP/1.1 405 Method Not Allowed\r\nAllow: POST\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
    c.stop();
    return;
  }
  if (protegido && !PortalSeguro::confere(autorizacao, credencialAdmin)) {
    c.print("HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Basic realm=\"Master LoRa\"\r\n"
            "Cache-Control: no-store\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
    c.stop();
    return;
  }
  if (salva) {
    String token;
    bool presente;
    if (!HttpMaster::campo(body, "token", token, presente) || !PortalSeguro::confere(token, tokenPortal)) {
      c.print("HTTP/1.1 403 Forbidden\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
      c.stop();
      return;
    }
  }

  // --- /ajustar : parâmetros da mesh LoRa ---
  if (post && rota == "/ajustar") {
    uint32_t novoSF = loraSF;
    uint32_t novaFreq = rfFrequency;
    uint32_t novoTtlMin = ttlLimpezaS / 60UL, novoOnlineS = onlineTimeoutMs / 1000UL;
    if (!numeroFormulario(body, "sf", 7, 12, novoSF) ||
        !numeroFormulario(body, "freq", ConfigRadio::FREQUENCIA_MIN, ConfigRadio::FREQUENCIA_MAX, novaFreq) ||
        !numeroFormulario(body, "ttl_min", 1, INT32_MAX / 60000UL, novoTtlMin) ||
        !numeroFormulario(body, "online_s", 1, INT32_MAX / 1000UL, novoOnlineS)) {
      c.println("HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain; charset=utf-8\r\nConnection: close\r\n\r\nParametros LoRa invalidos.");
      c.stop();
      return;
    }
    const bool abriu = preferencias.begin("mesh_config", false);
    const bool gravou = abriu &&
        preferencias.putUChar("sf", (uint8_t)novoSF) == sizeof(uint8_t) &&
        preferencias.putULong("freq", novaFreq) == sizeof(uint32_t) &&
        preferencias.putUInt("ttl", novoTtlMin * 60UL) == sizeof(uint32_t) &&
        preferencias.putUInt("online_ms", novoOnlineS * 1000UL) == sizeof(uint32_t);
    if (abriu) preferencias.end();
    if (!gravou) {
      c.print("HTTP/1.1 500 Internal Server Error\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
      c.stop();
      return;
    }

    responderOk(c, "text/html");
    c.println(gerarPaginaSucesso("Configurações Salvas!", "Os parâmetros da mesh LoRa foram gravados. O Master está reiniciando para aplicar as alterações."));
    c.stop();
    delay(1500);
    ESP.restart();
    return;
  }

  else if (!post && rota == "/config") {
    responderOk(c, "text/html");
    c.println(gerarPaginaDashboard(true));
  }

  // --- /wifi : formulário de rede ---
  else if (!post && rota == "/wifi") {
    responderOk(c, "text/html");
    c.println(gerarPaginaWifi());
  }

  // --- /wifi_salvar : grava credenciais/IP e reinicia ---
  else if (post && rota == "/wifi_salvar") {
    String ssid, senha, aberta, novoIp, novoGw, novaMask, novoBroker;
    bool presente, brokerPresente;
    IPAddress endereco;
    uint32_t novaPorta = mqttPorta;
    if (!HttpMaster::campo(body, "ssid", ssid, presente) || !ssid.length() || ssid.length() > 32 ||
        !HttpMaster::campo(body, "pass", senha, presente) || senha.length() > 64 ||
        !HttpMaster::campo(body, "aberta", aberta, presente) || (presente && aberta != "1") ||
        !HttpMaster::campo(body, "ip", novoIp, presente) ||
            (presente && (novoIp.length() > 15 || novoIp.indexOf(':') >= 0 || !endereco.fromString(novoIp))) ||
        !HttpMaster::campo(body, "gw", novoGw, presente) ||
            (presente && (novoGw.length() > 15 || novoGw.indexOf(':') >= 0 || !endereco.fromString(novoGw))) ||
        !HttpMaster::campo(body, "mask", novaMask, presente) ||
            (presente && (novaMask.length() > 15 || novaMask.indexOf(':') >= 0 || !endereco.fromString(novaMask))) ||
        !HttpMaster::campo(body, "broker", novoBroker, brokerPresente) || !brokerValido(novoBroker) ||
        !numeroFormulario(body, "porta", 1, 65535, novaPorta)) {
      c.println("HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
      c.stop();
      return;
    }

    if (aberta == "1") senha = "";
    else if (!senha.length() && ssid == wifiSsid) senha = wifiPass;
    else if (!senha.length()) {
      c.println("HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\nInforme a senha ou marque Rede sem senha.");
      c.stop();
      return;
    }
    if (senha.length() && (senha.length() < 8 || senha.length() > 64 ||
        (senha.length() == 64 && strspn(senha.c_str(), "0123456789ABCDEFabcdef") != 64))) {
      c.println("HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\nSenha Wi-Fi invalida.");
      c.stop();
      return;
    }
    if (!novoIp.length()) novoIp = ip.toString();
    if (!novoGw.length()) novoGw = gw.toString();
    if (!novaMask.length()) novaMask = mask.toString();
    PerfilWifi perfil = {};
    snprintf(perfil.ssid, sizeof(perfil.ssid), "%s", ssid.c_str());
    snprintf(perfil.senha, sizeof(perfil.senha), "%s", senha.c_str());
    snprintf(perfil.ip, sizeof(perfil.ip), "%s", novoIp.c_str());
    snprintf(perfil.gateway, sizeof(perfil.gateway), "%s", novoGw.c_str());
    snprintf(perfil.mascara, sizeof(perfil.mascara), "%s", novaMask.c_str());
    if (!brokerPresente) novoBroker = mqttBroker;
    const bool abriu = preferencias.begin("wifi_config", false);
    bool gravou = abriu && preferencias.putBytes("perfil", &perfil, sizeof(perfil)) == sizeof(perfil);
    if (abriu) preferencias.end();
    const bool abriuMqtt = gravou && preferencias.begin("mqtt_config", false);
    // putString de texto vazio grava 0 bytes: aceita, é o MQTT desligado.
    gravou = abriuMqtt &&
        preferencias.putString("broker", novoBroker) == novoBroker.length() &&
        preferencias.putUShort("porta", (uint16_t)novaPorta) == sizeof(uint16_t);
    if (abriuMqtt) preferencias.end();
    if (!gravou) {
      c.print("HTTP/1.1 500 Internal Server Error\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
      c.stop();
      return;
    }

    responderOk(c, "text/html");
    c.println(gerarPaginaSucesso("Rede Salva!", "As credenciais, o IP e o broker MQTT foram gravados. O Master está reiniciando para conectar na nova rede."));
    c.stop();
    delay(1500);
    ESP.restart();
    return;
  }

  // --- /iot : JSON simples com temperatura e umidade de todos os nós ---
  else if (!post && rota == "/iot") {
    copiarNosParaWeb();
    responderOk(c, "application/json");
    c.print("{\"nodes\":");
    escreverJsonNos(c, true);
    c.println("}");
  }

  // --- /info : telemetria completa (usada pelo dashboard) ---
  else if (!post && rota == "/info") {
    copiarNosParaWeb();
    responderOk(c, "application/json");

    int nosOnline = 0;
    for (int i = 0; i < totalNosWeb; i++) {
      if (millis() - nosWeb[i].ultimoContatoMs < onlineTimeoutMs) nosOnline++;
    }

    c.printf(
      "{\"master_ip\":\"%s\",\"wifi_rssi\":%d,\"sf\":%u,\"freq\":%lu,"
      "\"ttl_limpeza_s\":%lu,\"online_timeout_ms\":%lu,\"uptime_str\":\"%s\","
      "\"total_nos\":%d,\"nos_online\":%d,\"mqtt\":\"%s\",\"mqtt_broker\":\"%s\",\"mqtt_porta\":%u,\"nodes\":",
      WiFi.localIP().toString().c_str(), WiFi.RSSI(), loraSF, rfFrequency,
      ttlLimpezaS, onlineTimeoutMs, formatarUptime().c_str(),
      totalNosWeb, nosOnline, textoEstadoMqtt(), mqttBroker.c_str(), mqttPorta
    );
    escreverJsonNos(c, false);
    c.println("}");
  }

  // --- / : dashboard principal ---
  else if (!post && (rota == "/" || rota == "/index" || rota == "/index.html")) {
    responderOk(c, "text/html");
    c.println(gerarPaginaDashboard());
  }

  else {
    c.print("HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
  }

  c.stop();
}

// ================================================================
// --- OLED: status resumido (o detalhe por nó agora vive no dashboard web) ---
// ================================================================
void atualizarDisplay() {
  int nosOnline = 0;
  for (int i = 0; i < totalNos; i++) {
    if (millis() - nos[i].ultimoContatoMs < onlineTimeoutMs) nosOnline++;
  }

  telaMaster.clear();
  telaMaster.setFont(ArialMT_Plain_10);
  const uint8_t mqttAgora = estadoMqtt.load();
  telaMaster.drawString(0, 0, "MASTER SF" + String(loraSF) + " | MQTT " +
                              (mqttAgora == MQTT_CONECTADO ? "OK" : mqttAgora == MQTT_DESCONECTADO ? "--" : "OFF"));
  char endereco[sizeof(enderecoStaOled)];
  portENTER_CRITICAL(&muxRede);
  memcpy(endereco, enderecoStaOled, sizeof(endereco));
  portEXIT_CRITICAL(&muxRede);
  telaMaster.drawString(0, 16, "IP: " + String(endereco));
  telaMaster.drawString(0, 32, "AP: " + String(AP_SSID));
  telaMaster.drawString(0, 48, "Nos: " + String(nosOnline) + " online / " + String(totalNos) + " total");
  telaMaster.display();
}

// ================================================================
// --- Setup ---
// ================================================================
void tarefaRede(void*) {
  constexpr unsigned long INTERVALO_AP_MS = 15000;
  WiFi.mode(WIFI_STA);
  senhaAp = PortalSeguro::carregarSenha("senha_ap");
  const String senhaAdmin = PortalSeguro::carregarSenha("senha_admin");
  tokenPortal = PortalSeguro::gerarChave();
  if (senhaAp.length()) Serial.printf("[PORTAL] Wi-Fi %s, senha: %s\n", AP_SSID, senhaAp.c_str());
  if (senhaAdmin.length()) {
    String codificada = base64::encode(String("admin:") + senhaAdmin);
    if (codificada.length()) credencialAdmin = String("Basic ") + codificada;
    Serial.printf("[PORTAL] Administrador: admin, senha: %s\n", senhaAdmin.c_str());
  }
  bool apAtivo = iniciarAPRecuperacao();
  unsigned long ultimaTentativaAP = millis();
  if (!apAtivo) Serial.println("[WIFI] Falha ao iniciar AP. Nova tentativa em 15 s.");
  if (!WiFi.config(ip, gw, mask, dns, gw)) {
    Serial.println("[WIFI] Erro ao configurar IP fixo.");
  }
  server.begin();

  for (;;) {
    if (!apAtivo && millis() - ultimaTentativaAP >= INTERVALO_AP_MS) {
      apAtivo = iniciarAPRecuperacao();
      ultimaTentativaAP = millis();
      if (!apAtivo) Serial.println("[WIFI] Falha ao iniciar AP. Nova tentativa em 15 s.");
    }
    processarWiFi();
    processarMqtt();
    WiFiClient cliente = server.available();
    if (cliente) atenderCliente(cliente);
    vTaskDelay(1);
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n==================================================");
  Serial.printf("   Master/Gateway IDC IoT - %s\n", VERSAO);
  Serial.println("==================================================");

  pinMode(VEXT_PIN, OUTPUT);
  digitalWrite(VEXT_PIN, LOW);
  delay(100);

  Mcu.begin(21, 0);

  telaMaster.init();
  telaMaster.flipScreenVertically();
  telaMaster.setFont(ArialMT_Plain_10);
  telaMaster.clear();
  telaMaster.drawString(0, 0, "MASTER ONLINE");
  telaMaster.drawString(0, 20, "Iniciando...");
  telaMaster.display();

  carregarConfiguracoesNVS();
  filaMqtt = xQueueCreate(40, sizeof(LeituraMqtt));
  if (!filaMqtt) Serial.println("[MQTT] Falha ao criar a fila. Leituras nao serao publicadas.");
  iniciarRadioMesh();
  const BaseType_t nucleoRede = portNUM_PROCESSORS > 1 ? 1 - xPortGetCoreID() : 0;
  if (xTaskCreatePinnedToCore(tarefaRede, "rede-master", 8192, nullptr, 1, nullptr, nucleoRede) != pdPASS) {
    Serial.println("[MASTER] Falha ao iniciar tarefa de rede.");
  }

  Serial.println("[MASTER] Setup concluido. Escutando mesh + servindo dashboard web.");
}

// ================================================================
// --- Loop ---
// ================================================================
void loop() {
  Radio.IrqProcess(); // Prioridade: processar o rádio LoRa
  if (timeoutTxPendente.exchange(0)) OnTxDone();
  processarOfertaMaster();

  static unsigned long ultimoTick = 0;

  if (millis() - ultimoTick >= 1000) {
    ultimoTick = millis();
    limparNosInativos();
    atualizarDisplay();
  }

}
