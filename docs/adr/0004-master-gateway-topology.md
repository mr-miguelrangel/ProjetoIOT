# ADR 0004: Arquitetura de Topologia Master-Worker e Libs do Firmware

## Status
Aceito

## Contexto
O ecossistema do projeto IoT opera com dispositivos ESP32 equipados com rádio LoRa. Era necessário esclarecer a topologia de rede entre os nós e definir a seleção das bibliotecas C++ para o cliente MQTT e serialização JSON.

## Decisão
1. **Topologia de Rede (Master-Worker)**:
   - Apenas o **Nó Master (Gateway)** possui conexão Wi-Fi ativa e realiza a comunicação MQTT direta com o Broker HiveMQ.
   - Os Nós Workers comunicam-se via LoRa com o Master (fora do escopo direto desta issue MQTT, mas condicionando o design dos tópicos e payloads para suportar múltiplos `deviceId`s roteados pelo Master).
2. **Ambiente de Compilação**:
   - `board = esp32dev` no `platformio.ini`, compatível com a família ESP32 LoRa.
3. **Bibliotecas Subjacentes do Firmware**:
   - `PubSubClient` (v2.8+) encapsulado dentro da implementação concreta `PubSubMqttAdapter` (chamada `HiveMqAdapter` até a revisão do [ADR 0005](0005-mosquitto-logical-device-id.md)).
   - `ArduinoJson` (v7+) para serialização e desserialização eficiente de payloads JSON estruturados.

## Consequências
- **Positivas**:
  - Economia extrema de energia e consumo de rede: os nós remotos não precisam de Wi-Fi nem de stack TCP/IP.
  - O `IMqttAdapter` implementado no Master fica preparado para publicar mensagens contendo `deviceId` dinâmico (tanto local do Master quanto dos Workers que ele gerencia).
- **Negativas/Trade-offs**:
  - O Nó Master torna-se um ponto central de comunicação com o broker.

