# ADR 0003: Alvo de Hardware ESP32 LoRa, Namespace de Tópicos e Isolamento de Credenciais

## Status
Aceito

## Contexto
Com a definição da arquitetura C++ baseada em PlatformIO e Adapter Pattern, foi necessário definir:
1. O hardware alvo exato para compilação e pinout.
2. A convenção de namespace de tópicos MQTT no HiveMQ público para evitar colisões.
3. A estratégia segura de injeção de credenciais de rede Wi-Fi.
4. A estrutura de diretórios do projeto para isolamento entre domínio e infraestrutura.

## Decisão
1. **Hardware Alvo**: Plataforma baseada em **ESP32 com rádio LoRa integrado** (ex.: placas da família TTGO T-Beam / LoRa32 ou Heltec WiFi LoRa 32). O ambiente PlatformIO será configurado para suportar compilação para ESP32 (`board = ttgo-lora32-v1` ou `board = esp32dev`).
2. **Namespace de Tópicos**:
   - Telemetria: `projetoiot/{deviceId}/telemetry`
   - Comandos: `projetoiot/{deviceId}/commands`
3. **Gestão de Credenciais**:
   - Criação de `include/config.h.example` versionado com valores de template.
   - `include/config.h` contendo SSID/senha reais mantido estritamente fora do controle de versão via `.gitignore`.
4. **Estrutura Arquitetural**:
   - `include/domain/`: Interfaces puras C++ (`IMqttAdapter.h`, entidades `TelemetryPayload.h`, `Command.h`).
   - `include/infrastructure/` e `src/infrastructure/`: Implementação concreta do cliente MQTT (`HiveMqAdapter.cpp`) e conectividade.
   - `src/main.cpp`: Ponto de entrada, instanciação e orquestração.

## Consequências
- **Positivas**:
  - Código desacoplado e pronto para suportar tanto conexão direta Wi-Fi/MQTT quanto futuras integrações onde o ESP32 atua como Gateway LoRa-para-MQTT.
  - Segurança garantida contra vazamento de credenciais em commits públicos.
  - Namespace isolado no HiveMQ público minimiza risco de interceptação ou colisão de mensagens.
- **Negativas/Trade-offs**:
  - Desenvolvedores precisam copiar `config.h.example` para `config.h` antes de compilar pela primeira vez.

