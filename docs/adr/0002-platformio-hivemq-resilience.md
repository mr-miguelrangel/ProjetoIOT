# ADR 0002: Toolchain PlatformIO, HiveMQ Público e Resiliência Não-Bloqueante

## Status
Aceito. O item 2 (broker inicial) foi substituído pelo [ADR 0005](0005-hivemq-cloud-logical-device-id.md): o broker alvo passa a ser o HiveMQ Cloud com TLS na 8883.

## Contexto
Para implementar o firmware C++ com o MQTT Adapter definido no ADR 0001, é necessário estabelecer:
1. O ecossistema de compilação e gerenciamento de dependências.
2. O ambiente inicial de testes para o broker MQTT.
3. A estratégia de resiliência em caso de desconexão.
4. O contrato de dados transmitido.

## Decisão
1. **Toolchain**: Utilizaremos o **PlatformIO** com framework Arduino (com `platformio.ini`), facilitando o gerenciamento de bibliotecas C++ (`ArduinoJson`, clientes MQTT, etc.).
2. **Broker Inicial**: Conexão com o HiveMQ Public Sandbox (`broker.hivemq.com:1883`) sem TLS nesta fase inicial de desenvolvimento para agilizar a validação ponta a ponta da interface `IMqttAdapter`.
3. **Resiliência de Rede**: A reconexão com o Wi-Fi e com o Broker MQTT deve ser **não-bloqueante** (`millis()` e temporizadores/máquina de estados), garantindo que a rotina de controle do microcontrolador não trave durante quedas de sinal.
4. **QoS**: Publicações de telemetria utilizarão **QoS 1** por padrão para garantir a entrega mínima sem descartar mensagens em redes instáveis.

## Consequências
- **Positivas**:
  - Compilação reproduzível via CLI do PlatformIO (`pio run`).
  - Lógica do firmware responsiva mesmo sem rede.
  - Setup imediato sem necessidade prévia de criação de contas ou certificados TLS na nuvem.
- **Negativas/Trade-offs**:
  - `broker.hivemq.com` é público e não confidencial: os tópicos devem utilizar prefixos únicos (`projetoiot/<deviceId>/...`) para evitar colisão com outros usuários públicos.

