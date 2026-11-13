# ProjetoIOT - Firmware ESP32 Gateway (MQTT / LoRa)

Repositório do projeto IoT de monitoramento de estufa: firmware do ESP32 Master (Gateway), que recebe dados dos Workers por LoRa e os publica por MQTT, e a documentação da etapa de nuvem (Mosquitto, Node-RED, InfluxDB e Grafana em Docker).

## 📐 Arquitetura

O projeto adota o padrão **Adapter** para isolar a lógica de comunicação da aplicação:
- `include/domain/`: Interfaces puras e modelos de domínio (`IMqttAdapter.h`, `TelemetryPayload.h`, `Command.h`).
- `include/infrastructure/` e `src/infrastructure/`: Implementação concreta do cliente MQTT com a biblioteca PubSubClient (`PubSubMqttAdapter.h`/`.cpp`). Não depende de nenhum broker específico.
- `src/main.cpp`: Ponto de entrada do firmware, ciclo de vida e orquestração.

Para detalhes de design e evolução das decisões arquiteturais:
- [Arquitetura do Sistema (diagrama, componentes, portas e protocolos)](docs/arquitetura.md)
- [Contrato MQTT (tópicos, QoS, retain, payloads e ACL)](docs/mqtt-contrato.md)
- [Glossário de Domínio (CONTEXT.md)](CONTEXT.md)
- [ADR 0001 - Padrão Adapter para MQTT](docs/adr/0001-mqtt-adapter-pattern.md)
- [ADR 0002 - PlatformIO, HiveMQ e Reconexão Não-Bloqueante](docs/adr/0002-platformio-hivemq-resilience.md)
- [ADR 0003 - Alvo de Hardware, Tópicos e Isolamento de Credenciais](docs/adr/0003-esp32-lora-structure.md)
- [ADR 0004 - Topologia Master-Worker e Libs do Firmware](docs/adr/0004-master-gateway-topology.md)
- [ADR 0005 - Mosquitto na VM com TLS e deviceId Lógico](docs/adr/0005-mosquitto-logical-device-id.md)
- [ADR 0006 - VM na AWS EC2 com SSH só por chave](docs/adr/0006-vm-aws-ec2.md)
- [ADR 0007 - Stack da nuvem com Docker Compose](docs/adr/0007-docker-compose-stack.md)
- [ADR 0008 - Usuários e ACL no Mosquitto](docs/adr/0008-mosquitto-auth-acl.md)
- [ADR 0009 - TLS no Mosquitto na porta 8883](docs/adr/0009-mosquitto-tls-8883.md)
- [ADR 0010 - Login e credenciais no Node-RED](docs/adr/0010-nodered-auth-security.md)
- [ADR 0011 - Caddy com HTTPS e subdomínios](docs/adr/0011-caddy-subdomains-https.md)
- [VM na nuvem: criação, firewall, verificação (T05) e deploy da stack (T06)](docs/vm.md)

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

A definição completa (QoS, retain, campos, faixas válidas e ACL) está no [Contrato MQTT](docs/mqtt-contrato.md). Resumo:

| Tópico | Direção | Retain |
|---|---|---|
| `projetoiot/<deviceId>/telemetry` | Master → nuvem | Não |
| `projetoiot/<deviceId>/commands` | Nuvem → Master | Nunca |
| `projetoiot/<deviceId>/state` | Master → nuvem (estado dos atuadores) | Sim |
| `projetoiot/<deviceId>/status` | Online/offline (LWT do Master e watchdog do Node-RED) | Sim |

O `deviceId` é um ID lógico ([ADR 0005](docs/adr/0005-mosquitto-logical-device-id.md)): `estufa-01`, `estufa-02`, ... para os Workers LoRa e `gateway-01` para o Master.

O broker é o **Mosquitto na VM**, com usuário e senha. O Master conecta na porta **8883** com TLS.

> **Estado atual do firmware:** o código ainda não segue o contrato. Ele usa o HiveMQ público (`broker.hivemq.com:1883`, sem TLS e sem senha), o MAC do ESP32 como `deviceId` e o formato antigo de `data` (`sensor`/`value`/`unit`). A lista completa de diferenças está na [seção 6 do contrato](docs/mqtt-contrato.md#6-diferenças-em-relação-ao-firmware-atual).

### Exemplo de Payload de Telemetria (JSON)
```json
{
  "deviceId": "estufa-01",
  "timestamp": 1794081600,
  "seq": 1532,
  "data": {
    "temperature": 24.5,
    "humidity": 61.0
  },
  "metadata": {
    "rssi": -87,
    "snr": 7.5,
    "battery": 92
  }
}
```

### Exemplo de Payload de Comando (JSON)
```json
{
  "deviceId": "estufa-01",
  "commandId": "c-1794081600-001",
  "action": "set",
  "params": {
    "actuator": "fan",
    "value": true
  }
}
```

