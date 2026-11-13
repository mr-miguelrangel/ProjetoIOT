# ADR 0008: Autenticação e Controle de Acesso (ACL) no Mosquitto

## Status
Aceito. Relacionado à T07 e alinhado ao contrato MQTT (`docs/mqtt-contrato.md` - Seção 5) e aos ADRs 0005 e 0007.

## Contexto
Com a decisão pelo Mosquitto orquestrado em container na nuvem (ADR 0005 e ADR 0007), é indispensável restringir o acesso ao broker para evitar conexões anônimas, garantir que cada cliente autentique-se individualmente e impor o princípio de privilégio mínimo via ACL (Access Control List).

Conforme o contrato de mensageria:
- O Gateway Master (`gateway-01`) publica telemetria e estado dos dispositivos, mas não pode injetar comandos ou assinar tópicos sensíveis de outros nós além de receber seus próprios comandos.
- O `nodered` precisa ingerir dados dos nós e enviar comandos, além de manter o watchdog em `projetoiot/+/status`.
- O usuário `dev-mqttx` atende ao ambiente de testes e simulação (T11/Wokwi), com permissão ampla no namespace `projetoiot/#`.

## Decisão
1. **Desativação de Acesso Anônimo**:
   - Definição explícita de `allow_anonymous false` em `infra/mosquitto/config/mosquitto.conf`. Conexões sem usuário e senha válidos são rejeitadas tanto na porta 1883 quanto na 8883.
2. **Armazenamento de Senhas (`password_file`)**:
   - As credenciais de acesso são armazenadas no arquivo `infra/mosquitto/config/passwd` com senhas hasheadas pelo utilitário `mosquitto_passwd` (formato PBKDF2/SHA-512).
   - O arquivo `passwd` gerado foi adicionado ao `.gitignore` para prevenir commits acidentais de credenciais. Forneceu-se um modelo `passwd.example` com o comando para geração das chaves.
3. **Regras Declarativas de ACL (`acl_file`)**:
   - Implementação de `infra/mosquitto/config/mosquitto.acl`:
     - **`gateway-01`**:
       - `topic write projetoiot/+/telemetry`
       - `topic write projetoiot/+/state`
       - `topic write projetoiot/gateway-01/status` (incluindo LWT)
       - `topic read  projetoiot/+/commands`
     - **`nodered`**:
       - `topic read      projetoiot/+/telemetry`
       - `topic read      projetoiot/+/state`
       - `topic readwrite projetoiot/+/status`
       - `topic write     projetoiot/+/commands`
     - **`dev-mqttx`**:
       - `topic readwrite projetoiot/#`
4. **Alinhamento com LWT (Last Will and Testament)**:
   - A permissão `write` em `projetoiot/gateway-01/status` garante a validação da mensagem de LWT pelo Mosquitto no momento em que o Master se conecta.

## Consequências
- **Positivas**:
  - Cada cliente tem credencial e privilégio granular conforme a matriz de segurança da T07.
  - Segurança contra injeção indevida de dados ou comandos arbitrários por dispositivos comprometidos.
  - Testes isolados através de usuário segregado (`dev-mqttx`).
- **Negativas / Operacionais**:
  - Exige a inicialização do arquivo `infra/mosquitto/config/passwd` na primeira inicialização da VM antes de subir o broker.
