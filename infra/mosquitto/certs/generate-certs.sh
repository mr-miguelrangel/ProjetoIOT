#!/usr/bin/env bash
# ==============================================================================
# Script de geração de certificados TLS autoassinados para desenvolvimento local
# Suporta a T09 (porta 8883 do Mosquitto e validação no ESP32/MQTTX)
#
# Uso, a partir da raiz do repositório:
#   ./infra/mosquitto/certs/generate-certs.sh <dominio>
# Na VM, use o mesmo DOMAIN do .env. Sem argumento, gera para localhost.
#
# O Mosquitto roda no container com o usuário 1883. No fim, o script passa o server.key
# para esse usuário (com sudo, se não estiver rodando como root). Sem isso, a chave fica
# com permissão 600 e dono "ubuntu", e o broker não sobe ("Unable to load server key file").
# ==============================================================================
set -euo pipefail

CERTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DOMAIN="${1:-localhost}"
MOSQUITTO_UID=1883

echo "Gerando certificados TLS autoassinados para domínio: ${DOMAIN}..."

# Apaga os arquivos de uma execução anterior. Isso funciona mesmo com o server.key
# pertencendo ao usuário 1883, porque quem apaga é o dono da pasta.
rm -f "${CERTS_DIR}"/{ca.key,ca.crt,ca.srl,server.key,server.csr,server.crt,server.ext}

# 1. Gerar Root CA
openssl req -new -x509 -days 365 -extensions v3_ca \
  -keyout "${CERTS_DIR}/ca.key" \
  -out "${CERTS_DIR}/ca.crt" \
  -nodes \
  -subj "/C=BR/ST=SP/L=SaoPaulo/O=ProjetoIOT/OU=Dev/CN=ProjetoIOT-CA"

# 2. Gerar chave e requisição (CSR) do servidor
openssl req -new \
  -out "${CERTS_DIR}/server.csr" \
  -keyout "${CERTS_DIR}/server.key" \
  -nodes \
  -subj "/C=BR/ST=SP/L=SaoPaulo/O=ProjetoIOT/OU=Broker/CN=${DOMAIN}"

# 3. Criar arquivo de extensões SAN (Subject Alternative Name)
cat > "${CERTS_DIR}/server.ext" <<EOF
authorityKeyIdentifier=keyid,issuer
basicConstraints=CA:FALSE
keyUsage = digitalSignature, nonRepudiation, keyEncipherment, dataEncipherment
subjectAltName = @alt_names

[alt_names]
DNS.1 = ${DOMAIN}
DNS.2 = localhost
DNS.3 = mqtt.${DOMAIN}
IP.1 = 127.0.0.1
EOF

# 4. Assinar o certificado do servidor com a CA gerada
openssl x509 -req -days 365 \
  -in "${CERTS_DIR}/server.csr" \
  -CA "${CERTS_DIR}/ca.crt" \
  -CAkey "${CERTS_DIR}/ca.key" \
  -CAcreateserial \
  -out "${CERTS_DIR}/server.crt" \
  -extfile "${CERTS_DIR}/server.ext"

# Ajustar permissões para leitura do Mosquitto.
# O ca.key fica só com quem rodou o script: o broker não precisa dele.
chmod 644 "${CERTS_DIR}/ca.crt" "${CERTS_DIR}/server.crt"
chmod 600 "${CERTS_DIR}/server.key" "${CERTS_DIR}/ca.key"

if [ "$(id -u)" -eq 0 ]; then
  chown "${MOSQUITTO_UID}:${MOSQUITTO_UID}" "${CERTS_DIR}/server.key"
elif command -v sudo >/dev/null 2>&1; then
  sudo chown "${MOSQUITTO_UID}:${MOSQUITTO_UID}" "${CERTS_DIR}/server.key"
else
  # Docker Desktop no Windows: a pasta montada não tem dono Unix, então não precisa.
  echo "Aviso: sem sudo. Em Linux, rode como root: chown ${MOSQUITTO_UID}:${MOSQUITTO_UID} ${CERTS_DIR}/server.key"
fi

echo "Certificados gerados com sucesso em ${CERTS_DIR}:"
echo "  - ca.crt (fornecer ao ESP32 / MQTTX para validação TLS)"
echo "  - server.crt"
echo "  - server.key"
