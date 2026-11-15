# Como contribuir

Toda alteração entra na `main` por **pull request (PR)**. Ninguém faz commit direto na `main`.

O fluxo é sempre o mesmo:

1. criar uma branch a partir da `main` atualizada;
2. fazer **uma** alteração pequena nessa branch;
3. abrir um PR para a `main`;
4. revisar, mergear e apagar a branch.

Uma branch = um assunto. Se durante o trabalho aparecer outra coisa para mudar, ela vai em outra branch e em outro PR.

## 1. Preparar o ambiente (só na primeira vez)

```sh
git clone <url-do-repositorio>
cd projeto-final
```

Para testar as alterações é preciso Docker (com Docker Compose). Veja a seção [Como rodar](README.md#como-rodar) do README.

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
| `feat/` | nova funcionalidade | `feat/alerta-umidade-alta` |
| `fix/` | correção de erro | `fix/grafico-sem-no3` |
| `docs/` | só documentação (README, prints, este guia) | `docs/contrato-mqtt` |
| `chore/` | configuração, dependências, Docker, `.gitignore` | `chore/atualiza-node-red` |

## 3. Fazer a alteração

Mantenha a alteração **pequena**: o PR deve poder ser revisado em poucos minutos. Bons exemplos do histórico do projeto:

- "Cria a tabela leituras e grava cada leitura válida no SQLite"
- "Adiciona a página Histórico com dados consultados no banco"
- "Adiciona botão Exportar CSV na página Histórico"

Cada um desses foi um passo isolado, e não "faz o banco de dados inteiro".

### Alterações no flow do Node-RED

O `nodered/flows.json` é gerado pelo editor, então não o edite à mão:

1. suba os serviços: `docker compose up -d --build`;
2. abra o editor em <http://localhost:1880>, faça a mudança e clique em **Deploy**;
3. o Node-RED grava a mudança em `nodered/flows.json`, que é o arquivo a commitar.

Antes de commitar, confira com `git diff` se só mudou o que você pretendia. Mover nós de lugar no editor também altera o arquivo (posições `x`/`y`); evite arrastar nós sem necessidade.

### Testar

- Use o simulador (`simulador/simular_mestre.py`) para gerar leituras sem o hardware.
- Confira o dashboard em <http://localhost:1880/dashboard>.
- Se a mudança afeta o contrato MQTT, o banco ou o modo de rodar, atualize o `README.md` no mesmo PR.

## 4. Commitar

```sh
git status
git add <arquivos>
git commit -m "Adiciona filtro por nó na página Histórico"
```

### Mensagem de commit

- Em português, começando com um verbo no presente: **Adiciona**, **Corrige**, **Remove**, **Documenta**, **Atualiza**…
- Uma linha curta (até ~72 caracteres) dizendo **o que** muda. Se precisar explicar o **porquê**, deixe uma linha em branco e escreva no corpo.
- Um commit por passo lógico. Uma branch pode ter mais de um commit, mas todos sobre o mesmo assunto.

Não commite arquivos gerados ou locais: o banco SQLite e os demais arquivos que o Node-RED cria em `nodered/` já estão no `.gitignore`.

## 5. Enviar a branch e abrir o pull request

```sh
git push -u origin <tipo>/<descricao-curta>
```

No GitHub, clique em **Compare & pull request** (ou vá em **Pull requests → New pull request**) com:

- **base:** `main`
- **compare:** a sua branch

Se tiver o [GitHub CLI](https://cli.github.com/), dá para fazer pelo terminal:

```sh
gh pr create --base main --fill
```

### Descrição do PR

```markdown
## O que muda
Adiciona um filtro por nó (no1, no2, no3) na página Histórico.

## Como testar
1. `docker compose up -d --build`
2. Rodar o simulador
3. Abrir Histórico e trocar o filtro

## Evidência
<print do dashboard, se a mudança for visual>
```

## 6. Revisão

- Peça a revisão de pelo menos um colega.
- Ajustes pedidos na revisão são feitos **na mesma branch**: basta commitar e dar `git push` de novo que o PR se atualiza sozinho.
- Se a `main` andou enquanto o PR estava aberto, atualize sua branch:

  ```sh
  git switch main
  git pull
  git switch <sua-branch>
  git merge main
  ```

  Em caso de conflito no `flows.json`, prefira refazer a sua mudança no editor do Node-RED sobre a versão nova da `main` em vez de resolver o JSON à mão.

## 7. Merge e limpeza

Com o PR aprovado, clique em **Merge pull request** no GitHub e depois em **Delete branch**.

Depois, no seu computador:

```sh
git switch main
git pull
git branch -d <tipo>/<descricao-curta>
```

Pronto: a `main` local está atualizada e você pode começar a próxima contribuição pelo passo 2.

## Resumo rápido

```sh
git switch main && git pull
git switch -c feat/minha-mudanca
# ... altera, testa ...
git add <arquivos>
git commit -m "Adiciona minha mudança"
git push -u origin feat/minha-mudanca
# abre o PR no GitHub -> revisão -> merge -> apaga a branch
git switch main && git pull && git branch -d feat/minha-mudanca
```
