# ProjetoIOT - Firmware ESP32 Gateway (MQTT / LoRa)

Repositório de firmware para dispositivo IoT (ESP32 Master / Gateway) integrando protocolo MQTT com broker HiveMQ e preparação para roteamento de nós LoRa.

## 📐 Arquitetura

O projeto adota o padrão **Adapter** para isolar a lógica de comunicação da aplicação:
- `include/domain/`: Interfaces puras e modelos de domínio (`IMqttAdapter.h`, `TelemetryPayload.h`, `Command.h`).
- `include/infrastructure/` e `src/infrastructure/`: Implementação concreta do cliente MQTT com HiveMQ (`HiveMqAdapter.h`/`.cpp`).
- `src/main.cpp`: Ponto de entrada do firmware, ciclo de vida e orquestração.

Para detalhes de design e evolução das decisões arquiteturais:
- [Arquitetura do Sistema (diagrama, componentes, portas e protocolos)](docs/arquitetura.md)
- [Glossário de Domínio (CONTEXT.md)](CONTEXT.md)
- [ADR 0001 - Padrão Adapter para MQTT](docs/adr/0001-mqtt-adapter-pattern.md)
- [ADR 0002 - PlatformIO, HiveMQ e Reconexão Não-Bloqueante](docs/adr/0002-platformio-hivemq-resilience.md)
- [ADR 0003 - Alvo de Hardware, Tópicos e Isolamento de Credenciais](docs/adr/0003-esp32-lora-structure.md)
- [ADR 0004 - Topologia Master-Worker e Libs do Firmware](docs/adr/0004-master-gateway-topology.md)
- [ADR 0005 - HiveMQ Cloud com TLS e deviceId Lógico](docs/adr/0005-hivemq-cloud-logical-device-id.md)

---

## 🚀 Como Compilar e Usar

### Pré-requisitos
- [PlatformIO](https://platformio.org/) (CLI ou extensão no VSCode/Cursor/IDE).
- Placa de desenvolvimento ESP32 (ex: ESP32 Dev Module, TTGO LoRa32, Heltec LoRa32).

### Configuração de Credenciais
Copie o arquivo de exemplo para criar a sua configuração local:
```bash
cp include/config.h.example include/config.h
```
Abra `include/config.h` e preencha o seu SSID e senha de Wi-Fi. O arquivo `include/config.h` é ignorado pelo Git para proteção de dados sensíveis.

### Compilação e Gravação
```bash
# Compilar firmware
pio run

# Gravar no ESP32 conectado via USB
pio run --target upload

# Abrir monitor serial
pio device monitor -b 115200
```

---

## 📡 Tópicos MQTT

- **Telemetria**: `projetoiot/<deviceId>/telemetry`
- **Comandos**: `projetoiot/<deviceId>/commands`

O `deviceId` é um ID lógico ([ADR 0005](docs/adr/0005-hivemq-cloud-logical-device-id.md)): `estufa-01`, `estufa-02`, ... para os Workers LoRa e `gateway-01` para o Master. O broker alvo é o HiveMQ Cloud com TLS na porta 8883.

> **Estado atual do firmware:** o código ainda usa o HiveMQ público (`broker.hivemq.com:1883`, sem TLS) e o MAC do ESP32 como `deviceId`. A migração para o ADR 0005 está pendente.

### Exemplo de Payload de Telemetria (JSON)
```json
{
  "deviceId": "estufa-01",
  "timestamp": 123456,
  "data": {
    "sensor": "temperatura",
    "value": 24.5,
    "unit": "C"
  },
  "metadata": {
    "rssi": -65,
    "battery": 100
  }
}
```

### Exemplo de Payload de Comando (JSON)
```json
{
  "deviceId": "estufa-01",
  "action": "ping",
  "params": {}
}
```

