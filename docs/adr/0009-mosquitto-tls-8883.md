# ADR 0009: Ativação de TLS no Mosquitto na Porta 8883 e Isolamento da Porta 1883

## Status
Aceito. Relacionado à T09 (Issue #11) e alinhado aos ADRs 0005, 0006, 0007 e 0008.

## Contexto
O contrato de arquitetura do ProjetoIOT define que a comunicação externa (ESP32 Master, clientes de simulação Wokwi e MQTTX) deve trafegar criptografada pela internet com TLS na porta `8883`. Ao mesmo tempo, a porta padrão MQTT `1883` em texto puro deve ficar estritamente confinada à rede privada interna do Docker (`iot-network`), permitindo que apenas o container do Node-RED se comunique com o Mosquitto sem sobrecarga de TLS, eliminando qualquer risco de sniffing na borda pública da VM.

## Decisão
1. **Configuração dos Listeners no Mosquitto (`mosquitto.conf`)**:
   - `listener 1883`: mantido ativo sem TLS para comunicação estritamente interna no Docker. Não possui mapeamento de portas (`ports:`) para o host no `docker-compose.yml`.
   - `listener 8883`: configurado com suporte a TLS (`tlsv1.2`), exposto no `docker-compose.yml` (`8883:8883`).
2. **Estratégia de Certificados TLS**:
   - **Terminação direta no Mosquitto**: a porta `8883` é atendida diretamente pelo broker.
   - **Integração com Caddy (Produção)**: o `Caddyfile` define o subdomínio `mqtt.{$DOMAIN}` gerenciando a emissão ACME pelo Let's Encrypt / ZeroSSL. O volume do Caddy (`caddy_data`) é montado no container do Mosquitto como somente leitura (`/caddy_data:ro`), permitindo usar os certificados gerenciados na nuvem.
   - **Desenvolvimento Local (`generate-certs.sh`)**: fornecimento de um script reprodutível em `infra/mosquitto/certs/generate-certs.sh` para gerar certificados autoassinados com Subject Alternative Names (SAN) válidos para `localhost`, `127.0.0.1` e `${DOMAIN}`.
3. **Validação no Firmware do ESP32**:
   - O firmware no modo seguro usará `WiFiClientSecure` validando o certificado da autoridade certificadora raiz (`MQTT_ROOT_CA`). Em produção, a raiz é a `ISRG Root X1` da Let's Encrypt; em desenvolvimento, é o `ca.crt` gerado.

## Consequências
- **Positivas**:
  - Toda a telemetria e controle externo via MQTT são criptografados em trânsito com TLS 1.2+.
  - A porta `1883` permanece inacessível via internet pública.
  - O ESP32 e o MQTTX utilizam validação estrita de CA, garantindo autenticidade do broker e protegendo contra ataques man-in-the-middle (MitM).
- **Negativas**:
  - Exige que o cliente ESP32 tenha memória RAM para o handshake TLS (`WiFiClientSecure`), o que já é suportado pelo hardware ESP32.
