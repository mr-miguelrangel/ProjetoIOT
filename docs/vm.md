# VM na nuvem (AWS EC2)

Entregável da tarefa **T05 — Criar a VM na nuvem com acesso SSH por chave**.
Critério de pronto: SSH só por chave; firewall aberto apenas em 22, 443 e 8883.

A escolha do provedor e do tamanho da VM está na [ADR 0006](adr/0006-vm-aws-ec2.md).

## Resumo

| Item | Valor |
|---|---|
| Provedor e região | AWS EC2, US East (Ohio), `us-east-2`. A região é fixa nas contas do cadastro novo da AWS (ver o [passo 1](#1-conferir-a-conta)). |
| Conta | Cadastro novo da AWS (conta organizada em projeto), no plano Free |
| Instância | `projetoiot-vm`, tipo `t3.small` (2 vCPU, 2 GiB de RAM, x86_64), crédito de CPU `standard` |
| Sistema | Ubuntu Server 24.04 LTS |
| Disco | 20 GiB gp3, com swap de 2 GiB em `/swapfile` |
| IP | IP elástico (fixo). O valor não fica no repositório, que é público. |
| Acesso | SSH com o usuário `ubuntu`, só por chave ed25519, uma chave por pessoa |
| Firewall | Grupo de segurança `projetoiot-vm-sg` na AWS e `ufw` na VM, os dois com entrada só em 22, 443 e 8883 |
| Primeiro boot | [`infra/vm/cloud-init.yaml`](../infra/vm/cloud-init.yaml) |
| Verificação | [`infra/vm/verificar-vm.ps1`](../infra/vm/verificar-vm.ps1) |

## Firewall

Entrada liberada:

| Porta | Protocolo | Origem | Uso |
|---|---|---|---|
| 22 | TCP | 0.0.0.0/0 | SSH, só com chave |
| 443 | TCP | 0.0.0.0/0 | Caddy: editor e dashboard do Node-RED e Grafana (T08) |
| 8883 | TCP | 0.0.0.0/0 | Mosquitto com TLS (T09) |

Qualquer outra entrada é descartada, inclusive a 80 e as portas internas da stack (1880, 1883, 3000 e 8086). A saída fica liberada, porque a VM precisa baixar pacotes e imagens Docker, falar com o Let's Encrypt e enviar os alertas ao Telegram.

As mesmas regras ficam em duas camadas:

- **Grupo de segurança da AWS** (`projetoiot-vm-sg`): filtra o tráfego antes de ele chegar à VM. É a camada que protege os containers, porque o Docker cria regras de iptables próprias para as portas publicadas, e elas passam por cima do `ufw`. Por isso o `docker-compose.yml` da T06 só pode publicar a 443 e a 8883.
- **`ufw` na VM**: protege os serviços que rodam direto no sistema, caso alguém abra uma porta a mais no grupo de segurança.

A porta 80 fica fechada, como pede o kanban. O Caddy da T08 consegue emitir o certificado do Let's Encrypt pela 443 (desafio TLS-ALPN). A consequência é que `http://` não redireciona para `https://`.

A 22 fica aberta para qualquer origem, porque o IP de casa muda e a dupla acessa de lugares diferentes. O que protege o SSH é a exigência de chave.

## SSH

O `cloud-init.yaml` grava o arquivo `/etc/ssh/sshd_config.d/00-projetoiot.conf`:

| Opção | Valor | Efeito |
|---|---|---|
| `PasswordAuthentication` | `no` | Recusa login por senha |
| `KbdInteractiveAuthentication` | `no` | Recusa senha pelo modo teclado interativo (PAM) |
| `PermitRootLogin` | `no` | Recusa o usuário root, mesmo com chave |
| `PubkeyAuthentication` | `yes` | Aceita chave pública |
| `AuthenticationMethods` | `publickey` | Deixa a chave como único método aceito |

O sshd fica com o primeiro valor que lê para cada opção, e o Ubuntu lê os arquivos de `sshd_config.d` em ordem alfabética, antes do `sshd_config`. O prefixo `00` garante que essas opções valham.

A imagem Ubuntu da AWS já vem sem login por senha. O arquivo deixa isso explícito e acrescenta as outras opções da tabela.

## Passo a passo

### 1. Conferir a conta

- A conta precisa estar no **plano Free**. O plano, o saldo de créditos e a data de término aparecem no console de Billing and Cost Management.
- A conta foi criada pelo cadastro novo da AWS, com login do Google, do GitHub ou AWS Builder ID. Nesse cadastro, a AWS fixa a região pelo país do endereço de contato, e o console não tem seletor de região. Para o Brasil, a região é **US East (Ohio), `us-east-2`** ([AWS](https://docs.aws.amazon.com/accounts/latest/reference/project-regions.html)). Para conferir: https://settings.aws.com → **Project** → **Overview** → **Additional Info** → **Region**.
- Esse tipo de conta não tem usuário root. Ative o MFA em https://settings.aws.com → **Manage security and sign-in**. Se entrou com Google ou GitHub, também dá para ativar a verificação em duas etapas direto neles ([AWS](https://docs.aws.amazon.com/accounts/latest/reference/register-mfa-devices.html)).

### 2. Gerar a chave SSH

No seu PC:

```powershell
ssh-keygen -t ed25519 -a 100 -f "$env:USERPROFILE\.ssh\projetoiot_vm" -C "pessoa-a@projetoiot"
```

Defina uma passphrase. A chave privada (`projetoiot_vm`) não sai do seu PC e não vai para o repositório. A AWS só recebe a pública (`projetoiot_vm.pub`).

Para não digitar a passphrase a cada conexão, carregue a chave no ssh-agent do Windows (opcional):

```powershell
# Uma vez, no PowerShell como administrador:
Get-Service ssh-agent | Set-Service -StartupType Automatic
Start-Service ssh-agent

# No PowerShell normal:
ssh-add "$env:USERPROFILE\.ssh\projetoiot_vm"
```

### 3. Importar a chave pública

EC2 → **Rede e segurança → Pares de chaves** (Key pairs) → **Ações → Importar par de chaves** (Import key pair):

- Nome: `projetoiot-pessoa-a`
- Conteúdo: o arquivo `projetoiot_vm.pub`

### 4. Executar a instância

EC2 → **Instâncias → Executar instâncias** (Launch instances):

| Campo | Valor |
|---|---|
| Nome | `projetoiot-vm` |
| Imagem (AMI) | Ubuntu Server 24.04 LTS (HVM), SSD Volume Type, arquitetura 64 bits (x86) |
| Tipo de instância | `t3.small`, marcada como qualificada para o nível gratuito (Free tier eligible) |
| Par de chaves | `projetoiot-pessoa-a` |
| Configurações de rede | Clique em **Editar** e em **Criar grupo de segurança**. Nome: `projetoiot-vm-sg`. Regras de entrada: `ssh` (TCP 22), `HTTPS` (TCP 443) e `TCP personalizado` na porta 8883, todas com origem **Qualquer lugar** (0.0.0.0/0). Nenhuma outra regra. |
| Configurar armazenamento | 20 GiB, gp3 |
| Detalhes avançados → Especificação de crédito (Credit specification) | `Standard`. No `Unlimited`, que é o padrão da T3, o uso de CPU acima da linha de base é cobrado à parte e gasta crédito. No `Standard`, a VM só fica mais lenta. |
| Detalhes avançados → Dados do usuário (User data) | Cole o conteúdo inteiro de [`infra/vm/cloud-init.yaml`](../infra/vm/cloud-init.yaml) |

### 5. Associar um IP elástico

EC2 → **Rede e segurança → IPs elásticos** → **Alocar endereço IP elástico** → **Alocar**. Com o IP selecionado: **Ações → Associar endereço IP elástico**, escolha a instância `projetoiot-vm` e clique em **Associar**.

Com o IP elástico, o endereço não muda quando a VM é parada e iniciada, e o domínio da T08 aponta para ele.

### 6. Primeira conexão

Espere uns 5 minutos. No primeiro boot, o cloud-init atualiza os pacotes e pode reiniciar a VM uma vez.

```powershell
ssh -i "$env:USERPROFILE\.ssh\projetoiot_vm" ubuntu@<IP_ELASTICO>
```

Na primeira conexão, o ssh mostra a impressão digital (fingerprint) da VM e pede confirmação. Para conferir se é a sua VM, abra a instância no console → **Ações → Monitorar e solucionar problemas → Obter log do sistema**. As impressões digitais aparecem entre `BEGIN SSH HOST KEY FINGERPRINTS` e `END SSH HOST KEY FINGERPRINTS`.

Dentro da VM, `cloud-init status --wait` precisa terminar com `status: done`.

Para entrar só com `ssh projetoiot-vm`, acrescente um atalho ao `~/.ssh/config` (troque o IP):

```powershell
Add-Content -Encoding ascii -Path "$env:USERPROFILE\.ssh\config" -Value @"

Host projetoiot-vm
    HostName <IP_ELASTICO>
    User ubuntu
    IdentityFile ~/.ssh/projetoiot_vm
    IdentitiesOnly yes
"@
```

O `-Encoding ascii` é necessário: o ssh do Windows não lê o arquivo salvo em UTF-16, que é o padrão de alguns comandos do Windows PowerShell 5.1.

### 7. Verificar o critério de pronto

Na raiz do repositório:

```powershell
.\infra\vm\verificar-vm.ps1 -Endereco <IP_ELASTICO>
```

| Verificação | Esperado agora | Depois da T06 |
|---|---|---|
| Porta 22 | `aberta` | `aberta` |
| Portas 443 e 8883 | `recusada` (liberada, mas sem serviço ouvindo) | `aberta` |
| Portas 80, 1880, 1883, 3000 e 8086 | `bloqueada` (sem resposta) | `bloqueada` |
| Login sem chave | O servidor oferece só `publickey` | Igual |
| `sshd -T` e `ufw status verbose` | Valores da seção [SSH](#ssh); `ufw` ativo, negando entrada e liberando só 22, 443 e 8883 | Igual |

O script termina com `Tudo certo` e código 0 quando tudo passa. Se a chave tiver passphrase e não estiver no ssh-agent, o ssh pede a passphrase uma vez.

Se o PowerShell disser que a execução de scripts está desabilitada, rode assim:

```powershell
powershell -ExecutionPolicy Bypass -File .\infra\vm\verificar-vm.ps1 -Endereco <IP_ELASTICO>
```

Rode o script de casa ou pelo 4G. Algumas redes, como a da faculdade, bloqueiam a saída para a 22 ou a 8883, e aí o teste falha por causa da rede, não da VM.

O teste de portas vê o efeito das duas camadas juntas. O print das regras de entrada do grupo de segurança completa a evidência.

## Acesso da Pessoa B

Cada pessoa usa a própria chave, e ninguém compartilha chave privada.

1. A Pessoa B gera a chave dela com o comando do passo 2, trocando o comentário do `-C`, e envia só o arquivo `.pub`.
2. A Pessoa A acrescenta a chave na VM:

   ```powershell
   Get-Content .\pessoa-b.pub | ssh projetoiot-vm "tr -d '\r' >> ~/.ssh/authorized_keys"
   ```

   O `tr -d '\r'` remove o fim de linha do Windows, que estragaria a linha da chave.

Para revogar um acesso, apague a linha da chave em `~/.ssh/authorized_keys`.

Se a Pessoa B também precisar do console da AWS, convide-a como membro do projeto em https://settings.aws.com ([AWS](https://docs.aws.amazon.com/accounts/latest/reference/invite-team-members.html)). Não compartilhem o login de quem criou a conta.

## Se perder o acesso

Use o **EC2 Instance Connect**: abra a instância no console → **Conectar → EC2 Instance Connect**. Ele abre um terminal no navegador com uma chave temporária, então funciona mesmo com o SSH só por chave. De lá, dá para corrigir o `~/.ssh/authorized_keys`.

Se a VM for recriada com o mesmo IP, o ssh vai acusar que a impressão digital mudou. Apague a entrada antiga com `ssh-keygen -R <IP_ELASTICO>`.

## Custos e prazo

- O plano Free não gera cobrança. A conta recebe US$ 100 de crédito, mais até US$ 100 completando atividades ([AWS](https://docs.aws.amazon.com/awsaccountbilling/latest/aboutv2/free-tier-plans.html)).
- A VM ligada o tempo todo gasta cerca de US$ 20 de crédito por mês: US$ 15,18 da `t3.small` (US$ 0,0208/h, [DoiT](https://www.doit.com/compute/spot/us-east-2/t3.small)), US$ 3,65 do IP (US$ 0,005/h, [AWS](https://aws.amazon.com/vpc/pricing/)) e US$ 1,60 dos 20 GiB de gp3 (US$ 0,08/GB-mês, [aws-pricing.com](https://aws-pricing.com/us-east-2.html)). Os US$ 100 iniciais duram uns 5 meses.
- O plano termina 6 meses depois da criação da conta ou quando o crédito acaba. Aí a conta fecha e a VM se perde. Acompanhem o saldo no console de Billing and Cost Management e façam o backup da T27 antes disso.
- O IP elástico é cobrado mesmo sem VM associada. Se apagarem a VM, liberem o IP também.

## Evidências

Preencher depois de criar a VM. Não deixem o IP nem o número da conta AWS aparecerem nos prints.

- [ ] Saída do `verificar-vm.ps1` terminando com `Tudo certo` (colar abaixo).
- [ ] Print das regras de entrada do grupo de segurança `projetoiot-vm-sg`.
- Conta AWS criada em: __/__/____. O plano Free termina em: __/__/____.

```text
(colar aqui a saída do verificar-vm.ps1)
```

## Próximos passos

- **T06**: instalar o Docker e publicar no `docker-compose.yml` só a 443 (Caddy) e a 8883 (Mosquitto).
- **T08**: apontar o domínio (por exemplo, um subdomínio gratuito do DuckDNS) para o IP elástico.
