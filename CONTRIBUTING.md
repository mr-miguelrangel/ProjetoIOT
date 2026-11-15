# Como contribuir

Toda alteração entra na `main` do repositório [mr-miguelrangel/ProjetoIOT](https://github.com/mr-miguelrangel/ProjetoIOT) por **pull request (PR)**. Ninguém faz commit direto na `main`.

O fluxo é sempre o mesmo:

1. criar uma branch a partir da `main` atualizada;
2. fazer **uma** alteração pequena nessa branch;
3. abrir um PR para a `main`;
4. revisar, mergear e apagar a branch.

Uma branch = um assunto. Se durante o trabalho aparecer outra coisa para mudar, ela vai em outra branch e em outro PR.

Links úteis:

| O quê | Endereço |
|---|---|
| Repositório | <https://github.com/mr-miguelrangel/ProjetoIOT> |
| Pull requests abertos | <https://github.com/mr-miguelrangel/ProjetoIOT/pulls> |
| Issues | <https://github.com/mr-miguelrangel/ProjetoIOT/issues> |
| Dashboard (local) | <http://localhost:1880/dashboard> |
| Editor do Node-RED (local) | <http://localhost:1880> |
| CSV do histórico (local) | <http://localhost:1880/historico.csv> |
| Broker MQTT (local) | `localhost:1883` (para o mestre: `<ip-do-computador>:1883`) |

## 1. Preparar o ambiente (só na primeira vez)

```sh
git clone https://github.com/mr-miguelrangel/ProjetoIOT.git
cd ProjetoIOT
```

O que cada parte do projeto precisa:

| Parte | Pasta | Ferramentas |
|---|---|---|
| Nuvem (broker + Node-RED) | `docker-compose.yml`, `mosquitto/`, `nodered/` | Docker com Docker Compose |
| Simulador do mestre | `simulador/` | Python 3 e `pip install -r simulador/requirements.txt` |
| Firmware do nó sensor | `Sensor_HeltecV3/` | Arduino IDE com as bibliotecas listadas no topo de `Sensor_HeltecV3.ino` |

Para subir a nuvem, veja a seção [Como rodar](README.md#como-rodar) do README.

## 2. Criar a branch

Sempre comece da `main` atualizada:

```sh
git switch main
git pull
git switch -c <tipo>/<descricao-curta>
```

### Nome da branch

Formato: `<tipo>/<descricao-curta>`, em minúsculas, com palavras separadas por hífen.

| Tipo | Quando usar | Exemplo |
|---|---|---|
| `feat/` | nova funcionalidade | `feat/alerta-umidade-alta`, `feat/historico-filtro-30-dias` |
| `fix/` | correção de erro | `fix/grafico-sem-no3`, `fix/simulador-reconecta-broker` |
| `docs/` | só documentação (README, prints, este guia) | `docs/contrato-mqtt-rssi`, `docs/evidencia-mestre-real` |
| `chore/` | configuração, dependências, Docker, `.gitignore` | `chore/atualiza-node-red-dashboard` |

Mudanças no firmware usam os mesmos tipos, com o assunto no nome: `feat/heltec-envia-bateria`, `fix/heltec-intervalo-envio`.

## 3. Fazer a alteração

Mantenha a alteração **pequena**: o PR deve poder ser revisado em poucos minutos. Exemplos de PRs do tamanho certo para este projeto:

- "Adiciona o filtro de 30 dias na página Histórico" (só o flow);
- "Inclui o campo `bateria` no contrato MQTT" (README + simulador + flow, porque o contrato mudou);
- "Corrige o aviso de nó sem dados que não aparecia para o no3" (só o flow);
- "Reduz o intervalo de envio do Heltec para 10 s" (só `Sensor_HeltecV3.ino`).

Um PR como "faz o banco de dados inteiro" deve ser quebrado em passos: criar a tabela, depois a página Histórico, depois o botão Exportar CSV.

### Alterações no flow do Node-RED

O `nodered/flows.json` é gerado pelo editor, então não o edite à mão:

1. suba os serviços: `docker compose up -d --build`;
2. abra o editor em <http://localhost:1880>, faça a mudança e clique em **Deploy**;
3. o Node-RED grava a mudança em `nodered/flows.json`, que é o arquivo a commitar.

Antes de commitar, confira com `git diff nodered/flows.json` se só mudou o que você pretendia. Mover nós de lugar no editor também altera o arquivo (posições `x`/`y`); evite arrastar nós sem necessidade.

### Alterações no contrato MQTT

O contrato (tópicos `idc/sensores/<id>/leituras` e `idc/mestre/status` e os campos do JSON) está na seção [Contrato MQTT](README.md#contrato-mqtt) do README. Se você mudar um tópico ou um campo, atualize no mesmo PR:

- o `README.md`;
- o `simulador/simular_mestre.py`, para que ele continue publicando no formato novo;
- o flow em `nodered/flows.json`, se ele usar o campo.

### Alterações no firmware (`Sensor_HeltecV3/`)

Os parâmetros de rádio (`LORA_FREQ`, `LORA_BW`, `LORA_SF`) precisam ser iguais no nó sensor e no mestre. Se mudar algum deles, diga no PR qual valor o mestre deve usar.

### Testar

Suba a stack e gere leituras com o simulador:

```sh
docker compose up -d --build
python simulador/simular_mestre.py
```

Depois confira:

- <http://localhost:1880/dashboard>: mestre 🟢 online e os nós `no1`, `no2` e `no3` atualizando;
- ☰ → **Histórico**: filtros, gráficos, tabela e **Exportar CSV**, se a mudança mexe no banco.

Para testar um caso específico sem o simulador, publique uma mensagem pelo cliente do container:

```sh
docker compose exec mosquitto mosquitto_pub -h localhost -t idc/sensores/no3/leituras -m '{"temperatura": 27.0, "umidade": 58.5, "rssi": -92}'
```

Para ver tudo o que chega ao broker:

```sh
docker compose exec mosquitto mosquitto_sub -h localhost -t 'idc/#' -v
```

Se a mudança afeta o contrato MQTT, o banco ou o modo de rodar, atualize o `README.md` no mesmo PR.

## 4. Commitar

```sh
git status
git add nodered/flows.json README.md
git commit -m "Adiciona filtro de 30 dias na página Histórico"
```

### Mensagem de commit

- Em português, começando com um verbo no presente: **Adiciona**, **Corrige**, **Remove**, **Documenta**, **Atualiza**…
- Uma linha curta (até ~72 caracteres) dizendo **o que** muda. Se precisar explicar o **porquê**, deixe uma linha em branco e escreva no corpo.
- Um commit por passo lógico. Uma branch pode ter mais de um commit, mas todos sobre o mesmo assunto.

Exemplos:

```text
Corrige o aviso de nó sem dados para o no3
Adiciona o campo bateria no simulador do mestre
Documenta o RSSI no contrato MQTT
Atualiza o @flowfuse/node-red-dashboard no Dockerfile
```

Com corpo explicando o porquê:

```sh
git commit -m "Reduz o intervalo de envio do Heltec para 10 s" -m "Com 30 s o gráfico de 30 min tinha poucos pontos por nó."
```

Não commite arquivos gerados ou locais: o `nodered/historico.db` e os demais arquivos que o Node-RED cria em `nodered/` (`node_modules/`, `.npm/`, `.config.*.json`) já estão no `.gitignore`. Se o `git status` mostrar algum deles, não o adicione.

## 5. Enviar a branch e abrir o pull request

```sh
git push -u origin feat/historico-filtro-30-dias
```

O `git push` mostra um link para abrir o PR. Também dá para abrir direto pelo endereço (troque pelo nome da sua branch):

```text
https://github.com/mr-miguelrangel/ProjetoIOT/compare/main...feat/historico-filtro-30-dias
```

Confira que está com:

- **base:** `main`
- **compare:** a sua branch

Com o [GitHub CLI](https://cli.github.com/) instalado e logado (`gh auth login`), dá para fazer pelo terminal:

```sh
gh pr create --base main --fill
```

Ou com título e descrição de um arquivo:

```sh
gh pr create --base main --title "Adiciona filtro de 30 dias na página Histórico" --body-file pr.md
```

### Descrição do PR

```markdown
## O que muda
Adiciona a opção "últimos 30 dias" no filtro de período da página Histórico.
Nos gráficos, cada ponto passa a ser a média de 6 h nesse período.

## Como testar
1. `docker compose up -d --build`
2. `python simulador/simular_mestre.py` por alguns minutos
3. Abrir http://localhost:1880/dashboard, ir em ☰ → Histórico e escolher "últimos 30 dias"
4. Clicar em Exportar CSV e conferir que o arquivo traz as leituras do período

## Evidência
![histórico com filtro de 30 dias](docs/evidencias/historico-30-dias.png)
```

Se a mudança for visual, salve o print em `docs/evidencias/` com um nome que diga o que ele mostra (como os que já estão lá: `dashboard.png`, `historico-csv.png`) e use-o no PR e, se fizer sentido, no README.

Para um exemplo de descrição de uma mudança maior, veja o [PR #43](https://github.com/mr-miguelrangel/ProjetoIOT/pull/43).

## 6. Revisão

- Peça a revisão de pelo menos um colega no campo **Reviewers** do PR.
- Ajustes pedidos na revisão são feitos **na mesma branch**: basta commitar e dar `git push` de novo que o PR se atualiza sozinho.
- Se a `main` andou enquanto o PR estava aberto, atualize sua branch:

  ```sh
  git switch main
  git pull
  git switch feat/historico-filtro-30-dias
  git merge main
  ```

  Em caso de conflito no `nodered/flows.json`, prefira refazer a sua mudança no editor do Node-RED sobre a versão nova da `main` em vez de resolver o JSON à mão.

## 7. Merge e limpeza

Com o PR aprovado, clique em **Merge pull request** no GitHub e depois em **Delete branch**.

Depois, no seu computador:

```sh
git switch main
git pull
git branch -d feat/historico-filtro-30-dias
```

Pronto: a `main` local está atualizada e você pode começar a próxima contribuição pelo passo 2.

## Resumo rápido

```sh
git switch main && git pull
git switch -c feat/historico-filtro-30-dias
docker compose up -d --build
# ... altera no editor (http://localhost:1880), clica em Deploy ...
python simulador/simular_mestre.py   # testa no http://localhost:1880/dashboard
git diff nodered/flows.json
git add nodered/flows.json
git commit -m "Adiciona filtro de 30 dias na página Histórico"
git push -u origin feat/historico-filtro-30-dias
gh pr create --base main --fill      # ou pelo link que o push mostra
# revisão -> merge -> Delete branch no GitHub
git switch main && git pull && git branch -d feat/historico-filtro-30-dias
```
