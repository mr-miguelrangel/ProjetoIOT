# IDC – Mesh LoRa de temperatura e umidade (firmware + nuvem)

Projeto final de Internet das Coisas – ICT/Unifesp, São José dos Campos.

Nós escravos (Slave) medem temperatura e umidade com um DHT11 e enviam as leituras por uma malha
LoRa (mesh) até o nó mestre (Master). O Master publica as leituras via MQTT, e a etapa de nuvem
(broker MQTT + Node-RED) mostra tudo no dashboard e grava no banco de dados.

```
 nó 1 ─┐
 nó 2 ─┼─ LoRa mesh ─> Master ── Wi-Fi / MQTT ──> Mosquitto :1883 ──> Node-RED :1880 ──> dashboard
 nó 3 ─┘                                                                   └──> SQLite (histórico)
```

Um nó pode falar direto com o Master ou através de outro nó (repetidor): a malha escolhe o caminho sozinha.

## Estrutura

```
codigos/master/             # firmware do nó mestre (Heltec WiFi LoRa 32 V3): mesh LoRa + MQTT + painel web
codigos/slave/              # firmware dos nós (Heltec WiFi LoRa 32 V3): DHT11 + mesh LoRa
compose.yml                 # sobe os dois serviços: mosquitto e nodered
mosquitto/mosquitto.conf    # broker na porta 1883, acesso anônimo
nodered/Dockerfile          # imagem do Node-RED + @flowfuse/node-red-dashboard + node-red-node-sqlite
nodered/flows.json          # o flow (carregado automaticamente ao subir)
docs/evidencias/            # capturas de tela do funcionamento
```

## Como rodar

Pré-requisito: Docker (com Docker Compose).

```sh
docker compose up -d --build
```

- Dashboard: <http://localhost:1880/dashboard> (páginas "Temperatura e Umidade" e "Histórico", no menu ☰)
- Editor do Node-RED: <http://localhost:1880>
- Broker MQTT: `<ip-do-computador>:1883` (é esse endereço que o mestre deve usar)

A pasta `nodered/` é montada como `/data` do Node-RED. Assim, o `flows.json` versionado é carregado
ao subir o container, e o que for alterado e implantado ("Deploy") no editor é gravado de volta no
`flows.json`, pronto para commit. Os demais arquivos que o Node-RED cria nessa pasta são ignorados pelo git.

Para parar: `docker compose down`.

## Contrato MQTT

O firmware do mestre (`codigos/master/`) segue este contrato.

| Tópico | Payload | Quem publica |
|---|---|---|
| `idc/sensores/<id>/leituras` | JSON (abaixo) | mestre, uma mensagem por leitura de cada nó |
| `idc/mestre/status` | `online` ou `offline` (texto) | mestre |

**Leituras.** `<id>` é `no` + o ID numérico do nó (o nó com ID 1 publica como `no1`, e assim por
diante). O dashboard tem painéis para `no1`, `no2` e `no3`; nós com outros IDs são gravados no banco,
mas ainda não aparecem nos gauges. Exemplo de payload:

```json
{"temperatura": 25.4, "umidade": 61.2, "rssi": -87}
```

| Campo | Tipo | Unidade | Obrigatório |
|---|---|---|---|
| `temperatura` | número | °C | sim |
| `umidade` | número | % | sim |
| `rssi` | número | dBm (RSSI do LoRa) | não |

No firmware, o `rssi` é o do último salto: o sinal com que o Master ouviu o nó que entregou o pacote.
Para um nó que chega por um repetidor, é o RSSI do repetidor.

**Status do mestre.** Ao conectar, o mestre publica `online` com *retain*. Na conexão ele registra um
Last Will no mesmo tópico com payload `offline` e *retain*: se o mestre cair, o broker publica `offline`
por ele. O *retain* faz o dashboard saber o status atual mesmo se for aberto ou reiniciado depois.

Configuração usada no mestre: broker na porta 1883, sem usuário/senha, QoS 0 nas leituras, retain
no status (o Last Will é registrado com QoS 1) e `client_id` `idc-mestre`.

## Firmware (Master e Slave)

As duas placas são **Heltec WiFi LoRa 32 (V3)**. Cada pasta abre direto na Arduino IDE
(`codigos/master/master.ino` e `codigos/slave/slave.ino`).

**Placa e bibliotecas** (Arduino IDE):

| O quê | Onde | Master | Slave |
|---|---|---|---|
| Placa "Heltec WiFi LoRa 32(V3)" | pacote de placas da Heltec (`heltec:esp32`) | ✓ | ✓ |
| Heltec ESP32 Dev-Boards | gerenciador de bibliotecas | ✓ | ✓ |
| PubSubClient (Nick O'Leary) | gerenciador de bibliotecas | ✓ | |
| DHT sensor library + Adafruit Unified Sensor | gerenciador de bibliotecas | | ✓ |

**Ligação do DHT11 no Slave:** dados no **GPIO 4** (`DHT_PIN` em `codigos/slave/sensor_dht.h`), VCC em 3,3 V e GND.

**Como os dados viajam.** Cada slot do pacote LoRa tem 25 bytes; 4 deles levam a medida:
temperatura × 10 (int16) e umidade × 10 (uint16). A codificação está em `medida_sensor.h`, que é
**igual** nas duas pastas. Se mudar de um lado, copie para o outro. O Slave lê o DHT11 a cada 10 s
e envia a leitura mais recente a cada ciclo (60 s). Um nó que ainda não conseguiu ler o sensor
continua na malha, mas o Master não publica nada por ele.

### Configurar o Master

1. Grave o `codigos/master/` e abra o Serial Monitor (115200): a cada boot ele mostra a senha da rede de
   recuperação `MASTER_CONFIG` e a senha do usuário `admin` do painel (geradas no primeiro boot e
   guardadas na placa).
2. Conecte na rede `MASTER_CONFIG` e abra <http://192.168.4.1/wifi> (usuário `admin`).
3. Preencha o Wi-Fi, o IP fixo do Master na sua rede e o **Broker MQTT**: o IP do computador onde
   roda o `docker compose`. Salve; o Master reinicia.

Se o IP do computador mudar (outra rede, outro dia), basta voltar em `/wifi` e trocar o broker.
O painel do Master (`http://<ip-do-master>/`) mostra o estado do MQTT, e o display também
(`MQTT OK`, `--` desconectado ou `OFF` sem broker).

### Configurar os Slaves

No Slave, toque no botão **PRG** até chegar na tela **SISTEMA** e toque 3× rápido para abrir o menu.
Toque para descer até "Iniciar Ponto de Acesso" e segure 2 s para escolher. Conecte na rede
`SLAVE_CONFIG` (a senha aparece no Serial Monitor) e abra <http://192.168.4.1>. Ali se
define o **ID do nó** (1 vira `no1`, 2 vira `no2`...) e os parâmetros de rádio. SF e frequência
precisam ser iguais em todos os nós e no Master. Cada nó precisa de um ID único, diferente do ID do
Master (101).

### Economia de energia (extra)

Na mesma página, em "Gerenciamento de Energia", cada Slave pode ser configurado como:

- **Repetidor** (padrão): o rádio LoRa fica sempre ligado, e o nó encaminha as leituras de outros nós.
- **Nó de ponta**: só envia a própria leitura e o rádio LoRa pode dormir. Ele fica ligado por um tempo
  mínimo (padrão 10 min) e depois dorme (padrão 50 min). O DHT11 continua lendo durante o sono.

Pensado para nós alimentados por bateria; o sono do rádio foi testado na versão 5.5.1 do firmware. Na apresentação todos os nós ficam como repetidor: um nó de
ponta dormindo passa do limite de 60 s e aparece como "SEM DADOS" no dashboard.

## O flow no Node-RED

1. **mqtt in** `idc/sensores/+/leituras`, com saída já como JSON parseado.
2. **function "identifica nó e horário"**: extrai o id do tópico (`msg.no`), anexa o horário de
   recebimento (`msg.recebido`) e guarda esse horário no contexto do flow.
3. **switch "separa por nó"**: envia cada leitura para os gauges do seu nó. Cada gauge lê o
   campo do payload que exibe (`payload.temperatura` ou `payload.umidade`), sem código.
4. **ui-chart** de temperatura e de umidade: uma série por nó (`msg.no`), guardando os últimos 30 min.
5. **inject a cada 1 s + function "tempo desde a última leitura"**: atualiza o texto "há X s" de cada
   nó e o aviso de **nó sem dados** quando um nó passa de 60 s sem enviar (ou nunca enviou).
6. **mqtt in** `idc/mestre/status` + **change** (JSONata): mostra 🟢 online / 🔴 offline.
7. **debug** com as leituras recebidas, para ajudar nos testes.
8. Grupo **Banco de dados**: grava as leituras no SQLite e alimenta a página Histórico
   (detalhes na seção abaixo).

## Testando sem o hardware

Dá para fazer o papel do Master publicando as mensagens do contrato à mão com o `mosquitto_pub`.

Com o `mosquitto_pub` instalado na máquina:

```sh
mosquitto_pub -h localhost -t idc/mestre/status -m online -r
mosquitto_pub -h localhost -t idc/sensores/no1/leituras -m '{"temperatura": 25.4, "umidade": 61.2, "rssi": -87}'
mosquitto_pub -h localhost -t idc/mestre/status -m offline -r
```

Sem instalar nada, usando o cliente que já vem no container do broker:

```sh
docker compose exec mosquitto mosquitto_pub -h localhost -t idc/sensores/no2/leituras -m '{"temperatura": 31.5, "umidade": 55.0}'
```

Para gerar leituras contínuas dos três nós (a cada 5 s, até Ctrl+C):

```sh
while true; do
  for n in 1 2 3; do
    docker compose exec -T mosquitto mosquitto_pub -h localhost -t idc/sensores/no$n/leituras \
      -m "{\"temperatura\": $((20 + RANDOM % 10)).$((RANDOM % 10)), \"umidade\": $((50 + RANDOM % 20)).$((RANDOM % 10))}"
  done
  sleep 5
done
```

Para ver tudo o que passa no broker:

```sh
docker compose exec mosquitto mosquitto_sub -h localhost -t 'idc/#' -v
```

## Evidências de funcionamento

Testes feitos com leituras simuladas em 13/11/2026 (`docs/evidencias/`):

**Leituras chegando**: mestre online, os três nós atualizando gauges e gráficos, "há 2 s" em cada nó.

![dashboard com dados](docs/evidencias/dashboard.png)

**Mestre desconectado à força**: o Last Will publicou `offline`, e após mais de 60 s sem leituras
os três nós aparecem como "SEM DADOS".

![dashboard sem dados](docs/evidencias/dashboard_sem_dados.png)

Também foi verificado que uma mensagem enviada com `mosquitto_pub` (comando acima) chega ao flow
(nó *debug*).

**Flow completo no editor do Node-RED**: recepção MQTT, gauges e gráficos, "nó sem dados", status
do mestre e o grupo "Banco de dados".

![flow completo no Node-RED](docs/evidencias/flow_node_red.png)

> **Pendente:** uma captura com o Master real enviando dados dos Slaves.

## Banco de dados (desafio extra)

Todas as leituras válidas são gravadas em um banco **SQLite**, e a página **Histórico** do dashboard
consulta esse banco.

### Por que SQLite

- **Simples:** é só instalar o nó `node-red-node-sqlite` (no `nodered/Dockerfile`); não há container
  de banco, servidor, usuário ou senha para configurar.
- **Arquivo único:** o banco inteiro é o arquivo `/data/historico.db`. Como `/data` é a pasta
  `nodered/` do repositório, o arquivo fica em `nodered/historico.db` no computador e **sobrevive a
  reinícios** do container. Ele não vai para o git (`.gitignore`).
- **Suficiente para o volume do projeto:** 3 nós com uma leitura a cada poucos segundos.

### Esquema

A tabela e o índice são criados automaticamente quando o flow inicia (`CREATE ... IF NOT EXISTS`):

```sql
CREATE TABLE IF NOT EXISTS leituras (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    id_no TEXT NOT NULL,          -- no1, no2, no3
    temperatura REAL NOT NULL,    -- °C
    umidade REAL NOT NULL,        -- %
    rssi INTEGER,                 -- dBm, pode ser nulo
    recebido_em TEXT NOT NULL     -- ISO 8601 em UTC, horário do servidor (ex.: 2026-11-14T00:12:17.133Z)
);
CREATE INDEX IF NOT EXISTS idx_leituras_no_data ON leituras (id_no, recebido_em);
```

### Como funciona no flow (grupo "Banco de dados")

1. **Cria a tabela** ao iniciar.
2. **Valida e grava**: cada leitura com `temperatura` e `umidade` numéricas vira um `INSERT` com
   parâmetros (`msg.params`, sem concatenar strings). Mensagens com JSON inválido (capturadas por um
   nó *catch* no `mqtt in`) ou sem temperatura/umidade vão para o debug **"leituras descartadas"** e
   não são gravadas; o resto do flow continua funcionando.
3. **Página Histórico**: filtros de nó (todos, no1, no2, no3) e período (última hora, 24 h, 7 dias),
   gráficos de temperatura e umidade, tabela com as últimas 50 leituras do filtro e o total de
   registros no banco. As consultas rodam ao abrir a página e ao trocar um filtro. Nos gráficos cada
   ponto é a média de um intervalo (30 s na última hora, 12 min em 24 h, 84 min em 7 dias), para que
   o período de 7 dias não fique pesado; a tabela e o CSV trazem as leituras individuais.
4. **Exportar CSV**: o botão baixa `historico.csv` com as leituras do filtro atual. O mesmo arquivo
   pode ser baixado direto em <http://localhost:1880/historico.csv>.

### Consultar os dados manualmente

O container não tem o programa `sqlite3`, mas o arquivo está no computador em `nodered/historico.db`.
Ele pode ser aberto com o [DB Browser for SQLite](https://sqlitebrowser.org/) ou, com o `sqlite3`
instalado na máquina:

```sh
sqlite3 nodered/historico.db "SELECT COUNT(*) FROM leituras;"
sqlite3 nodered/historico.db "SELECT * FROM leituras ORDER BY id DESC LIMIT 10;"
```

Sem instalar nada, usando o Node.js de dentro do container:

```sh
docker compose exec nodered node -e "new (require('sqlite3').Database)('/data/historico.db').all('SELECT id_no, COUNT(*) AS n FROM leituras GROUP BY id_no', (e, r) => console.table(r))"
```

### Backup

O backup é a cópia do arquivo. Para garantir uma cópia consistente, pare o Node-RED antes:

```sh
docker compose stop nodered
cp nodered/historico.db backup-historico-2026-11-13.db
docker compose start nodered
```

Para restaurar, faça o caminho inverso (com o Node-RED parado, copie o backup para `nodered/historico.db`).
Para começar do zero, pare o Node-RED e apague `nodered/historico.db`; a tabela é recriada ao subir.

### Roteiro de demonstração (apresentação)

1. Subir tudo: `docker compose up -d --build`.
2. Ligar o Master e os Slaves e deixar enviando por alguns minutos (sem o hardware, usar o laço de
   `mosquitto_pub` da seção [Testando sem o hardware](#testando-sem-o-hardware)).
3. Abrir <http://localhost:1880/dashboard>, ir em ☰ → **Histórico** e mostrar os gráficos, a tabela e
   o total de registros. Trocar o filtro de nó e de período. Clicar em **Exportar CSV**.
4. Anotar o total de registros e reiniciar os containers: `docker compose restart`.
5. Recarregar a página **Histórico**: as leituras de antes do reinício continuam lá e o total é o
   mesmo (ou maior, se os nós continuaram enviando).
6. (Opcional) Enviar uma mensagem malformada e mostrar que nada quebra; ela aparece no debug
   "leituras descartadas" do editor:
   `docker compose exec mosquitto mosquitto_pub -h localhost -t idc/sensores/no1/leituras -m 'isso nao e json'`

### Evidências

Testes feitos com leituras simuladas em 13/11/2026 (`docs/evidencias/`). Mensagens malformadas (texto que
não é JSON, JSON sem umidade, temperatura como texto) foram descartadas sem afetar o flow.

**Antes do reinício**: leituras enviadas por alguns minutos e interrompidas; o banco tem 333 registros.

![histórico antes do reinício](docs/evidencias/historico_antes.png)

**Depois de `docker compose restart`**: os mesmos 333 registros, gráficos e tabela continuam lá.

![histórico depois do reinício](docs/evidencias/historico_depois_restart.png)

**CSV exportado**: arquivo `historico.csv` baixado pelo botão "Exportar CSV" (cabeçalho + 333 leituras;
o print mostra as primeiras 40 linhas).

![CSV exportado](docs/evidencias/historico_csv.png)

**Grupo "Banco de dados" no editor do Node-RED**

![grupo Banco de dados no flow](docs/evidencias/flow_banco_de_dados.png)

## Decisões de simplicidade

Por ser uma rede local de laboratório e para manter o projeto mínimo, **não há** autenticação, TLS
nem backend próprio. Os gráficos da página principal guardam apenas os últimos 30 min em memória;
o histórico completo fica no SQLite (seção acima), dentro do próprio Node-RED.
