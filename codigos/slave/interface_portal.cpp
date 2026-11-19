#include "interface_portal.h"
#include "sensor_dht.h"
#include "config_radio.h"
#include "perfil_sono.h"
#include "portal_seguro.h"

// --- Instâncias de Servidores do Modo Configuração ---
WebServer server(80);
const byte DNS_PORT = 53;
DNSServer dnsServer;

// --- Variáveis Locais ---
const char* URL_AMIGAVEL = "menu.config"; // Link limpo para o DNS
bool modoConfiguracaoAtivo = false;       // Inicializa como falso

Preferences preferences;
static String tokenPortal;

// --- Funções Auxiliares Internas ---

// Gera o formulário HTML que o técnico vai ver no celular
void enviarPaginaHTML() {
    const String host = server.hostHeader();
    if (host != URL_AMIGAVEL && host != "192.168.4.1") {
        server.sendHeader("Location", "http://192.168.4.1/", true);
        server.send(302, "text/plain", "");
        return;
    }
    if (!modoCampoAtivo) {
        Serial.println("[INTERFACE] Requisição recebida: Enviando página de configuração HTML.");
    }

    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<title>Painel de Controle</title>";
    html += "<style>";
    html += "body{font-family:'Segoe UI',Roboto,Helvetica,Arial,sans-serif; margin:0; padding:0; background:#f0f2f5; color:#333;} ";
    html += ".header{background:#1e293b; color:#fff; padding:20px; text-align:center; box-shadow:0 2px 4px rgba(0,0,0,0.1);} ";
    html += ".header h2{margin:0; font-size:22px; font-weight:600;} ";
    html += ".container{max-width:550px; margin:20px auto; padding:0 15px; box-sizing:border-box;} ";
    html += ".card{background:#fff; padding:20px; border-radius:10px; box-shadow:0 4px 6px rgba(0,0,0,0.05); margin-bottom:20px;} ";
    html += ".status-box{background:#e2e8f0; padding:15px; border-radius:8px; margin-bottom:20px; border-left:6px solid #3b82f6; font-size:14px; line-height:1.6;} ";
    html += "h3{color:#1e293b; border-bottom:2px solid #e2e8f0; padding-bottom:8px; margin-top:0; margin-bottom:15px; font-size:16px; text-transform:uppercase; letter-spacing:0.5px;} ";
    html += "h4{color:#475569; margin:10px 0 5px 0; font-size:14px;} ";
    html += "label{display:block; margin-top:12px; font-weight:600; font-size:13px; color:#475569;} ";
    html += "input,select{width:100%; padding:10px; margin-top:6px; border:1px solid #cbd5e1; border-radius:6px; box-sizing:border-box; font-size:14px; transition:border 0.2s;} ";
    html += "input:focus,select:focus{border-color:#3b82f6; outline:none; box-shadow:0 0 0 3px rgba(59,130,246,0.15);} ";
    html += "input[readonly]{background:#f1f5f9; color:#94a3b8; cursor:not-allowed; border-color:#e2e8f0;} ";
    html += "input[type='checkbox']{width:auto; transform:scale(1.2); margin-right:10px; vertical-align:middle;} ";
    html += ".checkbox-container{background:#f8fafc; padding:12px; border-radius:6px; border:1px solid #e2e8f0; margin-bottom:15px;} ";
    html += ".sub-secao{background:#f8fafc; padding:15px; border-radius:8px; margin-top:15px; border:1px solid #e2e8f0;} ";
    html += "input[type='submit']{background:#3b82f6; color:#fff; border:none; margin-top:25px; padding:12px; border-radius:6px; cursor:pointer; font-size:16px; font-weight:600; width:100%; transition:background 0.2s;} ";
    html += "input[type='submit']:hover{background:#2563eb;} ";
    html += "</style></head><body>";
    
    html += "<div class='header'><h2>MENU CONFIGURAÇÃO</h2></div>";
    html += "<div class='container'>";
    
    // 🔹 ITEM 3: Bloco de Status Informativo contendo o ID do Pai Dinâmico/Automático
    html += "<div class='status-box'>";
    html += "<strong>Última rota confirmada (ID do pai):</strong> " + (parentID.length() > 0 ? parentID : "Sem rota confirmada") + "<br>";
    html += "</div>";

    html += "<form action='/salvar' method='POST'>";
    html += "<input type='hidden' name='token' value='" + tokenPortal + "'>";
    
    // Parâmetros do Sensor e Identificação
    html += "<div class='card'>";
    html += "<h3>1. Identificação e Sensor</h3>";
    // 🔹 ITEM 3: Campo My ID movido para cá e tornado editável
    html += "<label>ID do Dispositivo (My ID):</label><input type='number' name='myID' value='" + String(MEU_ID) + "' required>";
    html += "<label>Sensor:</label><p>DHT11 (temperatura e umidade) no pino " + String(DHT_PIN) + ".</p>";
    if (sensor_tem_leitura()) {
        html += "<p>Última leitura: <strong>" + String(sensor_temperatura(), 1) + " °C</strong> e <strong>" +
                String(sensor_umidade(), 0) + " %</strong>, há " + String(sensor_idade_s()) + " s.</p>";
    } else {
        html += "<p>Ainda sem leitura válida do sensor.</p>";
    }
    html += "</div>";

    // Conectividade LoRa Mesh
    html += "<div class='card'>";
    html += "<h3>2. Conectividade LoRa Mesh</h3>";
    html += "<label>Spreading Factor (SF):</label><input type='number' min='7' max='12' name='loraSF' value='" + String(loraSF) + "'>";
    html += "<label>Frequência (Hz):</label><input type='number' min='902000000' max='928000000' name='rfFrequency' value='" + String(rfFrequency) + "'>";
    html += "<p>Use os mesmos SF e frequência em todos os dispositivos. Padrão de bancada: SF7 / 915 MHz.</p>";
    html += "<label>Critério de Conexão:</label>";
    html += "<select name='criterioConexao'>";
    html += "<option value='RSSI'" + String(criterioConexao == "RSSI" ? " selected" : "") + ">Melhor RSSI</option>";
    html += "<option value='Saltos'" + String(criterioConexao == "Saltos" ? " selected" : "") + ">Menor Custo de Saltos</option>";
    html += "</select>";
    html += "<label>RSSI Mínimo de Conexão (dBm):</label><input type='number' name='rssiMinimo' value='" + String(rssiMinimo) + "'>";
    html += "<label>Timeout de Desconexão do Pai (ms):</label><input type='number' name='timeoutPaiMs' value='" + String(timeoutPaiMs) + "'>";
    html += "</div>";

    // Gerenciamento de Energia
    html += "<div class='card'>";
    html += "<h3>3. Gerenciamento de Energia</h3>";
    
    html += "<div class='checkbox-container'>";
    html += "<label style='margin-top:0; color:#1e293b;'><input type='checkbox' name='modoCampoAtivo' value='true'" + String(modoCampoAtivo ? " checked" : "") + "> <strong>Ativar Modo Campo</strong></label>";
    html += "<p style='margin:5px 0 0 24px; font-size:12px; color:#64748b;'>Se desmarcado, o dispositivo operará no perfil estável de Bancada.</p>";
    html += "</div>";
    
    html += "<label>Papel deste Slave na rede LoRa</label><select name='papelLoRa'>";
    html += "<option value='repetidor'" + String(pontaComSono ? "" : " selected") + ">Repetidor — rádio sempre ligado</option>";
    html += "<option value='ponta'" + String(pontaComSono ? " selected" : "") + ">Nó de ponta — rádio pode dormir</option></select>";
    html += "<p>Use Repetidor nos nós que encaminham leituras. O nó de ponta só envia a própria leitura. O sensor continua ativo durante o sono do rádio.</p>";
    html += "<label>Tempo mínimo de rádio ligado (min)</label><input type='number' min='1' max='1440' name='tempoAtivoMin' value='" + String(tempoAtivoMin) + "'>";
    html += "<label>Tempo de sono do rádio (min)</label><input type='number' min='1' max='1440' name='tempoSleepMin' value='" + String(tempoSleepMin) + "'>";
    html += "</div>";

    html += "<input type='submit' value='Salvar Configurações'>";
    html += "</form></div></body></html>";

    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "text/html", html);
}

// Recebe os dados digitados, aplica nas globais, salva e exibe a tela de confirmação profissional
void tratarSalvarConfig() {
    const String host = server.hostHeader();
    if ((host != URL_AMIGAVEL && host != "192.168.4.1") ||
        !PortalSeguro::confere(server.arg("token"), tokenPortal)) {
        server.send(403, "text/plain; charset=utf-8", "Abra o portal de configuracao novamente.");
        return;
    }
    // Validar o par antes de alterar qualquer parametro ou contador.
    uint32_t novoSF = loraSF;
    uint32_t novaFreq = rfFrequency;
    if ((server.hasArg("loraSF") && !ConfigRadio::numeroValido(server.arg("loraSF").c_str(), 7, 12, novoSF)) ||
        (server.hasArg("rfFrequency") && !ConfigRadio::numeroValido(server.arg("rfFrequency").c_str(),
            ConfigRadio::FREQUENCIA_MIN, ConfigRadio::FREQUENCIA_MAX, novaFreq))) {
        server.send(400, "text/plain; charset=utf-8", "Radio invalido: use SF7 a SF12 e frequencia entre 902000000 e 928000000 Hz.");
        return;
    }
    bool novaPontaComSono = pontaComSono;
    if (server.hasArg("papelLoRa")) {
        const String papel = server.arg("papelLoRa");
        if (papel != "ponta" && papel != "repetidor") {
            server.send(400, "text/plain; charset=utf-8", "Papel LoRa invalido.");
            return;
        }
        novaPontaComSono = papel == "ponta";
    }
    uint32_t novoTempoAtivo = tempoAtivoMin;
    uint32_t novoTempoSleep = tempoSleepMin;
    if ((server.hasArg("tempoAtivoMin") && !ConfigRadio::numeroValido(server.arg("tempoAtivoMin").c_str(),
            PerfilSono::MIN_MINUTOS, PerfilSono::MAX_MINUTOS, novoTempoAtivo)) ||
        (server.hasArg("tempoSleepMin") && !ConfigRadio::numeroValido(server.arg("tempoSleepMin").c_str(),
            PerfilSono::MIN_MINUTOS, PerfilSono::MAX_MINUTOS, novoTempoSleep))) {
        server.send(400, "text/plain; charset=utf-8", "Tempos do radio invalidos: use de 1 a 1440 minutos.");
        return;
    }
    uint32_t novoId = MEU_ID, novoTimeout = timeoutPaiMs;
    int16_t novoRssi = rssiMinimo;
    const String novoCriterio = server.hasArg("criterioConexao") ? server.arg("criterioConexao") : criterioConexao;
    if ((server.hasArg("myID") && (!ConfigRadio::numeroValido(server.arg("myID").c_str(), 1, 65534, novoId) || novoId == 101)) ||
        (server.hasArg("timeoutPaiMs") && !ConfigRadio::numeroValido(server.arg("timeoutPaiMs").c_str(), 60000, INT32_MAX, novoTimeout)) ||
        (novoCriterio != "RSSI" && novoCriterio != "Saltos")) {
        server.send(400, "text/plain; charset=utf-8", "Parametros do Slave invalidos. O ID 101 pertence ao Master.");
        return;
    }
    if (server.hasArg("rssiMinimo")) {
        const String textoRssi = server.arg("rssiMinimo");
        uint32_t magnitude;
        if (textoRssi != "0" && (!textoRssi.startsWith("-") ||
            !ConfigRadio::numeroValido(textoRssi.c_str() + 1, 1, 150, magnitude))) {
            server.send(400, "text/plain; charset=utf-8", "RSSI minimo deve estar entre -150 e 0 dBm.");
            return;
        }
        novoRssi = textoRssi == "0" ? 0 : -(int16_t)magnitude;
    }
    const bool novoModoCampo = server.hasArg("modoCampoAtivo");
    const bool abriuNvs = preferences.begin("config_params", false);
    const bool gravou = abriuNvs &&
        preferences.putUShort("id_no", (uint16_t)novoId) == sizeof(uint16_t) &&
        preferences.putUChar("loraSF", (uint8_t)novoSF) == sizeof(uint8_t) &&
        preferences.putULong("freq", novaFreq) == sizeof(novaFreq) &&
        preferences.putString("critConex", novoCriterio) == novoCriterio.length() &&
        preferences.putShort("rssiMin", novoRssi) == sizeof(novoRssi) &&
        preferences.putUInt("tPaiMs", novoTimeout) == sizeof(novoTimeout) &&
        preferences.putBool("mCampo", novoModoCampo) == sizeof(uint8_t) &&
        PerfilSono::salvar(preferences, novaPontaComSono, (uint16_t)novoTempoAtivo, (uint16_t)novoTempoSleep);
    if (abriuNvs) preferences.end();
    if (!gravou) {
        server.send(500, "text/plain; charset=utf-8", "Falha ao gravar a configuracao. Tente novamente.");
        return;
    }
    if (!modoCampoAtivo) {
        Serial.println("[INTERFACE] Recebendo novos parâmetros do formulário Web...");
    }

    MEU_ID = (uint16_t)novoId;
    
    loraSF = (uint8_t)novoSF;
    rfFrequency = novaFreq;
    criterioConexao = novoCriterio;
    rssiMinimo = novoRssi;
    timeoutPaiMs = novoTimeout;

    tempoAtivoMin = (uint16_t)novoTempoAtivo;
    tempoSleepMin = (uint16_t)novoTempoSleep;
    pontaComSono = novaPontaComSono;
    
    modoCampoAtivo = novoModoCampo;

    if (!modoCampoAtivo) {
        Serial.println("[INTERFACE] Sucesso: Parâmetros gravados na NVS Flash! Reiniciando o ESP32...");
    }

    // --- INTERFACE DE SUCESSO DO PORTAL ---
    String htmlPortalSucesso = R"rawliteral(
    <!DOCTYPE html><html><head><meta charset='UTF-8'>
    <meta name='viewport' content='width=device-width, initial-scale=1.0'>
    <title>Configurações Salvas</title>
    <style>
      body { font-family: 'Segoe UI', Arial, sans-serif; background: #f0f2f5; display: flex; justify-content: center; align-items: center; height: 100vh; margin: 0; }
      .card-sucesso { background: white; padding: 40px; border-radius: 10px; box-shadow: 0 4px 12px rgba(0,0,0,0.1); text-align: center; max-width: 420px; width: 90%; border-top: 6px solid #3b82f6; }
      .icone { font-size: 50px; color: #3b82f6; margin-bottom: 15px; font-weight: bold; }
      h2 { margin: 0 0 12px 0; color: #1e293b; font-size: 22px; }
      p { color: #475569; font-size: 14px; line-height: 1.6; margin: 0 0 25px 0; }
      .spinner { border: 4px solid #f3f3f3; border-top: 4px solid #1e293b; border-radius: 50%; width: 30px; height: 30px; animation: spin 1s linear infinite; margin: 0 auto; }
      @keyframes spin { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }
    </style>
    </head><body>
      <div class='card-sucesso'>
        <div class='icone'>✓</div>
        <h2>Configurações Salvas!</h2>
        <p>Os parâmetros foram aplicados e salvos. O dispositivo está reiniciando para aplicar o novo perfil de operação.</p>
        <div class='spinner'></div>
      </div>
    </body></html>)rawliteral";

    server.send(200, "text/html", htmlPortalSucesso);
    
    delay(2500);
    ESP.restart(); 
}

// --- Funções Principais Exportadas ---

void interface_iniciarModoConfiguracao() {
    const char* apSSID = "SLAVE_CONFIG";
    
    IPAddress apIP(192, 168, 4, 1);
    WiFi.mode(WIFI_AP);
    const String senhaAp = PortalSeguro::carregarSenha("senha_ap");
    tokenPortal = PortalSeguro::gerarChave();
    if (!senhaAp.length() || !tokenPortal.length() || !WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0)) ||
        !WiFi.softAP(apSSID, senhaAp.c_str())) {
        WiFi.softAPdisconnect(true);
        modoConfiguracaoAtivo = false;
        Serial.println("[PORTAL] Falha ao iniciar AP protegido.");
        return;
    }
    Serial.printf("[PORTAL] Wi-Fi %s, senha: %s\n", apSSID, senhaAp.c_str());

    dnsServer.start(DNS_PORT, "*", apIP);

    if (!modoCampoAtivo) {
        Serial.printf("[INTERFACE] Access Point '%s' iniciado.\n", apSSID);
        Serial.printf("[INTERFACE] Modo Configuração active em: http://%s ou http://192.168.4.1\n", URL_AMIGAVEL);
    }

    server.on("/", HTTP_GET, enviarPaginaHTML);
    server.on("/salvar", HTTP_POST, tratarSalvarConfig);
    
    server.onNotFound([]() {
        server.sendHeader("Location", "http://192.168.4.1/", true);
        server.send(302, "text/plain", ""); 
    });
    
    server.begin();
    modoConfiguracaoAtivo = true;
}

void interface_processarPortal() {
    if (modoConfiguracaoAtivo) {
        dnsServer.processNextRequest();
        server.handleClient();
    }
}
