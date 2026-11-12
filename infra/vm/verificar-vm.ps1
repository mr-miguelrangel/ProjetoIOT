<#
.SYNOPSIS
    Verifica o criterio de pronto da T05: SSH so por chave e firewall aberto apenas em 22, 443 e 8883.

.DESCRIPTION
    Roda no seu PC (Windows PowerShell 5.1 ou PowerShell 7) e testa a VM de fora:

      1. Portas TCP. A 22 precisa responder. A 443 e a 8883 precisam estar liberadas: ate a T06
         nao ha servico nelas, entao a VM recusa a conexao em vez de ignorar. A 80 e as portas
         internas da stack precisam estar bloqueadas (sem resposta).
      2. Login sem chave. O servidor precisa oferecer apenas o metodo "publickey".
      3. Configuracao dentro da VM, entrando com a sua chave: sshd sem senha e sem root, ufw ativo,
         negando entrada por padrao e liberando so 22, 443 e 8883.

    A saida nao mostra o IP da VM, entao pode ir para o relatorio ou para o PR.
    Detalhes em docs/vm.md.

.PARAMETER Endereco
    IP elastico (ou nome DNS) da VM.

.PARAMETER Chave
    Chave privada SSH. Padrao: ~/.ssh/projetoiot_vm

.PARAMETER Usuario
    Usuario da VM. Padrao: ubuntu

.PARAMETER OpcoesSshExtras
    Opcoes extras repassadas ao ssh, por exemplo @('-o', 'UserKnownHostsFile=C:\temp\known_hosts').

.EXAMPLE
    .\infra\vm\verificar-vm.ps1 -Endereco 203.0.113.10
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Endereco,

    [string]$Chave = (Join-Path $env:USERPROFILE '.ssh\projetoiot_vm'),

    [string]$Usuario = 'ubuntu',

    [string[]]$OpcoesSshExtras = @()
)

Set-StrictMode -Version 2.0
# Com 'Stop', o Windows PowerShell 5.1 transforma o stderr do ssh em erro fatal.
$ErrorActionPreference = 'Continue'

# Estados aceitos para cada porta. "recusada": o firewall deixou passar, mas nada ouve na porta.
# "bloqueada": o pacote foi descartado e nao houve resposta.
$portas = @(
    @{ Porta = 22;   Aceitos = @('aberta');             Uso = 'SSH' }
    @{ Porta = 443;  Aceitos = @('aberta', 'recusada'); Uso = 'HTTPS do Caddy (T08)' }
    @{ Porta = 8883; Aceitos = @('aberta', 'recusada'); Uso = 'MQTT com TLS (T09)' }
    @{ Porta = 80;   Aceitos = @('bloqueada');          Uso = 'HTTP fica fechado' }
    @{ Porta = 1880; Aceitos = @('bloqueada');          Uso = 'Node-RED, so rede interna' }
    @{ Porta = 1883; Aceitos = @('bloqueada');          Uso = 'MQTT sem TLS, so rede interna' }
    @{ Porta = 3000; Aceitos = @('bloqueada');          Uso = 'Grafana, so rede interna' }
    @{ Porta = 8086; Aceitos = @('bloqueada');          Uso = 'InfluxDB, so rede interna' }
)

# Valores esperados na saida de "sshd -T" (ver infra/vm/cloud-init.yaml).
$sshdEsperado = [ordered]@{
    'passwordauthentication'       = 'no'
    'kbdinteractiveauthentication' = 'no'
    'permitrootlogin'              = 'no'
    'pubkeyauthentication'         = 'yes'
    'authenticationmethods'        = 'publickey'
}

$resultados = New-Object System.Collections.Generic.List[object]

function Add-Resultado {
    param([string]$Item, [bool]$Ok, [string]$Detalhe)
    $situacao = 'FALHOU'
    if ($Ok) { $situacao = 'OK' }
    $resultados.Add([pscustomobject]@{ Resultado = $situacao; Item = $Item; Detalhe = $Detalhe })
}

function Test-PortaTcp {
    # Retorna 'aberta' (conectou), 'recusada' (a VM respondeu que nao ha servico na porta)
    # ou 'bloqueada' (nenhuma resposta dentro do prazo). O Windows repete a tentativa quando a
    # conexao e recusada e leva uns 2 s para desistir, por isso o prazo e de 5 s.
    param([string]$Destino, [int]$Porta, [int]$PrazoMs = 5000)
    $cliente = New-Object System.Net.Sockets.TcpClient
    try {
        $tarefa = $cliente.ConnectAsync($Destino, $Porta)
        if ($tarefa.Wait($PrazoMs)) { return 'aberta' }
        return 'bloqueada'
    }
    catch {
        $erro = $_.Exception
        while ($null -ne $erro -and -not ($erro -is [System.Net.Sockets.SocketException])) {
            $erro = $erro.InnerException
        }
        if ($null -eq $erro) { return "erro: $($_.Exception.Message)" }
        if ($erro.SocketErrorCode -eq [System.Net.Sockets.SocketError]::ConnectionRefused) { return 'recusada' }
        if ($erro.SocketErrorCode -eq [System.Net.Sockets.SocketError]::TimedOut) { return 'bloqueada' }
        return "erro: $($erro.SocketErrorCode)"
    }
    finally {
        $cliente.Close()
    }
}

function Get-MetodosSemChave {
    # Tenta entrar sem chave e sem interacao. O ssh falha e mostra os metodos que o servidor aceita,
    # por exemplo "Permission denied (publickey)." ou "Permission denied (publickey,password).".
    param([string]$Destino, [string[]]$OpcoesSsh = @())
    $opcoes = @(
        '-o', 'BatchMode=yes',
        '-o', 'PubkeyAuthentication=no',
        '-o', 'PreferredAuthentications=password,keyboard-interactive',
        '-o', 'ConnectTimeout=10'
    ) + $OpcoesSsh
    $saida = (& ssh @opcoes $Destino 'exit' 2>&1 | ForEach-Object { "$_" }) -join ' '
    $metodos = $null
    if ($saida -match 'Permission denied \(([^)]*)\)') { $metodos = $Matches[1] }
    return [pscustomobject]@{ Metodos = $metodos; Saida = $saida.Trim() }
}

function Test-SaidaDoServidor {
    # Confere a saida de "sshd -T" (filtrada) e de "ufw status verbose" vinda da VM.
    param([string[]]$Linhas = @())

    foreach ($opcao in $sshdEsperado.Keys) {
        $esperado = $sshdEsperado[$opcao]
        $linha = @($Linhas | Where-Object { $_ -match "^$opcao\s" }) | Select-Object -First 1
        $valor = '(ausente)'
        if ($linha) {
            $partes = $linha -split '\s+', 2
            if ($partes.Count -ge 2) { $valor = $partes[1].Trim() }
        }
        Add-Resultado "sshd: $opcao" ($valor -eq $esperado) "valor: $valor; esperado: $esperado"
    }

    $linhaStatus = @($Linhas | Where-Object { $_ -match '^Status:' }) | Select-Object -First 1
    $ativo = [bool]($linhaStatus -and $linhaStatus -match '^Status:\s+active')
    Add-Resultado 'ufw: ativo' $ativo "$linhaStatus"

    $linhaPadrao = @($Linhas | Where-Object { $_ -match '^Default:' }) | Select-Object -First 1
    $negaEntrada = [bool]($linhaPadrao -and $linhaPadrao -match 'deny \(incoming\)')
    Add-Resultado 'ufw: entrada negada por padrao' $negaEntrada "$linhaPadrao"

    $liberadas = @($Linhas |
        Where-Object { $_ -match '\sALLOW IN\s' } |
        ForEach-Object { ($_ -split '\s+')[0] } |
        Sort-Object -Unique)
    $textoLiberadas = $liberadas -join ', '
    if (-not $textoLiberadas) { $textoLiberadas = '(nenhuma)' }
    Add-Resultado 'ufw: so 22, 443 e 8883 liberadas' ($textoLiberadas -eq '22/tcp, 443/tcp, 8883/tcp') "liberadas: $textoLiberadas"
}

if (-not (Get-Command ssh -ErrorAction SilentlyContinue)) {
    Write-Error 'Cliente ssh nao encontrado. Instale o recurso opcional "Cliente OpenSSH" do Windows.'
    exit 2
}

$destino = "$Usuario@$Endereco"
$opcoesComuns = @('-o', 'StrictHostKeyChecking=accept-new') + $OpcoesSshExtras
$linhasServidor = @()

Write-Output 'Verificacao da T05: SSH so por chave e firewall aberto apenas em 22, 443 e 8883'
Write-Output ('Data: {0:yyyy-MM-dd HH:mm}' -f (Get-Date))

# 1. Portas, vistas de fora (efeito do grupo de seguranca e do ufw juntos)
foreach ($p in $portas) {
    Write-Host ("Testando a porta {0}..." -f $p.Porta)
    $estado = Test-PortaTcp -Destino $Endereco -Porta $p.Porta
    $detalhe = '{0}; esperado: {1}; uso: {2}' -f $estado, ($p.Aceitos -join ' ou '), $p.Uso
    Add-Resultado ('porta {0}' -f $p.Porta) ($p.Aceitos -contains $estado) $detalhe
}

# 2. Login sem chave
Write-Host 'Testando login sem chave...'
$semChave = Get-MetodosSemChave -Destino $destino -OpcoesSsh $opcoesComuns
if ($semChave.Metodos) {
    $somenteChave = (@($semChave.Metodos -split ',' | Where-Object { $_ -ne 'publickey' }).Count -eq 0)
    Add-Resultado 'login sem chave recusado' $somenteChave ('metodos oferecidos: {0}; esperado: publickey' -f $semChave.Metodos)
}
else {
    Add-Resultado 'login sem chave recusado' $false ('resposta inesperada do ssh: {0}' -f $semChave.Saida)
}

# 3. Configuracao dentro da VM
if (-not (Test-Path -LiteralPath $Chave)) {
    Add-Resultado 'login com a chave' $false "chave nao encontrada: $Chave"
}
else {
    Write-Host 'Entrando na VM com a chave (o ssh pede a passphrase, se ela nao estiver no ssh-agent)...'
    $comandoRemoto = "sudo sshd -T | grep -Ei '^(passwordauthentication|kbdinteractiveauthentication|permitrootlogin|pubkeyauthentication|authenticationmethods) '; sudo ufw status verbose"
    $opcoesChave = @('-i', $Chave, '-o', 'IdentitiesOnly=yes', '-o', 'ConnectTimeout=10') + $opcoesComuns
    # So o stdout e capturado; o stderr (pedido de passphrase e avisos) continua aparecendo na tela.
    $linhasServidor = @(& ssh @opcoesChave $destino $comandoRemoto)
    $codigo = $LASTEXITCODE
    if ($codigo -eq 255) {
        Add-Resultado 'login com a chave' $false 'o ssh nao conseguiu entrar (codigo 255); veja a mensagem acima'
    }
    else {
        $detalhe = ''
        if ($codigo -ne 0) { $detalhe = "entrou, mas o comando remoto terminou com codigo $codigo" }
        Add-Resultado 'login com a chave' $true $detalhe
        Test-SaidaDoServidor -Linhas $linhasServidor
    }
}

Write-Output ''
Write-Output (($resultados | Format-Table -AutoSize -Wrap | Out-String -Width 160).TrimEnd())

if ($linhasServidor.Count -gt 0) {
    Write-Output ''
    Write-Output 'Saida do servidor (sshd -T filtrado e ufw status verbose):'
    $linhasServidor | ForEach-Object { Write-Output "  $_" }
}

$falhas = @($resultados | Where-Object { $_.Resultado -ne 'OK' }).Count
Write-Output ''
if ($falhas -gt 0) {
    Write-Output "$falhas verificacao(oes) falharam. Veja a coluna Detalhe e a secao Verificacao de docs/vm.md."
    exit 1
}
Write-Output 'Tudo certo: SSH so por chave e firewall aberto apenas em 22, 443 e 8883.'
exit 0
