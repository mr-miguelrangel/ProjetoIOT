# ADR 0005: Mosquitto na VM com TLS e deviceId Lógico

## Status
Aceito. Substitui parcialmente o ADR 0002 (broker inicial) e o ADR 0003 (identificação do dispositivo nos tópicos).

Revisado durante a T02. A primeira versão desta ADR escolhia o HiveMQ Cloud como broker. A escolha foi trocada antes de qualquer implementação (ver "Histórico").

## Contexto
Os ADRs 0002 e 0003 definiram o HiveMQ Public Sandbox (`broker.hivemq.com:1883`, sem TLS nem autenticação) como broker inicial, e o firmware passou a usar o MAC do ESP32 como `deviceId` nos tópicos.

Na definição da arquitetura da etapa de nuvem (T01, ver [arquitetura.md](../arquitetura.md)) surgiram dois requisitos que essas escolhas não atendem:

1. **Segurança do broker**: as tarefas T07, T09 e T28 exigem autenticação, ACL por tópico, TLS na porta 8883 e recusa de clientes sem credenciais. O sandbox público não oferece nenhum desses recursos.
2. **Workers LoRa reais**: os Workers não têm Wi-Fi nem identidade MQTT própria. O MAC disponível no lado MQTT é só o do Master, então ele não identifica de qual Worker veio cada leitura.

Alternativas avaliadas para o broker:

| Alternativa | Por que foi descartada ou escolhida |
|---|---|
| HiveMQ Cloud, plano gratuito (Serverless) | Não aceita clusters novos e para de funcionar em 31/12/2026, antes do fim do projeto ([HiveMQ Cloud Serverless End of Life](https://docs.hivemq.com/hivemq-cloud/serverless-end-of-life.html)). Além disso, só permite uma permissão por credencial, sem ACL por tópico ([comparação de planos](https://docs.hivemq.com/hivemq-platform/connect/access-management-options-per-cloud-plan.html)). |
| HiveMQ Cloud Starter | Tem ACL por tópico, mas é pago (cerca de US$ 0,34 por hora mais o tráfego, segundo a [página de preços](https://www.hivemq.com/pricing/)). Inviável para o projeto. |
| HiveMQ Enterprise na VM com licença Lab | Gratuito, mas limitado a 25 conexões, a licença precisa ser renovada manualmente a cada 30 dias e a configuração é mais pesada. |
| **Mosquitto na VM** | **Escolhido.** Gratuito, sem prazo de validade, com usuário e senha, ACL por tópico e TLS. Roda em Docker junto com o resto da stack. |

## Decisão
1. **Broker**: Mosquitto em container Docker na VM.
   - Porta **8883** com TLS, exposta para a internet, para o Master e para testes com MQTTX.
   - Porta **1883** sem TLS, só na rede interna do Docker, para o Node-RED.
   - `allow_anonymous false` nas duas portas, com uma credencial por cliente e ACL por tópico. Os usuários e a ACL estão na seção 5 do [contrato MQTT](../mqtt-contrato.md).
   - O certificado TLS usa o mesmo domínio configurado para o Caddy (T08).
   - O sandbox público do HiveMQ fica restrito a testes iniciais de desenvolvimento, até a VM estar pronta.
2. **`deviceId` lógico** no lugar do MAC:
   - Workers: `estufa-01`, `estufa-02`, ... O ID é configurado em cada Worker e enviado dentro do pacote LoRa. O Master usa esse valor para montar o tópico `projetoiot/{deviceId}/...` e o campo `deviceId` do payload.
   - Master: `gateway-01`, usado como identidade do cliente MQTT (client ID, usuário, ACL e LWT).
3. **Clientes do broker**: o Master (`gateway-01`, pela 8883) e o Node-RED (`nodered`, pela 1883 interna). A credencial `dev-mqttx` existe só para testes.

## Consequências
- **Positivas**:
  - Atende autenticação, ACL por tópico e TLS sem custo e sem depender de um serviço externo.
  - O critério da T07 ("cada dispositivo só publica nos próprios tópicos") pode ser cumprido como está escrito.
  - O tráfego entre Node-RED e broker não sai da VM.
  - O `deviceId` continua o mesmo se uma placa for trocada, é legível nos dashboards e serve direto como tag no InfluxDB (T19) e como chave da ACL (T07).
- **Negativas/Trade-offs**:
  - A dupla mantém o broker: atualizações do container, arquivo de senhas, ACL e renovação do certificado TLS.
  - A VM precisa abrir a 8883 para entrada, além de 22 e 443.
  - Se a VM cair, o broker cai junto com o resto da stack.
  - O firmware precisa de mudanças ainda não implementadas: cliente TLS (`WiFiClientSecure` + certificado CA), usuário/senha no `config.h` e troca de `WiFi.macAddress()` pelo `deviceId` lógico em `src/main.cpp`.
  - Os IDs lógicos precisam ser atribuídos e controlados manualmente, sem duplicatas.

## Histórico
- **Versão 1 (T01)**: escolhia o HiveMQ Cloud, para manter o broker fora da VM. As tarefas T05, T06, T07 e T09 chegaram a ser ajustadas para isso.
- **Versão 2 (T02)**: troca para o Mosquitto, depois de confirmado o encerramento do plano gratuito do HiveMQ Cloud e a falta de ACL por tópico nele. As tarefas T05, T06, T07 e T09 voltaram ao escopo original do kanban. O adapter do firmware foi renomeado de `HiveMqAdapter` para `PubSubMqttAdapter`, porque não depende de nenhum broker específico.
