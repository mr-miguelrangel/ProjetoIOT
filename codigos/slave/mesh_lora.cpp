#include "mesh_lora.h"
#include "protocolo_mesh.h"
#include "revisao_origem.h"
#include <esp_random.h>
#include <atomic>
#include "sensor_dht.h"
#include "medida_sensor.h"
#include <Preferences.h>

// 🔹 BO-4 (nota geral): este arquivo só é compilado no firmware do nó Slave
// (V5_5_9S.ino). O Master roda um .ino totalmente separado (V5_5_9M.ino), com
// MEU_ID fixo em 101. Por isso, todo "if (MEU_ID == 0)" / "if (MEU_ID != 0)"
// que existia neste arquivo era resíduo de uma arquitetura antiga (onde o ID 0
// era reservado ao Master) e nunca era/deixava de ser executado na prática —
// foram removidos para simplificar a leitura do código.

// Referências globais externas vindas do main
extern uint16_t MEU_ID;
extern bool modoCampoAtivo;
extern uint8_t loraSF;
extern uint32_t rfFrequency;
extern const unsigned long TEMPO_CICLO_MS;
extern String parentID;
extern String criterioConexao;
extern int16_t rssiMinimo;
extern bool pontaComSono;
extern uint16_t tempoAtivoMin;
extern uint16_t tempoSleepMin;

// --- CONFIGURAÇÕES GLOBAIS DE REDE ---
uint32_t CONFIG_TTL_LIMPEZA_S = 86400;  // 24 horas padrão
static const unsigned long INTERVALO_BLOCOS_MS = 10000;

// --- Formato de rádio V5.5.7 ---
struct __attribute__((__packed__)) SlotTelemetria {
  uint16_t idOrigem;                   // Bytes 0 e 1
  uint16_t idDestinatario;             // Bytes 2 e 3
  uint32_t medida;                     // Bytes 4 a 7: temperatura e umidade (medida_sensor.h)
  uint8_t valoresPinos[5];             // Tipo e identificador no primeiro slot.
  uint8_t hb;                          // Byte 13
  uint8_t tempoDesdeUltimaMudanca[3];  // Bytes 14, 15 e 16 (Tempo de latência em 24 bits)
  uint64_t revisaoOrigem;              // Telemetria: época e amostra. Busca/oferta: número da busca.
};
static_assert(sizeof(SlotTelemetria) == 25, "Formato de slot LoRa inesperado");

// --- Estrutura de Envio Agregado: 6 Slots de 25 Bytes = 150 Bytes ---
struct __attribute__((__packed__)) PacoteSeisSlots {
  SlotTelemetria slots[6];
};

// Máquina de estados do Rádio MESH Hierárquico
enum EstadoRadioMesh { MESH_AGUARDANDO,
                       MESH_BUSCANDO,
                       MESH_TRANSMITINDO,
                       MESH_RECEBENDO };
static EstadoRadioMesh estadoMeshAtual = MESH_AGUARDANDO;

// Controle de Topologia e Conexão
static bool dispositivoOnline = false;
static bool radioDormindo = false;
static bool cicloConfirmadoAgora = false;
static uint32_t inicioFaseRadioMs = 0;
static uint16_t meuPaiID = 65535;
static uint32_t numeroBusca = 0;
static uint32_t epocaOrigem = 0;
static uint32_t amostraOrigem = 0;
static unsigned long inicioJanelaBuscaMs = 0;
static ProtocoloMesh::OfertaAgendada<SlotTelemetria> ofertaAgendada;

// --- Controle de ACK / Timeout de Pai ---
static unsigned long ultimoContatoPaiMs = 0;  // Carimbo do último ACK recebido do pai atual
static bool enviandoRespostaAuxiliar = false; // Flag genérica: true enquanto envia ACK/resposta de SOS
                                               // (garante que o OnTxDone devolva o rádio pro RX
                                               //  independente do estado da máquina principal)
static bool paiRespondendo = false;
static uint32_t timestampSucessoMs = 0;

// Variáveis de eleição na janela de busca
static uint16_t melhorPaiID = 65535;
static uint8_t melhorPaiHB = 255;
static int16_t melhorPaiRSSI = -150;

// --- Base de Dados Dinâmica (Gerencia até 100 nós) ---
#define MAX_NOS_BASE 100
static uint16_t paisPerdidos[MAX_NOS_BASE];
static int totalPaisPerdidos = 0;
static SlotTelemetria baseDispositivos[MAX_NOS_BASE];
static unsigned long instanteRecebimentoFilhoMs[MAX_NOS_BASE];  // Carimbo para cálculo de atraso
static unsigned long timestampUltimoContatoMs[MAX_NOS_BASE];    // Monitoramento TTL
static int nosRegistradosNaBase = 1;                            // Index 0 = Próprio nó

// Controle de retransmissão fracionada
static int indiceBaseEnvioAtual = 0;
static unsigned long ultimoEnvioPacoteMs = 0;
static bool transmissaoEmAndamento = false;

// Estado do transporte por salto.
static uint32_t versoesFilhos[MAX_NOS_BASE] = {};
static uint32_t ultimaVersaoFilho = 0;
static ProtocoloMesh::CacheRecepcao cacheRecepcao;
static PacoteSeisSlots blocoEmVoo;
static uint32_t versoesEmVoo[6];
static int slotsEmVoo = 0;
static uint8_t tentativasBloco = 0;
static bool transmitindoDados = false;
static bool aguardandoAck = false;
static std::atomic<uint32_t> timeoutTxPendente{0};
static unsigned long esperaBlocoMs = 0;
static const uint8_t MAX_TENTATIVAS_BLOCO = 3;
// Fim do estado do transporte por salto.

// Estados da Máquina de Telas e Display
static SSD1306Wire* display = nullptr;
enum TipoTela { TELA_HOME,
                TELA_TABELA,
                TELA_SISTEMA };
static TipoTela telaAtual = TELA_HOME;
static int indiceScrollTabela = 0;           // Controla qual nó inicia o desenho
static unsigned long ultimoScrollMs = 0;     // Temporizador para andar 1 nó por segundo

// --- Variáveis de Controle do Scroll de Diagnósticos (Tela 3) ---
static int indiceScrollSistema = 0;          // Controla qual linha de dados inicia o desenho (0 a 4)
static const int MAX_ITENS_SISTEMA = 7;      // Total de itens de diagnóstico cadastrados

// --- Variáveis de Controle do Menu Secreto de Configuração ---
static bool menuConfiguracaoAtivo = false;   // Define se estamos exibindo o menu secundário
static int linhaSelecionadaMenu = 0;         // Seta indicadora do menu (0: Alterar Modo, 1: Portal AP, 2: Voltar)
static const int MAX_LINHAS_MENU = 3;        // Total de opções do menu

// --- Variáveis de Detecção do Triplo Clique (PGR) ---
static int contadorCliquesPGR = 0;
static unsigned long janelaCliquesMs = 0;

static bool displayAtivo = false;
static unsigned long displayTimeout = 0;
static const unsigned long JANELA_2_MIN = 120000;

// Máquina de estados do botão PGR
static bool emModoEdicao = false;
static bool selecaoModoCampoEdicao = false;

// Variáveis de Diagnóstico / Sincronismo Visual
static String statusUltimoEnvio = "AGUARDANDO";
static uint32_t cronometroProximoEnvioSeg = 42;
static unsigned long ultimoIncrementoUptime = 0;
static uint32_t uptimeMinutos = 0;

unsigned long lastTxAttempt = 0;
unsigned long proximoIntervalo = 60000;

static Preferences prefInterna;

// Protótipos das funções internas
static void renderizarHome();
static void renderizarTabela();
static void renderizarSistema();
static void gerenciarTimeoutDisplay();
static void tratarBotaoPGR();
static void dispararProximoBlocoBase();
static void processarEnvioPendente();

// Função auxiliar para codificar uint32 em 3 bytes
static void codificarUint24(uint32_t valor, uint8_t* destino) {
  destino[0] = (uint8_t)(valor & 0xFF);
  destino[1] = (uint8_t)((valor >> 8) & 0xFF);
  destino[2] = (uint8_t)((valor >> 16) & 0xFF);
}

// Função auxiliar para decodificar 3 bytes em uint32
static uint32_t decodificarUint24(const uint8_t* origem) {
  return ((uint32_t)origem[2] << 16) | ((uint32_t)origem[1] << 8) | (uint32_t)origem[0];
}

static int indiceFilho(uint16_t id) {
  for (int i = 1; i < nosRegistradosNaBase; ++i)
    if (baseDispositivos[i].idOrigem == id) return i;
  return -1;
}

static int ordemFalha(uint16_t id) {
  for (int i = 0; i < totalPaisPerdidos; ++i)
    if (paisPerdidos[i] == id) return i;
  return -1;
}

static void perderPai() {
  Serial.printf("[MESH] Rota pelo pai %u perdida. Leituras mantidas na fila.\n", meuPaiID);
  int posicao = ordemFalha(meuPaiID);
  if (posicao < 0) {
    posicao = totalPaisPerdidos < MAX_NOS_BASE ? totalPaisPerdidos++ : 0;
  }
  for (int i = posicao; i + 1 < totalPaisPerdidos; ++i) paisPerdidos[i] = paisPerdidos[i + 1];
  paisPerdidos[totalPaisPerdidos - 1] = meuPaiID;
  ofertaAgendada.cancelar();
  parentID = "";
  dispositivoOnline = false;
  paiRespondendo = false;
  baseDispositivos[0].hb = 0;
  meuPaiID = 65535;
  slotsEmVoo = 0;
  aguardandoAck = false;
  transmissaoEmAndamento = false;
  estadoMeshAtual = MESH_AGUARDANDO;
  statusUltimoEnvio = "FALHA";
  cronometroProximoEnvioSeg = (uint32_t)random(2, 6);
}

static void confirmarBloco() {
  for (int s = 1; s < slotsEmVoo; ++s) {
    int i = indiceFilho(blocoEmVoo.slots[s].idOrigem);
    if (i >= 0 && versoesFilhos[i] == versoesEmVoo[s]) versoesFilhos[i] = 0;
  }
  slotsEmVoo = 0;
  aguardandoAck = false;
  ultimoEnvioPacoteMs = millis();
  esperaBlocoMs = INTERVALO_BLOCOS_MS;
  if (indiceBaseEnvioAtual >= nosRegistradosNaBase) {
    transmissaoEmAndamento = false;
    estadoMeshAtual = MESH_RECEBENDO;
    statusUltimoEnvio = "SUCESSO";
    timestampSucessoMs = millis();
    cicloConfirmadoAgora = true;
  }
}

void OnTxDone(void) {
  timeoutTxPendente.store(0);
  if (transmitindoDados) {
    transmitindoDados = false;
    aguardandoAck = true;
    ultimoEnvioPacoteMs = millis();
    // Em SF alto, um bloco cheio ocupa vários segundos no ar.
    esperaBlocoMs = (unsigned long)random(3500, loraSF >= 10 ? 15001 : 5001);
  }
  enviandoRespostaAuxiliar = false;
  Radio.Rx(0);
}

void OnTxTimeout(void) {
  // O driver também chama este callback pelo timer de software.
  timeoutTxPendente.store(1);
}

void OnRxTimeout(void) {}
void OnRxError(void) {}
void OnRxDone(uint8_t* payload, uint16_t size, int16_t rssi, int8_t snr) {
  lora_mesh_receber_pacote(payload, size, rssi, snr);
}

void lora_mesh_inicializar(SSD1306Wire* displayExterno) {
  display = displayExterno;
  pinMode(PIN_BOTAO_PGR, INPUT_PULLUP);

  // Inicializa o Slot do próprio nó
  memset(&baseDispositivos[0], 0, sizeof(SlotTelemetria));
  baseDispositivos[0].idOrigem = MEU_ID;
  baseDispositivos[0].medida = MedidaSensor::semLeitura();
  epocaOrigem = reservarEpocaOrigem();
  if (epocaOrigem == 0) Serial.println("[MESH] Falha na revisao de origem na NVS. Telemetria suspensa.");

  prefInterna.begin("config_params", true);
  modoCampoAtivo = prefInterna.getBool("mCampo", false);
  prefInterna.end();

  if (modoCampoAtivo) {
    displayAtivo = false;
    if (display != nullptr) display->displayOff();
  } else {
    displayAtivo = true;
    if (display != nullptr) display->displayOn();
  }

  static RadioEvents_t RadioEvents;
  RadioEvents.TxDone = OnTxDone;
  RadioEvents.TxTimeout = OnTxTimeout;
  RadioEvents.RxDone = OnRxDone;
  RadioEvents.RxTimeout = OnRxTimeout;
  RadioEvents.RxError = OnRxError;

  Radio.Init(&RadioEvents);
  Radio.SetChannel(rfFrequency);

  Radio.SetRxConfig(MODEM_LORA, 0, loraSF, 4, 0, 8, 0, false, 0, true, 0, 0, false, true);
  Radio.SetTxConfig(MODEM_LORA, 14, 0, 0, loraSF, 4, 8, false, true, 0, 0, false, 12000);
  Radio.Rx(0);

  inicioFaseRadioMs = millis();
  ultimoIncrementoUptime = millis();
  lastTxAttempt = millis();
  proximoIntervalo = TEMPO_CICLO_MS;
  cronometroProximoEnvioSeg = (uint32_t)random(2, 6);

  // 🔹 BO-4: removido o "if (MEU_ID == 0)" pelo mesmo motivo do OnTxDone acima —
  // dead code de uma arquitetura antiga onde o ID 0 era reservado ao Master.
  dispositivoOnline = false;
  baseDispositivos[0].hb = 0;
  estadoMeshAtual = MESH_AGUARDANDO;
  statusUltimoEnvio = epocaOrigem ? "AGUARDANDO" : "ERRO NVS";
}

// --- Processador de Pacotes Inteligente Modificado ---
void lora_mesh_receber_pacote(uint8_t* payload, uint16_t size, int16_t rssi, int8_t snr) {
  if (transmitindoDados || enviandoRespostaAuxiliar) return;
  if (payload == nullptr || size < sizeof(SlotTelemetria) ||
      size > sizeof(PacoteSeisSlots) || size % sizeof(SlotTelemetria) != 0) {
    Radio.Rx(0);
    return;
  }

  // --- TRATAMENTO DE MENSAGENS UNITÁRIAS (HANDSHAKES) ---
  if (size == sizeof(SlotTelemetria)) {
    SlotTelemetria singleSlot;
    memcpy(&singleSlot, payload, sizeof(SlotTelemetria));

    uint32_t tempoEstrategico = decodificarUint24(singleSlot.tempoDesdeUltimaMudanca);

    // --- COMPORTAMENTO COMO PAI: Detecta pedido de Socorro vindo de baixo ---
    if (singleSlot.valoresPinos[0] == ProtocoloMesh::Busca && singleSlot.idDestinatario == 0 &&
        ProtocoloMesh::idValido(singleSlot.idOrigem) && singleSlot.idOrigem != MEU_ID &&
        singleSlot.hb == 0 && singleSlot.medida == 0 && tempoEstrategico == 0 &&
        singleSlot.revisaoOrigem > 0 && singleSlot.revisaoOrigem <= UINT32_MAX) {
      Serial.printf("[MESH-RX] Pedido de SOCORRO recebido do No %d (RSSI: %d dBm)\n", singleSlot.idOrigem, rssi);
      
      // TRAVA 1 (ADOÇÃO): Se eu estou sem pai ou meu pai parou de responder, NÃO adoto ninguém!
      if (pontaComSono || meuPaiID == 65535 || !paiRespondendo) {
        if (!modoCampoAtivo) {
          Serial.println("[MESH-ADOÇÃO] Pedido ignorado. No indisponivel para repetir.");
        }
        Radio.Rx(0);
        return;
      }

      if (dispositivoOnline &&
          (nosRegistradosNaBase < MAX_NOS_BASE || indiceFilho(singleSlot.idOrigem) >= 0)) {
        Serial.printf("[MESH-TX] Respondendo SOCORRO para o No %d... (Estou Online, HB pai: %d)\n", singleSlot.idOrigem, baseDispositivos[0].hb);
        SlotTelemetria resposta;
        memset(&resposta, 0, sizeof(SlotTelemetria));
        resposta.idOrigem = MEU_ID;
        resposta.idDestinatario = singleSlot.idOrigem;
        ProtocoloMesh::marcar(resposta.valoresPinos, ProtocoloMesh::Oferta, meuPaiID);
        resposta.revisaoOrigem = singleSlot.revisaoOrigem;
        resposta.hb = baseDispositivos[0].hb;  

        ofertaAgendada.agendar(resposta, millis(), random(70, ProtocoloMesh::limiteSorteioOfertaMs(loraSF)));
      } else {
        Serial.printf("[MESH-RX] Ignorando socorro do No %d. Motivo: Online=%d, Vagas na Base=%d\n", 
                      singleSlot.idOrigem, dispositivoOnline, (MAX_NOS_BASE - nosRegistradosNaBase));
      }
      Radio.Rx(0);
      return;
    }

    // --- COMPORTAMENTO COMO FILHO BUSCADOR: Resposta de Socorro do Pai/Master ---
    if (estadoMeshAtual == MESH_BUSCANDO) {
      if (singleSlot.valoresPinos[0] == ProtocoloMesh::Oferta && singleSlot.idDestinatario == MEU_ID &&
          ProtocoloMesh::idValido(singleSlot.idOrigem) && singleSlot.idOrigem != MEU_ID &&
          ProtocoloMesh::sequencia(singleSlot.valoresPinos) != MEU_ID && singleSlot.hb < 254 &&
          numeroBusca != 0 && singleSlot.revisaoOrigem == numeroBusca &&
          millis() - inicioJanelaBuscaMs < ProtocoloMesh::janelaBuscaMs(loraSF) &&
          singleSlot.medida == 0 && tempoEstrategico == 0 && rssi >= rssiMinimo) {
        const int falhaCandidato = ordemFalha(singleSlot.idOrigem);
        const int falhaMelhor = ordemFalha(melhorPaiID);
        const bool priorizarRSSI = criterioConexao == "RSSI";
        const bool melhorMetrica = priorizarRSSI
          ? (rssi > melhorPaiRSSI || (rssi == melhorPaiRSSI && singleSlot.hb < melhorPaiHB))
          : (singleSlot.hb < melhorPaiHB || (singleSlot.hb == melhorPaiHB && rssi > melhorPaiRSSI));
        // Prioriza candidatos ainda não tentados. Se todos falharam, volta ao mais antigo.
        if (melhorPaiID == 65535 || falhaCandidato < falhaMelhor ||
            (falhaCandidato == falhaMelhor && melhorMetrica)) {
          melhorPaiID = singleSlot.idOrigem;
          melhorPaiHB = singleSlot.hb;
          melhorPaiRSSI = rssi;
          Serial.printf("[MESH-BUSCA] Candidato %u: %u saltos, RSSI %d dBm.\n",
                        melhorPaiID, melhorPaiHB, melhorPaiRSSI);
        }
      }
      Radio.Rx(0);
      return;
    }

    if (singleSlot.valoresPinos[0] == ProtocoloMesh::Ack) {
      if (aguardandoAck && slotsEmVoo > 0 && dispositivoOnline &&
          singleSlot.idDestinatario == MEU_ID && singleSlot.idOrigem == meuPaiID &&
          singleSlot.medida == 0 && tempoEstrategico == 0 && singleSlot.revisaoOrigem == 0 &&
          ProtocoloMesh::sequencia(singleSlot.valoresPinos) ==
            ProtocoloMesh::sequencia(blocoEmVoo.slots[0].valoresPinos)) {
        if (singleSlot.hb + 1 != baseDispositivos[0].hb) {
          perderPai();
          Radio.Rx(0);
          return;
        }
        if (!paiRespondendo) {
          parentID = String(meuPaiID);
          totalPaisPerdidos = 0;
          Serial.printf("[MESH] Rota confirmada via %u (%u saltos, RSSI %d dBm).\n",
                        meuPaiID, baseDispositivos[0].hb, rssi);
        }
        paiRespondendo = true;
        ultimoContatoPaiMs = millis();
        Serial.printf("[MESH-ACK] Bloco %lu confirmado pelo pai %u.\n",
                      (unsigned long)ProtocoloMesh::sequencia(singleSlot.valoresPinos), meuPaiID);
        confirmarBloco();
      }
      Radio.Rx(0);
      return;
    }
  }

  const int quantidade = size / sizeof(SlotTelemetria);
  const auto* slots = (const SlotTelemetria*)payload;
  const uint16_t remetente = slots[0].idOrigem;
  const uint32_t numero = ProtocoloMesh::sequencia(slots[0].valoresPinos);
  if (slots[0].valoresPinos[0] != ProtocoloMesh::Dados || numero == 0 ||
      slots[0].idDestinatario != MEU_ID || !ProtocoloMesh::idValido(remetente) ||
      remetente == MEU_ID || remetente == meuPaiID ||
      pontaComSono || !dispositivoOnline || meuPaiID == 65535 || !paiRespondendo ||
      slots[0].hb != baseDispositivos[0].hb + 1) {
    Radio.Rx(0);
    return;
  }

  if (!cacheRecepcao.repetido(remetente, numero)) {
    if (!ProtocoloMesh::aceitarBloco(slots, quantidade, MEU_ID,
          MAX_NOS_BASE - nosRegistradosNaBase,
          [](uint16_t id) { return indiceFilho(id) >= 0; }, true)) {
      Radio.Rx(0);
      return;
    }
    for (int s = 0; s < quantidade; ++s) {
      // Após trocar de rota, um filho pode devolver uma cópia da leitura
      // que este nó já fornece no próprio slot.
      if (slots[s].idOrigem == MEU_ID) continue;
      int i = indiceFilho(slots[s].idOrigem);
      if (i >= 0 && slots[s].revisaoOrigem <= baseDispositivos[i].revisaoOrigem) continue;
      if (i < 0) i = nosRegistradosNaBase++;
      baseDispositivos[i] = slots[s];
      instanteRecebimentoFilhoMs[i] = timestampUltimoContatoMs[i] = millis();
      if (++ultimaVersaoFilho == 0) ++ultimaVersaoFilho;
      versoesFilhos[i] = ultimaVersaoFilho;
    }
    cacheRecepcao.registrar(remetente, numero);
  }

  SlotTelemetria ack = {};
  ack.idOrigem = MEU_ID;
  ack.idDestinatario = remetente;
  ack.hb = baseDispositivos[0].hb;
  ProtocoloMesh::marcar(ack.valoresPinos, ProtocoloMesh::Ack, numero);
  delay(random(10, 60));
  enviandoRespostaAuxiliar = true;
  Radio.Send((uint8_t*)&ack, sizeof(ack));
}

static void concluirBusca() {
  if (melhorPaiID == 65535) {
    estadoMeshAtual = MESH_AGUARDANDO;
    statusUltimoEnvio = "AGUARDANDO";
    cronometroProximoEnvioSeg = (uint32_t)random(4, 10);
    return;
  }
  meuPaiID = melhorPaiID;
  baseDispositivos[0].hb = melhorPaiHB + 1;
  dispositivoOnline = true;
  paiRespondendo = false; // Só anuncia a rota depois do primeiro ACK.
  estadoMeshAtual = MESH_TRANSMITINDO;
  statusUltimoEnvio = "ENVIANDO";
  transmissaoEmAndamento = true;
  indiceBaseEnvioAtual = 0;
  ultimoEnvioPacoteMs = millis();
  esperaBlocoMs = 0;
}

static void processarOfertaSlave() {
  if (!paiRespondendo || transmitindoDados || enviandoRespostaAuxiliar || aguardandoAck ||
      !ofertaAgendada.pronta(millis(), loraSF)) return;
  SlotTelemetria resposta = ofertaAgendada.pacote;
  ofertaAgendada.cancelar();
  enviandoRespostaAuxiliar = true;
  Radio.Send((uint8_t*)&resposta, sizeof(resposta));
}

bool lora_mesh_radio_dormindo() {
  return radioDormindo;
}

static void processarSonoRadio() {
  const uint32_t agora = millis();
  if (radioDormindo) {
    const uint32_t tempoDormindo = agora - inicioFaseRadioMs;
    if (tempoDormindo < uint32_t(tempoSleepMin) * 60000UL) return;
    ultimoContatoPaiMs += tempoDormindo; // O prazo do pai só corre com o rádio acordado.
    Radio.Rx(0);
    radioDormindo = false;
    inicioFaseRadioMs = agora;
    cronometroProximoEnvioSeg = 0;
    statusUltimoEnvio = "RECEBENDO";
    return;
  }

  const bool cicloConfirmado = cicloConfirmadoAgora;
  cicloConfirmadoAgora = false;
  if (!pontaComSono || !cicloConfirmado || !paiRespondendo ||
      nosRegistradosNaBase != 1 || estadoMeshAtual != MESH_RECEBENDO ||
      enviandoRespostaAuxiliar || tempoAtivoMin == 0 || tempoSleepMin == 0 ||
      agora - inicioFaseRadioMs < uint32_t(tempoAtivoMin) * 60000UL) return;

  Radio.Sleep();
  radioDormindo = true;
  inicioFaseRadioMs = agora;
  statusUltimoEnvio = "LORA DORMINDO";
}

void lora_mesh_processar() {
  // 🔹 CORREÇÃO PARTE 1: Sincroniza dinamicamente a tabela do rádio com o ID atualizado via AP
  baseDispositivos[0].idOrigem = MEU_ID; 

  tratarBotaoPGR();
  extern bool modoConfiguracaoAtivo;
  if (modoConfiguracaoAtivo) return;
  gerenciarTimeoutDisplay();
  processarSonoRadio();

  extern uint32_t timeoutPaiMs;
  if (!radioDormindo && dispositivoOnline && !transmissaoEmAndamento && !enviandoRespostaAuxiliar &&
      millis() - ultimoContatoPaiMs >= timeoutPaiMs) {
    perderPai();
  }

  if (estadoMeshAtual == MESH_BUSCANDO && !enviandoRespostaAuxiliar &&
      millis() - inicioJanelaBuscaMs >= ProtocoloMesh::janelaBuscaMs(loraSF)) {
    concluirBusca();
  }

  if (!radioDormindo) {
    processarEnvioPendente();
    processarOfertaSlave();
  }

  if (millis() - ultimoIncrementoUptime >= 1000) {
    // RESET TIMEOUT TRIPLO CLIQUE: Se passou mais de 1 segundo sem atingir 3 cliques, limpa o contador
    if (contadorCliquesPGR > 0 && (millis() - janelaCliquesMs >= 1000)) {
      // Se acumulou apenas 1 clique curto antes de estourar o tempo de 1s, executa a mudança para a Tela 1 (Home)
      if (contadorCliquesPGR == 1 && telaAtual == TELA_SISTEMA && !menuConfiguracaoAtivo) {
        telaAtual = TELA_HOME;
      }
      contadorCliquesPGR = 0;
    }

    static uint8_t segundosAcumulados = 0;
    segundosAcumulados++;

    if (segundosAcumulados >= 60) {
      uptimeMinutos++;
      segundosAcumulados = 0;

      uint32_t limiteMs = CONFIG_TTL_LIMPEZA_S * 1000UL;
      for (int i = 1; i < nosRegistradosNaBase; i++) {
        if (!transmissaoEmAndamento && versoesFilhos[i] == 0 &&
            millis() - timestampUltimoContatoMs[i] >= limiteMs) {
          for (int j = i; j < nosRegistradosNaBase - 1; j++) {
            baseDispositivos[j] = baseDispositivos[j + 1];
            versoesFilhos[j] = versoesFilhos[j + 1];
            timestampUltimoContatoMs[j] = timestampUltimoContatoMs[j + 1];
            instanteRecebimentoFilhoMs[j] = instanteRecebimentoFilhoMs[j + 1];
          }
          nosRegistradosNaBase--;
          i--;
        }
      }
    }

    ultimoIncrementoUptime = millis();

    if (!radioDormindo && (estadoMeshAtual == MESH_AGUARDANDO || estadoMeshAtual == MESH_RECEBENDO) && cronometroProximoEnvioSeg > 0) {
      cronometroProximoEnvioSeg--;
      
      // ⏱️ Correção dos 2 segundos: volta para RECEBENDO após o tempo do SUCESSO expirar
      if (statusUltimoEnvio == "SUCESSO" && (millis() - timestampSucessoMs >= 2000)) {
        statusUltimoEnvio = "RECEBENDO";
      }

    } else if (!radioDormindo && (estadoMeshAtual == MESH_AGUARDANDO || estadoMeshAtual == MESH_RECEBENDO) &&
               cronometroProximoEnvioSeg == 0 && !enviandoRespostaAuxiliar && epocaOrigem != 0) {
      cronometroProximoEnvioSeg = TEMPO_CICLO_MS / 1000UL;

      // 🔹 BO-4: removido o "if (MEU_ID != 0) {...} else { lastTxAttempt = millis(); }" —
      // o else nunca executava (MEU_ID nunca é 0 neste módulo, ver nota no topo do arquivo).
      if (meuPaiID != 65535) {

        lastTxAttempt = millis(); 

        // 🕒 FIM DO CICLO: Transiciona para TRANSMITINDO e dispara os dados
        estadoMeshAtual = MESH_TRANSMITINDO;
        statusUltimoEnvio = "TRANSMITINDO"; 
        transmissaoEmAndamento = true;
        indiceBaseEnvioAtual = 0;
        ultimoEnvioPacoteMs = millis();
        esperaBlocoMs = 0;

      } else {
        estadoMeshAtual = MESH_BUSCANDO;
        statusUltimoEnvio = "BUSCANDO";
        inicioJanelaBuscaMs = millis();

        melhorPaiID = 65535;
        melhorPaiHB = 255;
        melhorPaiRSSI = -150;

        // 🔹 CORREÇÃO PARTE 2: Garante a consistência interna antes de estruturar o pacote de Socorro
        baseDispositivos[0].idOrigem = MEU_ID;

        // Monta Grito de Socorro com novo formato limpo de bytes
        SlotTelemetria gritoSocorro;
        memset(&gritoSocorro, 0, sizeof(SlotTelemetria));
        gritoSocorro.idOrigem = MEU_ID; // Agora sim vai com o ID dinâmico correto!
        gritoSocorro.idDestinatario = 0;
        gritoSocorro.hb = 0;

        numeroBusca = esp_random();
        if (numeroBusca == 0) numeroBusca = 1;
        gritoSocorro.revisaoOrigem = numeroBusca;
        ProtocoloMesh::marcar(gritoSocorro.valoresPinos, ProtocoloMesh::Busca);
        enviandoRespostaAuxiliar = true;
        Radio.Send((uint8_t*)&gritoSocorro, sizeof(SlotTelemetria));
      }
    }
  }

  if (displayAtivo && display != nullptr) {
    display->clear();
    switch (telaAtual) {
      case TELA_HOME: renderizarHome(); break;
      case TELA_TABELA: renderizarTabela(); break;
      case TELA_SISTEMA: renderizarSistema(); break;
    }
    display->display();
  }
}

static void dispararProximoBlocoBase() {
  if (slotsEmVoo == 0) {
    if (amostraOrigem == UINT32_MAX) {
      epocaOrigem = reservarEpocaOrigem();
      amostraOrigem = 0;
      if (epocaOrigem == 0) {
        perderPai();
        statusUltimoEnvio = "ERRO NVS";
        return;
      }
    }
    baseDispositivos[0].revisaoOrigem = (uint64_t(epocaOrigem) << 32) | ++amostraOrigem;
    // Leitura mais recente do DHT11 e há quanto tempo ela foi feita.
    baseDispositivos[0].medida = sensor_medida();
    uint32_t idade = sensor_idade_s();
    if (idade > 0xFFFFFF) idade = 0xFFFFFF;
    codificarUint24(idade, baseDispositivos[0].tempoDesdeUltimaMudanca);
    baseDispositivos[0].idDestinatario = meuPaiID;
    blocoEmVoo.slots[0] = baseDispositivos[0];
    uint32_t numero = esp_random();
    ProtocoloMesh::marcar(blocoEmVoo.slots[0].valoresPinos, ProtocoloMesh::Dados, numero ? numero : 1);
    tentativasBloco = 0;
    slotsEmVoo = 1;
    if (indiceBaseEnvioAtual == 0) indiceBaseEnvioAtual = 1;

    while (indiceBaseEnvioAtual < nosRegistradosNaBase && slotsEmVoo < 6) {
      const int i = indiceBaseEnvioAtual++;
      if (versoesFilhos[i] == 0) continue;
      SlotTelemetria slot = baseDispositivos[i];
      uint32_t idadeFilho = decodificarUint24(slot.tempoDesdeUltimaMudanca);
      uint32_t atraso = (millis() - instanteRecebimentoFilhoMs[i]) / 1000UL;
      codificarUint24(idadeFilho + atraso, slot.tempoDesdeUltimaMudanca);
      slot.idDestinatario = meuPaiID;
      memset(slot.valoresPinos, 0, sizeof(slot.valoresPinos));
      versoesEmVoo[slotsEmVoo] = versoesFilhos[i];
      blocoEmVoo.slots[slotsEmVoo++] = slot;
    }
  }

  ++tentativasBloco;
  aguardandoAck = false;
  transmitindoDados = true;
  Serial.printf("[MESH-DATA-TX] Bloco %lu para %u, %d slots, tentativa %u.\n",
                (unsigned long)ProtocoloMesh::sequencia(blocoEmVoo.slots[0].valoresPinos),
                meuPaiID, slotsEmVoo, tentativasBloco);
  Radio.Send((uint8_t*)&blocoEmVoo, (uint16_t)(slotsEmVoo * sizeof(SlotTelemetria)));
}

static void processarEnvioPendente() {
  // Mantém alterações de estado e comandos do rádio no loop principal.
  if (timeoutTxPendente.exchange(0)) OnTxDone();
  if (estadoMeshAtual != MESH_TRANSMITINDO || !transmissaoEmAndamento ||
      transmitindoDados || enviandoRespostaAuxiliar ||
      millis() - ultimoEnvioPacoteMs < esperaBlocoMs) return;

  if (aguardandoAck && tentativasBloco >= MAX_TENTATIVAS_BLOCO) {
    perderPai();
    return;
  }
  dispararProximoBlocoBase();
}

static void renderizarHome() {
  display->setFont(ArialMT_Plain_10);
  display->drawString(0, 0, "ID: " + String(MEU_ID));
  display->drawString(128 - display->getStringWidth(statusUltimoEnvio), 0, statusUltimoEnvio);
  display->drawHorizontalLine(0, 11, 128);

  display->setFont(ArialMT_Plain_16);
  if (sensor_tem_leitura()) {
    display->drawString(0, 12, String(sensor_temperatura(), 1) + "C  " + String(sensor_umidade(), 0) + "%");
  } else {
    display->drawString(0, 12, "SEM LEITURA");
  }

  display->setFont(ArialMT_Plain_10);
  display->drawString(0, 28, "STATUS: " + String(!dispositivoOnline || meuPaiID == 65535 ? "OFFLINE" : (paiRespondendo ? "ONLINE" : "ISOLADO")));

  display->drawString(0, 38, "CICLO:");
  unsigned long tempoPassadoCiclo = millis() - lastTxAttempt;
  int pctCiclo = (tempoPassadoCiclo * 100) / proximoIntervalo;
  display->drawProgressBar(35, 41, 90, 6, constrain(pctCiclo, 0, 100));

  if (!sensor_tem_leitura()) {
    display->drawString(0, 51, sensor_ultima_falhou() ? "SENSOR: SEM RESPOSTA" : "SENSOR: INICIANDO");
  } else {
    display->drawString(0, 51, String("LEITURA: ha ") + String(sensor_idade_s()) + "s" +
                                   (sensor_ultima_falhou() ? " (FALHA)" : ""));
  }
}

static void renderizarTabela() {
  display->setFont(ArialMT_Plain_10);
  
  // Cabeçalho com espaçamento horizontal otimizado para os 128 pixels da tela
  display->drawString(0, 0, "ID");
  display->drawString(26, 0, "DST");
  display->drawString(56, 0, "T/U");
  display->drawString(94, 0, "TMP");
  display->drawString(118, 0, "P");
  
  // Linha separadora cobrindo a largura inteira do display (0 a 128)
  display->drawHorizontalLine(0, 11, 128);

  int yOffset = 15; // Início da primeira linha de dados com margem confortável
  int nosExibidos = 0;
  
  if (indiceScrollTabela >= nosRegistradosNaBase) {
    indiceScrollTabela = 0;
  }

  // Ignora o índice 0 (nó local) se o scroll estiver no topo
  int i = (indiceScrollTabela == 0) ? 1 : indiceScrollTabela;

  // Loop limitado a exibir no máximo 3 nós por tela para não espremer
  for (; i < nosRegistradosNaBase && nosExibidos < 3; i++) {
    SlotTelemetria slot = baseDispositivos[i];
    
    char colID[6], colDST[6], colVAL[12], colTMP[8], colP[4];
    
    // 1. ID Origem
    sprintf(colID, "%03u", slot.idOrigem);
    
    // 2. ID Destino
    sprintf(colDST, "%03u", slot.idDestinatario == 65535 ? 0 : slot.idDestinatario);
    
    // 3. Temperatura/umidade inteiras (em branco se o nó ainda não tem leitura)
    const bool comLeitura = MedidaSensor::valida(slot.medida);
    if (comLeitura) {
      snprintf(colVAL, sizeof(colVAL), "%d/%d", (int)lroundf(MedidaSensor::temperatura(slot.medida)),
               (int)lroundf(MedidaSensor::umidade(slot.medida)));
    } else {
      strcpy(colVAL, " ");
    }
    
    // 4. TEMPO (Latência ou Uptime parcial)
    uint32_t latenciaOriginal = decodificarUint24(slot.tempoDesdeUltimaMudanca);
    if (comLeitura) {
      if (latenciaOriginal < 60) sprintf(colTMP, "%us", latenciaOriginal);
      else sprintf(colTMP, "%um", latenciaOriginal / 60);
    } else {
      uint32_t minutosRetido = (millis() - instanteRecebimentoFilhoMs[i]) / 60000UL;
      sprintf(colTMP, "%um", minutosRetido);
    }
    
    // 5. P (Pendência de Envio)
    strcpy(colP, versoesFilhos[i] == 0 ? "V" : "P");

    // Desenha as colunas alinhadas com as novas coordenadas de largura expandida
    display->drawString(0, yOffset, String(colID));
    display->drawString(26, yOffset, String(colDST));
    display->drawString(56, yOffset, String(colVAL));
    display->drawString(94, yOffset, String(colTMP));
    display->drawString(118, yOffset, String(colP));

    yOffset += 15; // Salto vertical de 15 pixels garante leitura limpa sem espremer
    nosExibidos++;
  }
}

static void renderizarSistema() {
  display->setFont(ArialMT_Plain_10);

  // --- SEGUNDA CAMADA: RENDERIZAÇÃO DO MENU DE CONFIGURAÇÃO ---
  if (menuConfiguracaoAtivo) {
    display->drawString(0, 0, "MENU CONFIGURAÇÃO");
    display->drawHorizontalLine(0, 11, 128);

    // Opção 0: Alterar Modo
    if (linhaSelecionadaMenu == 0) {
      display->drawString(0, 16, ">1. Alterar Modo Operação");
    } else {
      display->drawString(0, 16, "  1. Alterar Modo Operação");
    }

    // Opção 1: Abrir Ponto de Acesso
    if (linhaSelecionadaMenu == 1) {
      display->drawString(0, 28, ">2. Iniciar Ponto de Acesso");
    } else {
      display->drawString(0, 28, "  2. Iniciar Ponto de Acesso");
    }

    // Opção 2: Voltar
    if (linhaSelecionadaMenu == 2) {
      display->drawString(0, 40, ">3. Voltar");
    } else {
      display->drawString(0, 40, "  3. Voltar");
    }
    return; // Interrompe para não misturar com a tela de diagnóstico
  }

  // --- PRIMEIRA CAMADA: TELA DE DIAGNÓSTICO DO SISTEMA (PADRÃO) ---
  // Cabeçalho Fixo
  display->drawString(0, 0, "SISTEMA - " VERSAO_SLAVE);
  display->drawHorizontalLine(0, 11, 128);

  // Rodapé Informativo Fixo
  display->drawHorizontalLine(0, 51, 128);
  display->drawString(0, 54, "Menu: Pressione 3x PGR");

  // Janela Deslizante: Mostra apenas 3 informações por vez baseadas em 'indiceScrollSistema'
  int yOffset = 14;
  int itensExibidos = 0;

  for (int i = indiceScrollSistema; i < MAX_ITENS_SISTEMA && itensExibidos < 3; i++) {
    char bufferLinha[45];
    switch (i) {
      case 0:
        sprintf(bufferLinha, "Proximo Envio: %u s", cronometroProximoEnvioSeg);
        break;
      case 1:
        sprintf(bufferLinha, "Base: %d / 100", (nosRegistradosNaBase - 1));
        break;
      case 2:
        snprintf(bufferLinha, sizeof(bufferLinha), "Sensor: DHT11 (pino %d)", DHT_PIN);
        break;
      case 3:
        sprintf(bufferLinha, "Uptime: %u m", max((uint32_t)1, uptimeMinutos));
        break;
      case 4:
        snprintf(bufferLinha, sizeof(bufferLinha), "Papel: %s", pontaComSono ? "Ponta" : "Repetidor");
        break;
      case 5:
        snprintf(bufferLinha, sizeof(bufferLinha), "LoRa: %s", radioDormindo ? "Dormindo" : "Acordado");
        break;
      case 6:
        sprintf(bufferLinha, "Modo: %s", modoCampoAtivo ? "Campo" : "Bancada");
        break;
    }
    display->drawString(0, yOffset, String(bufferLinha));
    yOffset += 12; // Avanço de linha vertical proporcional
    itensExibidos++;
  }
}

static void gerenciarTimeoutDisplay() {
  if (!modoCampoAtivo) return;
  if (displayAtivo && (millis() - displayTimeout >= JANELA_2_MIN)) {
    displayAtivo = false;
    emModoEdicao = false;
    if (display != nullptr) {
      display->clear();
      display->display();
      display->displayOff();
    }
  }
}

static void tratarBotaoPGR() {
  static bool botaoPressionadoAnterior = false;
  static unsigned long tempoPressionado = 0;
  static bool acaoLongoExecutada = false; // Garante que a ação ocorre apenas uma vez por retenção
  
  bool botaoEstadoAtual = (digitalRead(PIN_BOTAO_PGR) == LOW);

  // --- LÓGICA DE DETECÇÃO INSTANTÂNEA EM TEMPO DE RETENÇÃO (BOTAO ESTÁ SENDO PRESSIONADO) ---
  if (botaoEstadoAtual && botaoPressionadoAnterior) {
    unsigned long tempoAtualRetencao = millis() - tempoPressionado;

    // Se estiver na Tela de Sistema e com o Menu Ativo, dispara a seleção NO INSTANTE que bate 2 segundos
    if (telaAtual == TELA_SISTEMA && menuConfiguracaoAtivo && !acaoLongoExecutada && tempoAtualRetencao >= 2000) {
      acaoLongoExecutada = true; // Bloqueia repetições até soltar o botão
      displayTimeout = millis();

      if (linhaSelecionadaMenu == 0) { // Alterar Modo
        modoCampoAtivo = !modoCampoAtivo;
        prefInterna.begin("config_params", false);
        prefInterna.putBool("mCampo", modoCampoAtivo);
        prefInterna.end();
        
        menuConfiguracaoAtivo = false; // Retorna para tela de Diagnóstico
        linhaSelecionadaMenu = 0;
        
        if (modoCampoAtivo) {
          displayAtivo = false;
          if (display != nullptr) {
            display->clear(); display->display(); display->displayOff();
          }
        }
      } 
      else if (linhaSelecionadaMenu == 1) { // Abrir Ponto de Acesso
        extern bool modoConfiguracaoAtivo;
        modoConfiguracaoAtivo = true;
        if (display != nullptr) {
          display->clear();
          display->setFont(ArialMT_Plain_10);
          display->drawString(0, 0, "PORTAL CONFIGURAÇÃO");
          display->drawHorizontalLine(0, 11, 128);
          display->drawString(0, 16, "REDE: SLAVE_CONFIG");
          display->drawString(0, 30, "IP: 192.168.4.1");
          display->drawString(0, 44, "SITE: menu.config");
          display->display();
        }
        extern void interface_iniciarModoConfiguracao();
        interface_iniciarModoConfiguracao();
        return; // Transfere o controle travando o fluxo no WebServer
      } 
      else if (linhaSelecionadaMenu == 2) { // Voltar
        menuConfiguracaoAtivo = false;
        linhaSelecionadaMenu = 0;
      }
    }
  }

  // --- CAPTURA DA BORDA DE DESCIDA (BOTOES DETECTADOS NO INÍCIO DO PRESSIONAMENTO) ---
  if (botaoEstadoAtual && !botaoPressionadoAnterior) {
    tempoPressionado = millis();
    botaoPressionadoAnterior = true;
    acaoLongoExecutada = false; // Reseta a trava para o novo ciclo de clique
  } 
  // --- CAPTURA DA BORDA DE SUBIDA (BOTÃO FOI SOLTO) ---
  else if (!botaoEstadoAtual && botaoPressionadoAnterior) {
    unsigned long duracaoClique = millis() - tempoPressionado;
    botaoPressionadoAnterior = false;

    // Se o display estava desligado pelo Modo Campo, apenas acende e ignora outras ações
    if (!displayAtivo && modoCampoAtivo) {
      displayAtivo = true;
      displayTimeout = millis();
      if (display != nullptr) display->displayOn();
      return;
    }
    displayTimeout = millis();

    // Se a ação de clique longo já foi disparada enquanto o botão estava afundado, ignora o evento de soltar
    if (acaoLongoExecutada) {
      acaoLongoExecutada = false;
      return;
    }

    // --- FILTRO DE DEBOUNCE PARA CLIQUES CURTOS DE NAVEGAÇÃO ---
    if (duracaoClique >= 50 && duracaoClique < 2000) {
      if (telaAtual == TELA_SISTEMA) {
        if (menuConfiguracaoAtivo) {
          // No menu, clique curto avança a seta de cima para baixo
          linhaSelecionadaMenu++;
          if (linhaSelecionadaMenu >= MAX_LINHAS_MENU) {
            linhaSelecionadaMenu = 0;
          }
        } else {
          // Inicializa ou acumula cliques na janela de triplo clique rápido (1 segundo)
          if (contadorCliquesPGR == 0) {
            janelaCliquesMs = millis();
          }
          contadorCliquesPGR++;

          // Se alcançou 3 ou mais cliques dentro do limite de 1 segundo
          if (contadorCliquesPGR >= 3) {
            if (millis() - janelaCliquesMs <= 1000) {
              menuConfiguracaoAtivo = true; // Abre o menu de configuração
              linhaSelecionadaMenu = 0;
            }
            contadorCliquesPGR = 0;
          }
        }
      } else {
        // Navegação direta e limpa entre as telas Home e Tabela
        if (telaAtual == TELA_HOME) {
          telaAtual = TELA_TABELA;
          indiceScrollTabela = 0;
        } else if (telaAtual == TELA_TABELA) {
          telaAtual = TELA_SISTEMA;
          indiceScrollSistema = 0;
          menuConfiguracaoAtivo = false;
          contadorCliquesPGR = 0;
        }
      }
    }
  }

  // --- LÓGICA DE SCROLL POR RETENÇÃO CONTÍNUA (BOTAO CONTINUA PRESSIONADO) ---
  if (botaoEstadoAtual && !menuConfiguracaoAtivo && !acaoLongoExecutada) {
    if (millis() - tempoPressionado >= 1000) { // Entra em modo de scroll contínuo após 1s de retenção
      if (millis() - ultimoScrollMs >= 1000) {
        ultimoScrollMs = millis();

        if (telaAtual == TELA_TABELA) {
          indiceScrollTabela++;
          if (indiceScrollTabela >= nosRegistradosNaBase || nosRegistradosNaBase <= 1) {
            indiceScrollTabela = 0;
          }
        } 
        else if (telaAtual == TELA_SISTEMA) {
          indiceScrollSistema++;
          if (indiceScrollSistema > (MAX_ITENS_SISTEMA - 3)) {
            indiceScrollSistema = 0;
          }
        }
      }
    }
  }
}
