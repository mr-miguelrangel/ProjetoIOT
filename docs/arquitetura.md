# Arquitetura do Sistema — v2

Entregável da tarefa **T01 — Definir a arquitetura e desenhar o diagrama**.
Critério de pronto: diagrama com componentes, portas e protocolos aprovado pela dupla.

## Diagrama

```mermaid
flowchart TB
    subgraph CAMPO["Campo / Borda (Edge)"]
        direction TB
        W1["Nó Worker ESP32 + LoRa<br/>(end-device)<br/>deviceId: estufa-01"]
        W2["Nó Worker ESP32 + LoRa<br/>(end-device)<br/>deviceId: estufa-02"]
        MASTER["Nó Master / Gateway<br/>ESP32 + LoRa + Wi-Fi<br/>Firmware C++ (PlatformIO)<br/>IMqttAdapter + HiveMqAdapter<br/>cliente MQTT: gateway-01"]
    end

    subgraph INTERNET["Internet"]
        HIVEMQ["HiveMQ Cloud (broker)<br/>MQTT :8883 TLS + auth<br/>ACL por deviceId"]
        TELEGRAM["Telegram Bot API<br/>(alertas T15)"]
    end

    subgraph VM["VM na Nuvem (Docker Compose)"]
        direction TB
        CADDY["Caddy<br/>Proxy reverso + HTTPS<br/>:443"]
        subgraph DOCKERNET["Rede interna Docker (não exposta)"]
            direction TB
            NODERED["Node-RED<br/>ingestão, validação,<br/>LWT/watchdog, alertas<br/>:1880 (editor)"]
            INFLUX["InfluxDB<br/>séries temporais<br/>tags: deviceId, local<br/>:8086 (interno)"]
            GRAFANA["Grafana<br/>dashboards histórico<br/>:3000 (interno)"]
        end
    end

    subgraph USERS["Usuários / Operação"]
        OPERADOR["Operador<br/>Dashboard + Grafana"]
        ADMIN["Admin<br/>Editor Node-RED + SSH"]
    end

    W1 -.->|"LoRa (rádio)<br/>payload c/ deviceId=estufa-01"| MASTER
    W2 -.->|"LoRa (rádio)<br/>payload c/ deviceId=estufa-02"| MASTER

    MASTER ==>|"Wi-Fi / TCP<br/>MQTT 8883 TLS<br/>pub projetoiot/{deviceId}/telemetry"| HIVEMQ
    HIVEMQ ==>|"sub projetoiot/{deviceId}/commands"| MASTER

    HIVEMQ -->|"MQTT 8883 TLS<br/>sub projetoiot/+/telemetry"| NODERED
    NODERED -->|"MQTT 8883 TLS<br/>pub projetoiot/{deviceId}/commands"| HIVEMQ

    NODERED -->|"write (token escrita) :8086"| INFLUX
    NODERED -->|"HTTPS"| TELEGRAM
    GRAFANA -->|"query :8086"| INFLUX

    CADDY -->|"proxy :1880"| NODERED
    CADDY -->|"proxy :3000"| GRAFANA

    OPERADOR -->|"HTTPS :443"| CADDY
    ADMIN -->|"HTTPS :443"| CADDY
    ADMIN -.->|"SSH :22 (só chave)"| VM

    classDef cloud fill:#e2d9f3,stroke:#6f42c1,color:#333;
    classDef prod fill:#d4edda,stroke:#28a745,color:#333;
    classDef edge fill:#cfe2ff,stroke:#0d6efd,color:#333;

    class HIVEMQ,TELEGRAM cloud;
    class NODERED,INFLUX,GRAFANA,CADDY prod;
    class MASTER,W1,W2 edge;
```

## Componentes

| Componente | Onde roda | Papel |
|---|---|---|
| Nó Worker (`estufa-01`, `estufa-02`, ...) | ESP32 + LoRa no campo | Lê sensores e envia por LoRa ao Master. Não tem Wi-Fi nem identidade MQTT própria. |
| Nó Master (`gateway-01`) | ESP32 + LoRa + Wi-Fi | Recebe pacotes LoRa, extrai o `deviceId` de origem e publica no HiveMQ. Repassa comandos aos Workers. Único cliente MQTT de campo. |
| HiveMQ Cloud | SaaS (internet) | Broker MQTT com TLS, autenticação e ACL por `deviceId`. |
| Node-RED | VM, container Docker | Ingestão, validação/normalização, status online/offline (LWT + watchdog), alertas, dashboard e envio de comandos. |
| InfluxDB | VM, container Docker | Armazena a telemetria validada (tags `deviceId` e `local`). |
| Grafana | VM, container Docker | Painéis de histórico e comparação entre dispositivos. |
| Caddy | VM, container Docker | Proxy reverso com HTTPS para o editor/dashboard do Node-RED e o Grafana. |
| Telegram Bot API | SaaS (internet) | Canal de entrega dos alertas. |

## Portas e protocolos

| Origem → Destino | Protocolo | Porta | Exposição |
|---|---|---|---|
| Worker → Master | LoRa (rádio) | — | Local, sem IP |
| Master ↔ HiveMQ Cloud | MQTT sobre TLS (Wi-Fi/TCP) | 8883 | Internet, com usuário/senha |
| Node-RED ↔ HiveMQ Cloud | MQTT sobre TLS | 8883 | Saída da VM para a internet |
| Node-RED → InfluxDB | HTTP (API v2) | 8086 | Só rede interna Docker |
| Grafana → InfluxDB | HTTP (API v2) | 8086 | Só rede interna Docker |
| Caddy → Node-RED | HTTP | 1880 | Só rede interna Docker |
| Caddy → Grafana | HTTP | 3000 | Só rede interna Docker |
| Usuários → Caddy | HTTPS | 443 | Internet |
| Admin → VM | SSH (só chave) | 22 | Internet |
| Node-RED → Telegram | HTTPS | 443 | Saída da VM para a internet |

Firewall da VM: entrada aberta apenas em **22** e **443**. Como o broker é o HiveMQ Cloud, a VM não precisa abrir a 8883 para entrada (o Node-RED só faz conexão de saída).

## Tópicos MQTT

| Tópico | Direção | Quem publica | Quem assina |
|---|---|---|---|
| `projetoiot/{deviceId}/telemetry` | Campo → nuvem | Master (em nome de cada Worker) | Node-RED (`projetoiot/+/telemetry`) |
| `projetoiot/{deviceId}/commands` | Nuvem → campo | Node-RED | Master |

A convenção detalhada (QoS, retain, LWT e exemplos de payload) é escopo da **T02**.

## Decisões tomadas

As decisões 1 e 3 estão registradas no [ADR 0005](adr/0005-hivemq-cloud-logical-device-id.md).

1. **Broker: HiveMQ Cloud** com TLS na 8883 e autenticação. O Mosquitto self-hosted foi descartado. O HiveMQ Public Sandbox (`broker.hivemq.com:1883`, ADR 0002) fica restrito a testes iniciais de desenvolvimento.
2. **Topologia Master-Worker com Workers LoRa reais** (ADR 0004). Só o Master tem Wi-Fi e fala MQTT.
3. **`deviceId` lógico**, não MAC.
   - Workers: `estufa-01`, `estufa-02`, ... O ID é configurado em cada Worker e viaja dentro do pacote LoRa.
   - Master: `gateway-01`, usado como identidade do cliente MQTT (usuário, ACL, LWT).
   - Motivo: o ID continua o mesmo se uma placa for trocada, é legível nos dashboards e serve direto como tag no InfluxDB e como chave da ACL.
4. **Stack de nuvem em Docker Compose** na VM: Node-RED, InfluxDB, Grafana e Caddy. Só o Caddy (443) fica exposto.

## Impacto no kanban

Com o broker no HiveMQ Cloud, estas tarefas mudam de escopo:

- **T06**: o `docker-compose.yml` sobe Node-RED, InfluxDB, Grafana e Caddy (sem Mosquitto).
- **T07**: passa a ser "configurar usuários e ACL no HiveMQ Cloud" (`gateway-01` publica em `projetoiot/+/telemetry` e assina `projetoiot/+/commands`; Node-RED faz o inverso).
- **T05 / T09**: a 8883 não precisa ser aberta na VM. O TLS do MQTT é fornecido pelo HiveMQ Cloud; a tarefa vira "conectar firmware e Node-RED ao HiveMQ Cloud via 8883 com TLS".
