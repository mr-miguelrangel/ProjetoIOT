# ADR 0006: VM na AWS EC2 (plano Free) com SSH só por chave

## Status
Aceito. Decidido durante a T05 e revisado na mesma tarefa, antes de criar a VM (ver "Histórico").

## Contexto
A stack da etapa de nuvem (Mosquitto, Node-RED, InfluxDB, Grafana e Caddy em Docker, ver [arquitetura.md](../arquitetura.md)) precisa de uma VM com:

- cerca de 2 GB de RAM para os cinco containers;
- IP público fixo, para o domínio da T08 e para o Master conectar na 8883;
- entrada aberta só em 22, 443 e 8883, que é o critério da T05;
- custo zero para a dupla, pelo menos até a apresentação (T32).

Alternativas avaliadas (preços e limites consultados em novembro de 2026):

| Alternativa | Por que foi descartada ou escolhida |
|---|---|
| Oracle Cloud Always Free (A1.Flex, ARM) | Grátis sem prazo, com até 2 OCPU e 12 GB. Descartada porque a criação costuma falhar por falta de capacidade e porque a Oracle pode recolher VMs com CPU, rede e memória abaixo de 20% por 7 dias ([Always Free Resources](https://docs.oracle.com/en-us/iaas/Content/FreeTier/freetier_topic-Always_Free_Resources.htm)). Com pouca carga, a stack fica abaixo disso. Também pede cartão. |
| Google Cloud, `e2-micro` do Free Tier | Só 1 GB de RAM e só em regiões dos EUA. O IPv4 público é cobrado depois da primeira hora do mês, cerca de US$ 3,60 por mês ([preços de rede](https://cloud.google.com/vpc/network-pricing)). |
| Azure for Students | US$ 100 de crédito por 12 meses, sem cartão. Fica como alternativa: a B1s gratuita tem só 1 GB, e a B1ms (2 GB) consome o crédito. |
| **AWS EC2, plano Free** | **Escolhida.** O plano Free não gera cobrança, dá US$ 100 de crédito (mais até US$ 100 em atividades) e dura 6 meses ou até o crédito acabar ([planos da AWS](https://docs.aws.amazon.com/awsaccountbilling/latest/aboutv2/free-tier-plans.html)). A `t3.small` é qualificada para o plano nas contas criadas a partir de 15/07/2025 ([EC2 Free Tier](https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/ec2-free-tier-usage.html)). |

A conta foi criada pelo cadastro novo da AWS (login do Google, do GitHub ou AWS Builder ID). Nesse cadastro, a região é fixada pelo país do endereço de contato e não pode ser trocada. Para o Brasil, ela é US East (Ohio), `us-east-2` ([regiões dos projetos](https://docs.aws.amazon.com/accounts/latest/reference/project-regions.html)). A EC2 e o EBS estão incluídos no Free Tier desse cadastro ([serviços suportados](https://docs.aws.amazon.com/accounts/latest/reference/supported-services-sign-up-new.html)).

## Decisão
1. **VM**: AWS EC2 `t3.small` (2 vCPU, 2 GiB, x86_64) na região `us-east-2` (Ohio), com Ubuntu Server 24.04 LTS, disco gp3 de 20 GiB e swap de 2 GiB.
   - Ohio porque é a região fixa da conta. Ativar os recursos avançados da conta liberaria a escolha de São Paulo, mas passaria para a dupla a administração completa da organização AWS, sem garantia de continuar no plano Free.
   - x86_64 em vez de ARM (`t4g.small`), para não depender de toda imagem Docker e biblioteca da stack ter versão ARM.
   - Crédito de CPU `standard` em vez de `unlimited`, que é o padrão da T3. No `unlimited`, o uso de CPU acima da linha de base é cobrado à parte e consome o crédito ([burstable instances](https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/burstable-performance-instances-how-to.html)). No `standard`, a VM só fica mais lenta.
2. **IP elástico** associado à VM, para o endereço não mudar quando ela for parada e iniciada.
3. **SSH só por chave**: usuário `ubuntu`, uma chave ed25519 por pessoa, sem senha e sem root (`/etc/ssh/sshd_config.d/00-projetoiot.conf`).
4. **Firewall em duas camadas**, as duas com entrada só em 22, 443 e 8883:
   - grupo de segurança `projetoiot-vm-sg` na AWS, que também vale para os containers;
   - `ufw` na VM.
5. **Configuração versionada**: o que roda dentro da VM no primeiro boot está em [`infra/vm/cloud-init.yaml`](../../infra/vm/cloud-init.yaml). O passo a passo do console e a verificação estão em [docs/vm.md](../vm.md).

## Consequências
- **Positivas**:
  - Custo zero para a dupla enquanto durar o plano Free.
  - Em Ohio, a VM gasta cerca de US$ 20 de crédito por mês (detalhado em [docs/vm.md](../vm.md#custos-e-prazo)), então os US$ 100 iniciais duram uns 5 meses.
  - A VM pode ser recriada a partir do `cloud-init.yaml`, o que ajuda no teste de restauração da T27.
- **Negativas/Trade-offs**:
  - O plano Free acaba 6 meses depois da criação da conta ou quando o crédito termina. Nesse momento a conta fecha e a VM se perde.
  - A VM fica nos EUA. Pela distância, a ida e volta de cada pacote leva uns 130 ms a mais do que em São Paulo (estimativa). Isso não pesa na telemetria a cada 30 s nem nos dashboards.
  - O Docker cria regras de iptables próprias para as portas publicadas, e elas passam por cima do `ufw`. Na T06, o `docker-compose.yml` só pode publicar a 443 e a 8883, e o grupo de segurança é a camada que garante o resto.
  - Com a 80 fechada, o Caddy (T08) precisa validar o certificado pela 443 (desafio TLS-ALPN), e `http://` não redireciona para `https://`.
  - A 22 fica aberta para qualquer origem, porque o IP de casa muda e a dupla acessa de lugares diferentes. A proteção do SSH depende só da exigência de chave.

## Histórico
- **Versão 1 (T05)**: escolhia a região `sa-east-1` (São Paulo), com custo estimado de US$ 31 de crédito por mês.
- **Versão 2 (T05)**: troca para `us-east-2` (Ohio), antes de criar a VM. A conta foi criada pelo cadastro novo da AWS, que fixa a região pelo país e não oferece São Paulo.
