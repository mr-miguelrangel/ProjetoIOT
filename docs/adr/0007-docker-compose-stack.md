# ADR 0007: Orquestração de Containers com Docker Compose na Nuvem

## Status
Aceito. Relacionado à T06 (Issue #8) e alinhado à arquitetura v3 ([arquitetura.md](../arquitetura.md)), à [ADR 0005](0005-mosquitto-logical-device-id.md) (Mosquitto) e à [ADR 0006](0006-vm-aws-ec2.md) (VM e firewall).

Revisado na mesma tarefa (ver "Histórico").

## Contexto
A arquitetura do backend em nuvem é composta por cinco serviços interdependentes:
1. **Mosquitto**: Broker MQTT com porta interna (1883) e porta TLS externa (8883).
2. **Node-RED**: Motor de regras, ingestão, watchdog e dashboard operacional (:1880).
3. **InfluxDB 2.x**: Banco de dados de séries temporais com autenticação por token (:8086).
4. **Grafana**: Visualização de dashboards históricos (:3000).
5. **Caddy**: Proxy reverso de borda com terminação TLS automática (:443).

Precisamos orquestrar o ciclo de vida, a persistência e a comunicação interna desses serviços de forma reproduzível e segura.

Restrição vinda da [ADR 0006](0006-vm-aws-ec2.md): o Docker cria regras de iptables próprias para as portas publicadas, e elas passam por cima do `ufw` da VM. Toda porta que aparece em `ports` no compose fica exposta, a menos que o grupo de segurança da AWS a bloqueie.

## Decisão
1. **Orquestrador**: **Docker Compose v2** (`docker compose up -d`) a partir da raiz do repositório. Na VM, o Docker Engine e o plugin do Compose vêm do repositório oficial do Docker (passo a passo em [docs/vm.md](../vm.md#deploy-da-stack-t06)).
2. **Topologia de Rede**:
   - Criação de uma rede de ponte interna (`iot-network`).
   - Serviços internos (Node-RED, InfluxDB, Grafana e porta 1883 do Mosquitto) comunicam-se via DNS interno do Docker, sem expor portas diretamente ao host da VM.
   - **Apenas a 443 (Caddy) e a 8883 (Mosquitto) são publicadas no host**, as mesmas portas liberadas no firewall da T05. A 80 não é publicada: o Caddy emite o certificado pelo desafio TLS-ALPN, na própria 443 ([ADR 0011](0011-caddy-subdomains-https.md)).
   - Na 8883, o Mosquitto só aceita conexões com TLS ([ADR 0009](0009-mosquitto-tls-8883.md)). A 1883, sem TLS, fica só na rede interna.
3. **Resiliência e Políticas de Reinício**:
   - Aplicação de `restart: unless-stopped` em todos os containers para garantir auto-recuperação da stack após reinício da VM ou falhas transitórias.
4. **Persistência de Dados**:
   - **Híbrida**: Named volumes para dados dinâmicos (`mosquitto_data`, `mosquitto_log`, `nodered_data`, `influxdb_data`, `grafana_data`, `caddy_data`, `caddy_config`).
   - Bind mounts, somente leitura, para arquivos de configuração versionados em `infra/` (`infra/caddy/Caddyfile`, `infra/mosquitto/config/`, `infra/nodered/settings.js`).
   - Bind mount, somente leitura, para arquivos gerados na VM e fora do Git: `infra/mosquitto/config/passwd` (T07) e `infra/mosquitto/certs/` (T09). O Mosquitto roda com o usuário 1883, então o `passwd` e o `server.key` precisam pertencer a ele ([docs/vm.md](../vm.md#deploy-da-stack-t06)).
5. **Segredos só no `.env`**:
   - Senhas e tokens (Node-RED, InfluxDB e Grafana) ficam no `.env`, que não vai para o Git. O `.env.example` traz os nomes e deixa os valores vazios.
   - O compose usa `${VAR:?mensagem}` para esses campos, sem valor padrão. Se o `.env` faltar ou um campo estiver vazio, o `docker compose up` para com erro, em vez de subir com uma senha conhecida exposta na 443.
6. **Login no editor do Node-RED**:
   - O editor fica atrás do Caddy na 443, aberta para a internet. Pelo editor dá para rodar código dentro do container, então ele exige login.
   - O `infra/nodered/settings.js` configura o `adminAuth` com um único usuário, lido de `NODE_RED_ADMIN_USER` e `NODE_RED_ADMIN_PASSWORD`. Não existe usuário anônimo: sem login, a API de administração responde 401.
   - O mesmo arquivo define o `credentialSecret` (de `NODE_RED_CREDENTIAL_SECRET`), que cifra as credenciais salvas nos fluxos, como a senha do MQTT (T07) e o token do InfluxDB.
   - Na T10, o arquivo ganhou um segundo login, só para os nós `http in` e o dashboard ([ADR 0010](0010-nodered-auth-security.md)).
   - Se alguma dessas variáveis faltar, ou se uma senha tiver menos de 12 caracteres, o Node-RED não inicia.
7. **Automação do InfluxDB 2.x**:
   - Inicialização declarativa via variáveis de ambiente (`DOCKER_INFLUXDB_INIT_MODE=setup`, org, bucket, retenção de 30 dias, usuário e token de admin no `.env`).
   - O setup só roda com o volume `influxdb_data` vazio. Trocar a senha ou o token no `.env` depois disso não muda os valores gravados.
8. **Integração Grafana**:
   - Configuração de `GF_SERVER_ROOT_URL=https://grafana.${DOMAIN}` para preservar integridade de redirecionamentos e WebSockets através do proxy Caddy.
9. **Versões fixas das imagens**: `eclipse-mosquitto:2.0`, `nodered/node-red:5.0.9`, `influxdb:2.7`, `grafana/grafana:13.3.1` e `caddy:2-alpine`. Com `latest`, recriar a VM (T27) poderia trazer uma versão maior diferente da testada. A atualização é feita trocando a tag no compose.

## Consequências
- **Positivas**:
  - Toda a infraestrutura sobe com um único comando declarativo (`docker compose up -d`).
  - O compose publica só as portas liberadas no firewall, então ele continua seguro mesmo que alguém abra uma porta a mais no grupo de segurança.
  - Nenhum serviço exposto sobe com senha padrão, e o editor do Node-RED exige login.
  - Setup do InfluxDB automatizado dispensa intervenção manual via browser no primeiro boot.
- **Negativas**:
  - Exige a criação inicial de um arquivo `.env` a partir de `.env.example`, com todas as senhas preenchidas, antes de iniciar a stack.
  - O login do Node-RED tem um único usuário com acesso total. Para dar acessos diferentes à dupla, seria preciso listar mais usuários no `settings.js`.
  - Perder o `NODE_RED_CREDENTIAL_SECRET` obriga a digitar de novo as credenciais dos fluxos.
  - As versões fixas não recebem correções sozinhas. A dupla precisa atualizar as tags de tempos em tempos.

## Histórico
- **Versão 1 (T06)**: publicava a 80 e a 443 no Caddy, tinha senhas padrão no compose (`adminpassword123`), deixava o editor do Node-RED sem login e usava `latest` no Node-RED e no Grafana.
- **Versão 2 (T06)**: só 443 e 8883 publicadas, conforme a ADR 0006; segredos obrigatórios no `.env`; `adminAuth` e `credentialSecret` no Node-RED; versões fixas. Testado com `docker compose up` local: sem login, `/flows` responde 401; com login, 200; a senha antiga do Grafana é recusada.
