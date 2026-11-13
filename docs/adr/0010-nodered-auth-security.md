# ADR 0010: Autenticação e Criptografia de Credenciais no Node-RED (T10)

## Status
Aceito. Relacionado à T10 (Issue #12) e alinhado às ADRs [0006](0006-vm-aws-ec2.md) (VM e firewall), [0007](0007-docker-compose-stack.md) (stack em Docker Compose) e [0011](0011-caddy-subdomains-https.md) (Caddy).

Revisado depois do merge da T10 (ver "Histórico").

## Contexto
O Node-RED gerencia a lógica de orquestração, regras de negócio e dashboards operacionais da infraestrutura IoT. Ele fica atrás do Caddy na 443, aberta para a internet (`nodered.$DOMAIN`). Por isso é preciso:
1. Impedir o acesso anônimo ao editor de fluxos. Pelo editor dá para rodar código dentro do container.
2. Proteger os dashboards e os endpoints HTTP (nós `http in`) contra acesso sem login.
3. Cifrar o `flows_cred.json` com uma chave fixa (`credentialSecret`). Sem ela, o Node-RED gera uma chave aleatória no primeiro boot, e os fluxos com credenciais não podem ser restaurados em outra instância (T27).

## Decisão
1. **Arquivo de configuração versionado**: [`infra/nodered/settings.js`](../../infra/nodered/settings.js), montado somente leitura em `/data/settings.js`. Os valores sensíveis vêm de variáveis de ambiente definidas no `.env`.
2. **Sem senha padrão**: o compose usa `${VAR:?...}` para as senhas e para a chave, e o `settings.js` recusa iniciar se alguma faltar, se uma senha tiver menos de 12 caracteres ou se as duas senhas forem iguais.
3. **Login do editor (`adminAuth`)**: um usuário com acesso total, `NODE_RED_ADMIN_USER` e `NODE_RED_ADMIN_PASSWORD`. A senha fica em texto no `.env` da VM e é comparada em tempo constante, sem gerar hash bcrypt à mão. Não existe usuário anônimo: sem login, a API de administração responde 401.
4. **Login dos endpoints e do dashboard (`httpNodeAuth`)**: autenticação HTTP Basic com credencial separada, `NODE_RED_HTTP_NODE_USER` e `NODE_RED_HTTP_NODE_PASSWORD`, para quem só precisa ver o dashboard. O `httpNodeAuth` recebe uma função em vez do par usuário/hash bcrypt, pelo mesmo motivo do item 3. O Node-RED aplica essa função na raiz dos nós HTTP, depois das rotas do editor, então o login do editor continua separado.
5. **Criptografia de credenciais (`credentialSecret`)**: chave em `NODE_RED_CREDENTIAL_SECRET`. Cifra as senhas do MQTT (T07), o token do InfluxDB e o token do Telegram salvos nos fluxos.
6. **Auditoria**: `logging.console.audit` ligado, para registrar logins e deploys no log do container.

## Consequências
- **Positivas**:
  - Editor e endpoints HTTP protegidos, cada um com sua credencial.
  - Nenhuma senha conhecida no repositório: o `.env.example` deixa os campos vazios.
  - Os fluxos podem ser restaurados em outra VM com a mesma chave.
- **Negativas**:
  - Qualquer requisição aos endpoints HTTP do Node-RED precisa de autenticação básica. Um webhook externo teria que mandar o usuário e a senha do `viewer`.
  - As senhas ficam em texto no `.env` da VM (permissão `600`). Quem tem acesso de root na VM consegue lê-las, mas esse acesso já dá controle total da stack.
  - O login do editor tem um único usuário com acesso total.
  - Perder o `NODE_RED_CREDENTIAL_SECRET` obriga a digitar de novo as credenciais dos fluxos.

## Histórico
- **Versão 1 (T10)**: usava hashes bcrypt em `NODE_RED_ADMIN_PASSWORD_HASH` e `NODE_RED_HTTP_NODE_PASSWORD_HASH`, com o hash de `adminpassword123` e a chave `projetoiot-super-secret-key-change-me` como valores padrão. No merge com o `main`, o `settings.js` ficou com as duas versões coladas, o que gerava `SyntaxError` e impedia o Node-RED de iniciar.
- **Versão 2**: um único `settings.js`, sem valores padrão, com a senha do editor do `main` e o `httpNodeAuth` da T10. Testado com `docker compose up` local, passando pelo Caddy:
  - sem login, `/flows` responde 401; com o login do editor, 200;
  - um nó `http in` responde 401 sem login, com senha errada ou com o login do editor, e 200 com o login do `viewer`;
  - o login do `viewer` não abre o editor (403).
