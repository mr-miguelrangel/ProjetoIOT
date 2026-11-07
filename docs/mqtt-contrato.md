# Contrato MQTT — v1

Entregável da tarefa **T02 — Definir a convenção de tópicos MQTT e o formato JSON**.
Critério de pronto: documento com tópicos, QoS, retain e exemplos de payload no repositório.

Este documento é a fonte da verdade para quem publica e quem assina mensagens no broker: firmware do Master (`gateway-01`), Node-RED, simulador (MQTTX/Wokwi) e testes. Qualquer mudança de tópico ou de campo deve ser feita aqui primeiro.

Contexto da arquitetura: [arquitetura.md](arquitetura.md). A escolha do Mosquitto e do `deviceId` lógico está na [ADR 0005](adr/0005-mosquitto-logical-device-id.md).

## 1. Regras gerais

- **Broker**: Mosquitto na VM, sempre com usuário e senha.
  - Clientes de fora da VM (Master, MQTTX): porta **8883** com TLS.
  - Node-RED: porta **1883**, só dentro da rede interna do Docker.
- **Prefixo**: todos os tópicos começam com `projetoiot/`.
- **Formato do tópico**: `projetoiot/{deviceId}/{tipo}`, com `tipo` em `telemetry`, `commands`, `state` ou `status`.
- **`deviceId`**: ID lógico, não MAC.
  - Formato: letras minúsculas, números e hífen, de 3 a 32 caracteres (regex `^[a-z0-9-]{3,32}$`). Os caracteres `/`, `+` e `#` são proibidos, porque têm significado especial nos tópicos.
  - Workers: `estufa-01`, `estufa-02`, ...
  - Master: `gateway-01`.
- **Payload**: JSON em UTF-8, sem quebras de linha. Tamanho máximo de **200 bytes**. O buffer padrão do PubSubClient tem 256 bytes e precisa caber o pacote inteiro (cabeçalho, tópico e payload); com um `deviceId` de 32 caracteres, o tópico chega a 51 bytes. Para mensagens maiores, o firmware precisa chamar `setBufferSize()`. O exemplo de telemetria da seção 3.1 tem 149 bytes.
- **Horário**: o campo `timestamp` é horário Unix em **segundos**, UTC. Ele é opcional (ver seção 4).
- **Campos desconhecidos**: quem recebe deve ignorar campos que não conhece. Isso permite adicionar campos sem quebrar quem já consome as mensagens.
- **Nomes de campos**: em inglês e em `camelCase`, como no código.

## 2. Tópicos

| Tópico | Direção | Publica | Assina | QoS | Retain |
|---|---|---|---|---|---|
| `projetoiot/{deviceId}/telemetry` | Campo → nuvem | Master, em nome de cada Worker e de si mesmo | Node-RED (`projetoiot/+/telemetry`) | 0 na publicação, 1 na assinatura | Não |
| `projetoiot/{deviceId}/commands` | Nuvem → campo | Node-RED | Master (`projetoiot/+/commands`) | 1 | **Nunca** |
| `projetoiot/{deviceId}/state` | Campo → nuvem | Master, em nome do Worker que tem o atuador | Node-RED (`projetoiot/+/state`) | 0 na publicação, 1 na assinatura | Sim |
| `projetoiot/{deviceId}/status` | Ver seção 3.4 | `gateway-01` para si mesmo; Node-RED para os Workers | Node-RED e dashboard (`projetoiot/+/status`) | 1 | Sim |

### Por que esses valores

- **Telemetria sem retain**: chega uma leitura nova a cada 30 s. Um valor guardado no broker poderia ser antigo e mostrar uma leitura errada no dashboard.
- **Comandos nunca com retain**: se um comando ficasse guardado, o Master o executaria de novo a cada reconexão.
- **Comandos com QoS 1**: perder um comando é pior que recebê-lo duas vezes. Por isso todo comando precisa ser seguro para executar mais de uma vez (seção 3.2).
- **State e status com retain**: quem abre o dashboard depois precisa ver o estado atual na hora, sem esperar a próxima mudança.

### Limitação atual: publicação em QoS 0

O firmware usa o PubSubClient, que **só publica em QoS 0**. Ele aceita assinar em QoS 0 ou 1. Na prática:

- A telemetria e o `state` publicados pelo Master saem em QoS 0. Uma mensagem pode se perder se a conexão cair no meio do envio. Para a telemetria isso é aceitável, porque a próxima leitura chega em 30 s. Para o `state`, o Node-RED trata a falta de confirmação (seção 3.3).
- O LWT do Master pode usar QoS 1, porque o PubSubClient permite escolher o QoS do LWT na conexão.
- O Node-RED publica e assina em QoS 1 normalmente.

Para ter QoS 1 na publicação, o firmware precisaria trocar de biblioteca (por exemplo, AsyncMqttClient ou o cliente MQTT do ESP-IDF). O `IMqttAdapter` (ADR 0001) existe para essa troca não afetar o resto do código.

## 3. Payloads

### 3.1 Telemetria — `projetoiot/{deviceId}/telemetry`

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

| Campo | Tipo | Obrigatório | Descrição |
|---|---|---|---|
| `deviceId` | string | Sim | Igual ao `{deviceId}` do tópico. Se forem diferentes, a mensagem é descartada. |
| `timestamp` | inteiro | Não | Horário Unix da leitura, em segundos. Ausente se o Master ainda não sincronizou o relógio. |
| `seq` | inteiro ≥ 0 | Não | Contador de mensagens de cada dispositivo. Volta a 0 quando ele reinicia. Serve para detectar perdas e repetições. |
| `data` | objeto | Sim | Uma ou mais leituras, com o nome da grandeza como chave (tabela abaixo). Não pode estar vazio. |
| `metadata` | objeto | Não | Dados de operação, que não são leituras de sensor. |
| `metadata.rssi` | inteiro (dBm) | Não | Para Workers: RSSI do pacote LoRa medido pelo Master. Para o `gateway-01`: RSSI do Wi-Fi. |
| `metadata.snr` | número (dB) | Não | SNR do pacote LoRa. Só existe para Workers. |
| `metadata.battery` | inteiro (%) | Não | Carga da bateria, de 0 a 100. |

**Grandezas aceitas em `data`.** A unidade é definida pelo nome do campo e não vai na mensagem.

| Campo | Unidade | Faixa válida | Uso |
|---|---|---|---|
| `temperature` | °C | -10 a 60 | Alerta acima de 35 °C (T15) |
| `humidity` | % (umidade relativa) | 0 a 100 | |

Para adicionar uma grandeza nova, inclua uma linha nesta tabela antes de mudar o firmware ou os fluxos.

**Intervalo de envio**: uma mensagem a cada **30 s** por dispositivo.

### 3.2 Comando — `projetoiot/{deviceId}/commands`

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

| Campo | Tipo | Obrigatório | Descrição |
|---|---|---|---|
| `deviceId` | string | Sim | Igual ao `{deviceId}` do tópico. O `Command.h` atual já exige esse campo. |
| `commandId` | string | Sim | ID único gerado pelo Node-RED (até 32 caracteres). Volta na mensagem de `state` para confirmar o comando (T23). |
| `action` | string | Sim | Ação a executar (tabela abaixo). |
| `params` | objeto | Não | Parâmetros da ação. Se a ação não tiver parâmetros, use `{}` ou omita. |

**Ações:**

| `action` | `params` | Efeito | Resposta |
|---|---|---|---|
| `set` | `{"actuator": "<nome>", "value": <bool>}` | Liga (`true`) ou desliga (`false`) o atuador | Mensagem de `state` com o novo valor e o `lastCommandId` |
| `ping` | `{}` | Teste de comunicação | Mensagem de `state` com o `lastCommandId` e o estado atual |

**Atuadores:**

| `actuator` | Descrição |
|---|---|
| `fan` | Ventilador da estufa |

> A dupla deve confirmar o atuador real do projeto. O `fan` é um exemplo e pode ser trocado por outro nome nesta tabela.

**Repetição de comandos**: com QoS 1, o mesmo comando pode chegar duas vezes. O Master guarda os últimos `commandId` recebidos e ignora repetições. Além disso, o comando `set` sempre define um valor absoluto ("ligar" ou "desligar") e nunca "inverter", então executar duas vezes dá o mesmo resultado.

**Comandos para Workers**: o Master assina `projetoiot/+/commands`, repassa por LoRa os comandos destinados a um Worker e executa os que têm `deviceId` igual a `gateway-01`.

### 3.3 Estado do atuador — `projetoiot/{deviceId}/state`

```json
{
  "deviceId": "estufa-01",
  "timestamp": 1794081605,
  "actuators": {
    "fan": true
  },
  "lastCommandId": "c-1794081600-001"
}
```

| Campo | Tipo | Obrigatório | Descrição |
|---|---|---|---|
| `deviceId` | string | Sim | Igual ao `{deviceId}` do tópico. |
| `timestamp` | inteiro | Não | Horário Unix da mudança de estado. |
| `actuators` | objeto | Sim | Estado atual de **todos** os atuadores do dispositivo, não só do que mudou. |
| `lastCommandId` | string | Não | `commandId` do último comando executado. Ausente se a mudança não veio de um comando (por exemplo, quando o dispositivo liga). |

**Regras:**
- O Master publica o `state` **depois** que o Worker confirma a mudança pelo LoRa, e não quando o comando chega. É isso que permite ao dashboard só mudar o switch quando o atuador mudou de verdade (T23).
- O dispositivo também publica o `state` quando liga, para o valor guardado no broker refletir a realidade.
- **Sem confirmação**: se o Node-RED não receber um `state` com o `lastCommandId` do comando em até **10 s**, ele considera o comando como falho, volta o switch do dashboard e registra o erro. Isso cobre o caso de o `state` se perder (QoS 0) ou de o Worker não responder.

### 3.4 Status online/offline — `projetoiot/{deviceId}/status`

```json
{ "state": "online", "source": "device" }
```

| Campo | Tipo | Obrigatório | Valores |
|---|---|---|---|
| `state` | string | Sim | `online` ou `offline` |
| `source` | string | Sim | Quem gerou a mensagem: `device`, `lwt` ou `watchdog` |
| `timestamp` | inteiro | Não | Horário Unix. Não aparece na mensagem do LWT, porque ela é definida na conexão e não muda depois. |

**Gateway (`gateway-01`), status pelo LWT do MQTT:**

| Momento | Quem publica | Payload | QoS | Retain |
|---|---|---|---|---|
| Na conexão, o Master registra o LWT | broker, se a conexão cair | `{"state":"offline","source":"lwt"}` | 1 | Sim |
| Logo após conectar | Master | `{"state":"online","source":"device"}` | 0 | Sim |
| Desligamento planejado, antes do `disconnect()` | Master | `{"state":"offline","source":"device"}` | 0 | Sim |

**Workers, status pelo watchdog do Node-RED.** Os Workers não têm conexão MQTT própria, então o LWT não funciona para eles.

- Se o Node-RED não receber telemetria de um Worker por **120 s** (4 envios perdidos), ele publica `{"state":"offline","source":"watchdog"}` em `projetoiot/{deviceId}/status`, com QoS 1 e retain.
- Quando a telemetria volta, ele publica `{"state":"online","source":"watchdog"}`.
- Se o `gateway-01` estiver offline, o Node-RED também marca todos os Workers como offline, porque não há como receber dados deles.

O limite de 120 s atende ao critério da T14 ("offline em até 2 min").

## 4. Validação no Node-RED (T13)

Toda mensagem de telemetria passa por estas regras antes de ser gravada ou exibida:

| Situação | O que fazer |
|---|---|
| JSON inválido | Descartar e registrar o erro |
| `deviceId` ausente ou diferente do tópico | Descartar e registrar o erro |
| `data` ausente ou vazio | Descartar e registrar o erro |
| Valor de `data` que não é número ou está fora da faixa da seção 3.1 | Descartar **só aquele valor** e registrar o erro. Se não sobrar nenhum valor, descartar a mensagem |
| Grandeza que não está na tabela da seção 3.1 | Ignorar o valor e registrar um aviso |
| `timestamp` ausente | Usar o horário de chegada ao Node-RED |
| `timestamp` mais de 5 min no futuro ou mais de 24 h no passado | Trocar pelo horário de chegada e registrar um aviso (indica relógio errado) |
| `seq` igual ao da mensagem anterior do mesmo dispositivo | Descartar como repetição |

Depois da validação, o Node-RED acrescenta o campo `local`, consultando uma tabela própria que liga cada `deviceId` a um local (por exemplo, `estufa-01` → `estufa-norte`). O `local` não viaja pelo LoRa nem pelo MQTT. No InfluxDB (T19), `deviceId` e `local` viram tags, e as grandezas de `data` viram campos.

## 5. Permissões no Mosquitto (T07)

### Credenciais

| Usuário | Usado por | Conecta em | Publica em | Assina |
|---|---|---|---|---|
| `gateway-01` | Master (ESP32) | 8883 (TLS) | `projetoiot/+/telemetry`, `projetoiot/+/state`, `projetoiot/gateway-01/status` (inclusive o LWT) | `projetoiot/+/commands` |
| `nodered` | Node-RED na VM | 1883 (rede interna do Docker) | `projetoiot/+/commands`, `projetoiot/+/status` (watchdog) | `projetoiot/+/telemetry`, `projetoiot/+/state`, `projetoiot/+/status` |
| `dev-mqttx` | Testes com MQTTX/Wokwi (T11) | 8883 (TLS) | `projetoiot/#` | `projetoiot/#` |

Regras:
- O Mosquitto roda com `allow_anonymous false`. Conexões sem usuário e senha são recusadas nas duas portas.
- Cada cliente tem credencial própria, para poder trocar ou revogar uma senha sem afetar os outros.
- As senhas ficam no `password_file` do Mosquitto (com hash, gerado pelo `mosquitto_passwd`), no `config.h` do firmware e nas variáveis de ambiente do Node-RED. Nenhuma senha vai para o Git.
- O `dev-mqttx` é só para testes. Desative essa credencial quando o ESP32 real estiver integrado (T29).

### Arquivo de ACL

Arquivo indicado em `acl_file` no `mosquitto.conf`. No Mosquitto, `write` permite publicar, `read` permite assinar e `readwrite` permite os dois.

```text
# Master (gateway-01)
user gateway-01
topic write projetoiot/+/telemetry
topic write projetoiot/+/state
topic write projetoiot/gateway-01/status
topic read  projetoiot/+/commands

# Node-RED
user nodered
topic read      projetoiot/+/telemetry
topic read      projetoiot/+/state
topic readwrite projetoiot/+/status
topic write     projetoiot/+/commands

# Testes (MQTTX / Wokwi)
user dev-mqttx
topic readwrite projetoiot/#
```

O Mosquitto também confere a ACL para o tópico do LWT: o `gateway-01` só consegue registrar o LWT porque tem `write` em `projetoiot/gateway-01/status`.

Limitação: o `nodered` precisa de `write` em `projetoiot/+/status` para o watchdog dos Workers, e esse filtro também cobre `projetoiot/gateway-01/status`. A ACL não impede o Node-RED de sobrescrever o status do gateway, então o fluxo do watchdog não deve publicar para o `gateway-01`.

## 6. Diferenças em relação ao firmware atual

O firmware na branch `main` ainda não segue este contrato. Pontos a ajustar (fora do escopo da T02):

| Item | Firmware atual | Contrato |
|---|---|---|
| `deviceId` | `WiFi.macAddress()` | ID lógico (`gateway-01`, `estufa-01`, ...) |
| Formato de `data` | `{"sensor": "temperatura", "value": 24.5, "unit": "C"}` | `{"temperature": 24.5, "humidity": 61.0}` |
| `timestamp` | `millis()/1000` (tempo desde que a placa ligou) | Horário Unix por NTP, ou ausente |
| Intervalo | 10 s | 30 s |
| Broker | `broker.hivemq.com:1883`, sem TLS e sem senha | Mosquitto na VM, `:8883` com TLS, usuário `gateway-01` e senha |
| LWT e `status` | Não existem | Seção 3.4 |
| `state` e `commandId` | Não existem | Seções 3.2 e 3.3 |
| Assinatura de comandos | Só `projetoiot/{MAC}/commands`. O `handleMqttMessage` reconhece o tópico exato ou `#`, mas não o `+` | `projetoiot/+/commands`, o que exige tratar o `+` no adapter |
