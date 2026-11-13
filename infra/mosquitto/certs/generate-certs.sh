#!/usr/bin/env bash
# ==============================================================================
# Script de geração de certificados TLS autoassinados para desenvolvimento local
# Suporta a T09 (porta 8883 do Mosquitto e validação no ESP32/MQTTX)
# ==============================================================================
set -euo pipefail

CERTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DOMAIN="${1:-localhost}"

echo "Gerando certificados TLS autoassinados para domínio: ${DOMAIN}..."

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

# Ajustar permissões para leitura do Mosquitto
chmod 644 "${CERTS_DIR}/ca.crt" "${CERTS_DIR}/server.crt"
chmod 600 "${CERTS_DIR}/server.key" "${CERTS_DIR}/ca.key"

echo "Certificados gerados com sucesso em ${CERTS_DIR}:"
echo "  - ca.crt (fornecer ao ESP32 / MQTTX para validação TLS)"
echo "  - server.crt"
echo "  - server.key"
