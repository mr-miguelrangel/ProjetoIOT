# ADR 0005: HiveMQ Cloud com TLS e deviceId Lógico

## Status
Aceito. Substitui parcialmente o ADR 0002 (broker inicial) e o ADR 0003 (identificação do dispositivo nos tópicos).

## Contexto
Os ADRs 0002 e 0003 definiram o HiveMQ Public Sandbox (`broker.hivemq.com:1883`, sem TLS nem autenticação) como broker inicial, e o firmware passou a usar o MAC do ESP32 como `deviceId` nos tópicos.

Na definição da arquitetura da etapa de nuvem (tarefa T01, ver [arquitetura.md](../arquitetura.md)) surgiram dois requisitos que essas escolhas não atendem:

1. **Segurança do broker**: as tarefas T07, T09 e T28 exigem autenticação, ACL por dispositivo, TLS na porta 8883 e recusa de clientes sem credenciais. O sandbox público não oferece nenhum desses recursos.
2. **Workers LoRa reais**: os Workers não têm Wi-Fi nem identidade MQTT própria. O MAC disponível no lado MQTT é só o do Master, então ele não identifica de qual Worker veio cada leitura.

Alternativas avaliadas para o broker:
- **Mosquitto self-hosted na VM**: atende os requisitos, mas exige abrir a 8883 na VM e manter certificados e ACL no próprio servidor.
- **HiveMQ Cloud**: atende os requisitos, mantém a tecnologia já usada no firmware (`HiveMqAdapter`) e tira o broker da VM.

## Decisão
1. **Broker**: HiveMQ Cloud, MQTT sobre TLS na porta **8883**, com usuário e senha por cliente e ACL por tópico. O Mosquitto sai da arquitetura. O sandbox público fica restrito a testes iniciais de desenvolvimento.
2. **`deviceId` lógico** no lugar do MAC:
   - Workers: `estufa-01`, `estufa-02`, ... O ID é configurado em cada Worker e enviado dentro do pacote LoRa. O Master usa esse valor para montar o tópico `projetoiot/{deviceId}/telemetry` e o campo `deviceId` do payload.
   - Master: `gateway-01`, usado como identidade do cliente MQTT (client ID, usuário, ACL e LWT).
3. **Clientes do broker**: o Master (`gateway-01`) e o Node-RED. Ambos conectam de saída na 8883 com TLS.

## Consequências
- **Positivas**:
  - Atende autenticação, ACL e TLS sem manter um broker próprio.
  - A VM só precisa de entrada nas portas 22 e 443.
  - O `deviceId` continua o mesmo se uma placa for trocada, é legível nos dashboards e serve direto como tag no InfluxDB (T19) e como chave da ACL (T07).
- **Negativas/Trade-offs**:
  - Dependência de um serviço externo e dos limites do plano gratuito do HiveMQ Cloud.
  - O firmware precisa de mudanças ainda não implementadas: cliente TLS (`WiFiClientSecure` + certificado CA), usuário/senha no `config.h` e troca de `WiFi.macAddress()` pelo `deviceId` lógico em `src/main.cpp`.
  - Os IDs lógicos precisam ser atribuídos e controlados manualmente, sem duplicatas.
  - As tarefas T05, T06, T07 e T09 do kanban mudam de escopo (detalhes em [arquitetura.md](../arquitetura.md#impacto-no-kanban)).
