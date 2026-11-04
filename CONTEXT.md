# Domain Model: ProjetoIOT (Firmware MQTT)

Este documento define a linguagem ubíqua e os conceitos centrais do domínio do firmware IoT.

## Glossário

### Nó Master (Master Node / Gateway)
A unidade ESP32 com LoRa que possui conectividade Wi-Fi e atua como ponte/gateway, sendo o único responsável por se conectar à internet e trocar mensagens MQTT (telemetria e comandos) com o Broker em nome da rede.

### Nó Worker (Worker Node / End-Device)
Dispositivo IoT remoto que se comunica unicamente via rádio LoRa com o Nó Master, sem conexão Wi-Fi direta com a internet.

### Dispositivo (Device / Node)
Conceito genérico para qualquer nó da rede. O `deviceId` identifica unicamente a origem de cada pacote de dados, permitindo ao Nó Master encaminhar a telemetria do Worker correto para o Broker MQTT.

### Broker MQTT
Servidor intermediário responsável por receber todas as mensagens dos publicadores e encaminhá-las aos assinantes correspondentes com base nos tópicos.

### Adaptador de Comunicação MQTT (MQTT Adapter)
Interface abstrata que desacopla a lógica de negócio do dispositivo da biblioteca/provedor concreto de MQTT (ex.: HiveMQ, Mosquitto, AWS IoT), permitindo trocar o broker ou a biblioteca de transporte sem impactar as regras de telemetria e comandos.

### Telemetria (Telemetry)
Fluxo de dados de estado e sensores emitidos pelo Dispositivo em direção à nuvem/broker, enviado com QoS 1 por padrão para assegurar entrega mínima.

### Comando (Command)
Instrução recebida pelo Dispositivo originada externamente para alterar seu estado, disparar uma rotina ou acionar atuadores.

### Payload de Telemetria
Contrato JSON padronizado que encapsula o estado ou leitura de sensores:
- `deviceId`: Identificador único do dispositivo.
- `timestamp`: Timestamp Unix (em segundos ou milissegundos).
- `data`: Objeto contendo os pares sensor/valor (ex.: `sensor`, `value`, `unit`).
- `metadata`: Metadados operacionais opcionais (ex.: `rssi`, `battery`).

### Reconexão Não-Bloqueante (Non-blocking Reconnect)
Estratégia de resiliência onde a perda de conexão com o broker não bloqueia a execução do ciclo de leitura local do microcontrolador, utilizando tentativas periódicas com backoff.

