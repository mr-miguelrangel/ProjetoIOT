# ADR 0010: Autenticação e Criptografia de Credenciais no Node-RED (T10)

## Status
Aceito. Relacionado à T10 (Issue #12) e alinhado aos ADRs 0006, 0007 e 0008.

## Contexto
O Node-RED gerencia a lógica de orquestração, regras de negócio e dashboards operacionais da infraestrutura IoT. Em ambiente de nuvem acessível via subdomínio público (`nodered.$DOMAIN`), é mandatório:
1. Impedir o acesso anônimo ao editor de fluxos (`adminAuth`).
2. Proteger os dashboards e endpoints HTTP contra visualizações ou acionamentos não autorizados (`httpNodeAuth`).
3. Garantir a criptografia determinística do arquivo `flows_cred.json` com uma chave secreta declarativa (`credentialSecret`), evitando que o Node-RED gere uma chave aleatória local que impeça a restauração e migração de fluxos com credenciais em múltiplos ambientes.

## Decisão
1. **Arquivo de Configuração Customizado (`infra/nodered/settings.js`)**:
   - Montado como somente leitura em `/data/settings.js` no container do Node-RED.
   - Parametrizado para ler configurações sensíveis de variáveis de ambiente do container.
2. **Autenticação do Editor (`adminAuth`)**:
   - Habilitação do esquema de credenciais baseado em hash bcrypt.
   - Variáveis `NODE_RED_ADMIN_USER` e `NODE_RED_ADMIN_PASSWORD_HASH` documentadas em `.env.example`.
3. **Autenticação de Endpoints e Dashboard (`httpNodeAuth`)**:
   - Proteção dos nós `http in` e telas operacionais com credenciais independentes via `NODE_RED_HTTP_NODE_USER` e `NODE_RED_HTTP_NODE_PASSWORD_HASH`.
4. **Criptografia de Credenciais (`credentialSecret`)**:
   - Chave configurada via `NODE_RED_CREDENTIAL_SECRET` no `docker-compose.yml` e `.env`.
   - Assegura que senhas de conexões MQTT e tokens de API do InfluxDB sejam salvos cifrados no volume `/data/flows_cred.json`.

## Consequências
- **Positivas**:
  - Editor e endpoints HTTP protegidos contra ataques e acessos indevidos.
  - Portabilidade total dos fluxos criptografados entre instâncias de desenvolvimento e produção compartilhando a mesma chave mestra.
  - Credenciais em texto puro permanecem fora do versionamento Git.
- **Negativas**:
  - Exige autenticação básica HTTP para qualquer requisição enviada aos endpoints HTTP do Node-RED.
