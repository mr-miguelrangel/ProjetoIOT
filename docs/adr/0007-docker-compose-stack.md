# ADR 0007: Orquestração de Containers com Docker Compose na Nuvem

## Status
Aceito. Relacionado à T06 (Issue #8) e alinhado à arquitetura v3 (`docs/arquitetura.md`) e aos ADRs 0005 e 0006.

## Contexto
A arquitetura do backend em nuvem é composta por cinco serviços interdependentes:
1. **Mosquitto**: Broker MQTT com porta interna (1883) e porta TLS externa (8883).
2. **Node-RED**: Motor de regras, ingestão, watchdog e dashboard operacional (:1880).
3. **InfluxDB 2.x**: Banco de dados de séries temporais com autenticação por token (:8086).
4. **Grafana**: Visualização de dashboards históricos (:3000).
5. **Caddy**: Proxy reverso de borda com terminação TLS automática (:80, :443).

Precisamos orquestrar o ciclo de vida, a persistência e a comunicação interna desses serviços de forma reproduzível e segura.

## Decisão
1. **Orquestrador**: **Docker Compose v2** (`docker compose up -d`) a partir da raiz do repositório.
2. **Topologia de Rede**:
   - Criação de uma rede de ponte interna (`iot-network`).
   - Serviços internos (Node-RED, InfluxDB, Grafana e porta 1883 do Mosquitto) comunicam-se via DNS interno do Docker, sem expor portas diretamente ao host da VM.
   - Apenas o Caddy (:80, :443) e o Mosquitto (:8883) expõem portas no host.
3. **Resiliência e Políticas de Reinício**:
   - Aplicação de `restart: unless-stopped` em todos os containers para garantir auto-recuperação da stack após reinício da VM ou falhas transitórias.
4. **Persistência de Dados**:
   - **Híbrida**: Named volumes para dados dinâmicos (`mosquitto_data`, `mosquitto_log`, `nodered_data`, `influxdb_data`, `grafana_data`, `caddy_data`, `caddy_config`).
   - Bind mounts para arquivos de configuração declarativos versionados em `infra/` (`infra/caddy/Caddyfile`, `infra/mosquitto/config/mosquitto.conf`).
5. **Automação do InfluxDB 2.x**:
   - Inicialização declarativa via variáveis de ambiente (`DOCKER_INFLUXDB_INIT_MODE=setup`, org, bucket, usuário e token de admin parametrizados em `.env`).
6. **Integração Grafana**:
   - Configuração de `GF_SERVER_ROOT_URL=https://grafana.${DOMAIN}` para preservar integridade de redirecionamentos e WebSockets através do proxy Caddy.

## Consequências
- **Positivas**:
  - Toda a infraestrutura sobe com um único comando declarativo (`docker compose up -d`).
  - Isolamento de rede reduz drasticamente a superfície de ataque externa da VM.
  - Setup do InfluxDB automatizado dispensa intervenção manual via browser no primeiro boot.
- **Negativas**:
  - Exige a criação inicial de um arquivo `.env` a partir de `.env.example` antes de iniciar a stack.
