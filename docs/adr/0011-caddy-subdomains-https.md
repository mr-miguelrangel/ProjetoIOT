# ADR 0011: Proxy Reverso Caddy com HTTPS e Roteamento por Subdomínios

## Status
Aceito. Relacionado à T08 (Issue #10) e dependente da infraestrutura Docker da T06 (Issue #8, [ADR 0007](0007-docker-compose-stack.md)).

Numeração: criada como 0006, que coincidia com a [ADR 0006 da VM](0006-vm-aws-ec2.md). Foi para 0008 na revisão da T06 e coincidiu de novo, com a [ADR 0008 do Mosquitto](0008-mosquitto-auth-acl.md), criada em paralelo na T07. Ficou com 0011, o próximo número livre depois das ADRs da T09 e da T10.

## Contexto
Na arquitetura do sistema em nuvem (ver [arquitetura.md](../arquitetura.md)), os serviços **Node-RED** (:1880) e **Grafana** (:3000) rodam dentro da rede isolada do Docker. Eles não devem ser expostos diretamente na internet sem criptografia e sem controle centralizado de portas.
Além disso, a tarefa T08 exige que tanto o editor quanto os dashboards abram com `https://` sob um certificado válido.

Precisamos definir:
1. O mecanismo de emissão e renovação de certificados TLS.
2. A topologia de URLs (subdomínios dedicados vs. prefixos de caminhos no mesmo domínio).
3. A estratégia para desenvolvimento local e posterior transição para a nuvem.
4. O manuseio de conexões WebSockets para o editor do Node-RED e visualizações ao vivo no Grafana.

## Decisão
1. **Servidor Web e Proxy Reverso**: Utilização do **Caddy v2** em container Docker.
   - O Caddy gerencia automaticamente o ciclo de vida de certificados TLS (via Let's Encrypt / ZeroSSL em produção, ou Internal CA autoassinada localmente).
   - Suporte nativo e transparente a upgrades de conexão HTTP para **WebSockets** em chamadas `reverse_proxy`.

2. **Roteamento por Subdomínios**:
   - `grafana.{$DOMAIN}` encaminha diretamente para o container `grafana:3000`.
   - `nodered.{$DOMAIN}` encaminha diretamente para o container `nodered:1880`.
   - Esta abordagem evita conflitos de roteamento de assets estáticos e dispensa a reconfiguração de `root_url` ou caminhos base no Node-RED e no Grafana.

3. **Ambiente Local vs. Produção**:
   - Por padrão, o ambiente inicial/local adota certificados internos gerados pelo Caddy (`tls internal` ou certificados locais para domínios `*.localhost`).
   - A configuração é parametrizada via variáveis de ambiente (`DOMAIN`), permitindo que a troca para o domínio público definitivo da VM em produção seja feita sem alterar o `Caddyfile`.

4. **Segurança do Node-RED**:
   - A autenticação do Node-RED é delegada para o mecanismo nativo (`adminAuth` e `httpNodeAuth` em [`infra/nodered/settings.js`](../../infra/nodered/settings.js), ver a [ADR 0010](0010-nodered-auth-security.md)), mantendo o Caddy responsável estritamente pelo roteamento e terminação TLS.

5. **Porta 80 fechada**:
   - O Caddy publica só a 443 ([ADR 0006](0006-vm-aws-ec2.md)). O certificado do Let's Encrypt é emitido pelo desafio TLS-ALPN, na própria 443, e `http://` não redireciona para `https://`.

## Consequências
- **Positivas**:
  - Configuração simples e declarativa no `Caddyfile`, sem necessidade de scripts de renovação com cron/certbot.
  - Subdomínios eliminam problemas com WebSockets e caminhos relativos de scripts do Node-RED e Grafana.
  - Facilidade de rodar localmente com HTTPS confiável via Caddy Internal CA antes do deploy na VM de produção.
- **Negativas / Riscos**:
  - Requer que o ambiente local tenha os domínios mapeados (ex.: `/etc/hosts` ou suporte a `*.localhost`) e, em produção, que as entradas de DNS (tipo A) apontem para a VM.

