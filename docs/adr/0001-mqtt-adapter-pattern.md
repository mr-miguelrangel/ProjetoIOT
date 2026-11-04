# ADR 0001: Arquitetura com Adapter Pattern para Comunicação MQTT

## Status
Aceito

## Contexto
O projeto é um firmware de dispositivo IoT em C++ que precisa comunicar leituras e comandos via protocolo MQTT. No momento inicial de desenvolvimento, o broker escolhido para testes é o HiveMQ, mas o ambiente e o provedor de broker final ainda estão em aberto (podendo variar entre brokers públicos, locais, autenticados ou na nuvem).

## Decisão
Adotar o padrão arquitetural **Adapter** (Interface abstrata de cliente de mensageria / MQTT), isolando a lógica de negócio do dispositivo (aquisição de sensores, acionamento de atuadores e tratamento de eventos) da implementação concreta de rede/MQTT.

Uma interface `IMqttClient` ou `MqttAdapter` expõe métodos como:
- `connect()`
- `disconnect()`
- `publish(topic, payload, qos, retain)`
- `subscribe(topic, callback)`
- `loop()` / `handleEvents()`

A primeira implementação concreta será focada no HiveMQ (suportando credenciais e/ou TLS conforme necessário).

## Consequências
- **Positivas**:
  - Troca simples de broker ou de biblioteca MQTT subjacente (ex.: PubSubClient vs AsyncMqttClient vs ESP-IDF nativo) sem refatorar o restante do código.
  - Facilidade de testes unitários com simulação/mock da camada de comunicação.
- **Negativas/Trade-offs**:
  - Pequeno overhead de indireção/polimorfismo (vtable em C++), que em C++ embarcado moderno é negligenciável para fluxos de rede.

