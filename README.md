# ProjetoIOT - Firmware ESP32 Gateway (MQTT / LoRa)

Repositório de firmware para dispositivo IoT (ESP32 Master / Gateway) integrando protocolo MQTT com broker HiveMQ e preparação para roteamento de nós LoRa.

## 📐 Arquitetura

O projeto adota o padrão **Adapter** para isolar a lógica de comunicação da aplicação:
- `include/domain/`: Interfaces puras e modelos de domínio (`IMqttAdapter.h`, `TelemetryPayload.h`, `Command.h`).
- `include/infrastructure/` e `src/infrastructure/`: Implementação concreta do cliente MQTT com HiveMQ (`HiveMqAdapter.h`/`.cpp`).
- `src/main.cpp`: Ponto de entrada do firmware, ciclo de vida e orquestração.

Para detalhes de design e evolução das decisões arquiteturais:
- [Glossário de Domínio (CONTEXT.md)](CONTEXT.md)
- [ADR 0001 - Padrão Adapter para MQTT](docs/adr/0001-mqtt-adapter-pattern.md)
- [ADR 0002 - PlatformIO, HiveMQ e Reconexão Não-Bloqueante](docs/adr/0002-platformio-hivemq-resilience.md)
- [ADR 0003 - Alvo de Hardware, Tópicos e Isolamento de Credenciais](docs/adr/0003-esp32-lora-structure.md)
- [ADR 0004 - Topologia Master-Worker e Libs do Firmware](docs/adr/0004-master-gateway-topology.md)

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

- **Telemetria**: `projetoiot/<MAC_DO_DISPOSITIVO>/telemetry`
- **Comandos**: `projetoiot/<MAC_DO_DISPOSITIVO>/commands`

### Exemplo de Payload de Telemetria (JSON)
```json
{
  "deviceId": "AA:BB:CC:DD:EE:FF",
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
  "deviceId": "AA:BB:CC:DD:EE:FF",
  "action": "ping",
  "params": {}
}
```

