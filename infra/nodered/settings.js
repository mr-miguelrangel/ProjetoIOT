// Configuração do Node-RED do ProjetoIOT (T06 e T10).
// O docker-compose.yml monta este arquivo em /data/settings.js (somente leitura).
// Opções que não aparecem aqui ficam com o valor padrão do Node-RED.
// Decisões na docs/adr/0010-nodered-auth-security.md.
//
// O Node-RED fica atrás do Caddy na 443, aberta para a internet. Por isso:
//   - o editor e a API de administração exigem login (adminAuth);
//   - os nós "http in" e o dashboard exigem outro login, só de leitura (httpNodeAuth);
//   - as credenciais salvas nos fluxos são cifradas com uma chave fixa (credentialSecret).
// Usuários, senhas e a chave vêm do .env (ver .env.example). Não há valor padrão para senha.

const crypto = require("crypto");

function obrigatoria(nome) {
    const valor = process.env[nome];
    if (!valor) {
        throw new Error(`Variável de ambiente ${nome} não definida. Veja o .env.example.`);
    }
    return valor;
}

function senhaObrigatoria(nome) {
    const valor = obrigatoria(nome);
    if (valor.length < 12) {
        throw new Error(`${nome} precisa ter pelo menos 12 caracteres.`);
    }
    return valor;
}

// Compara em tempo constante. Os dois lados passam pelo SHA-256 para terem o mesmo tamanho,
// que é exigido pelo timingSafeEqual.
function iguais(a, b) {
    const ha = crypto.createHash("sha256").update(String(a)).digest();
    const hb = crypto.createHash("sha256").update(String(b)).digest();
    return crypto.timingSafeEqual(ha, hb);
}

const ADMIN_USER = obrigatoria("NODE_RED_ADMIN_USER");
const ADMIN_PASSWORD = senhaObrigatoria("NODE_RED_ADMIN_PASSWORD");
const HTTP_NODE_USER = obrigatoria("NODE_RED_HTTP_NODE_USER");
const HTTP_NODE_PASSWORD = senhaObrigatoria("NODE_RED_HTTP_NODE_PASSWORD");

if (iguais(HTTP_NODE_PASSWORD, ADMIN_PASSWORD)) {
    throw new Error("NODE_RED_HTTP_NODE_PASSWORD precisa ser diferente de NODE_RED_ADMIN_PASSWORD.");
}

const admin = { username: ADMIN_USER, permissions: "*" };

// Autenticação básica (HTTP Basic) para os nós "http in" e o dashboard.
// O Node-RED aplica esta função na httpNodeRoot, depois das rotas do editor, então ela
// não afeta o login do editor. Requisições OPTIONS passam, como no httpNodeAuth padrão.
function autenticacaoBasica(req, res, next) {
    if (req.method === "OPTIONS") {
        return next();
    }
    const [tipo, valor] = (req.headers.authorization || "").split(" ");
    if (tipo === "Basic" && valor) {
        const texto = Buffer.from(valor, "base64").toString("utf8");
        const separador = texto.indexOf(":");
        if (separador >= 0) {
            const usuarioOk = iguais(texto.slice(0, separador), HTTP_NODE_USER);
            const senhaOk = iguais(texto.slice(separador + 1), HTTP_NODE_PASSWORD);
            if (usuarioOk && senhaOk) {
                return next();
            }
        }
    }
    res.set("WWW-Authenticate", 'Basic realm="ProjetoIOT", charset="UTF-8"');
    return res.sendStatus(401);
}

module.exports = {
    uiPort: 1880,
    flowFile: "flows.json",
    flowFilePretty: true,

    // Chave que cifra o flows_cred.json (senha do MQTT, token do InfluxDB, token do Telegram).
    // Se for trocada depois, as credenciais já salvas nos fluxos deixam de abrir.
    credentialSecret: obrigatoria("NODE_RED_CREDENTIAL_SECRET"),

    // Login do editor e da API de administração. Sem usuário padrão: quem não entra não vê nada.
    adminAuth: {
        type: "credentials",
        users: (username) => Promise.resolve(username === ADMIN_USER ? admin : null),
        authenticate: (username, password) => {
            const usuarioOk = iguais(username, ADMIN_USER);
            const senhaOk = iguais(password, ADMIN_PASSWORD);
            return Promise.resolve(usuarioOk && senhaOk ? admin : null);
        },
    },

    // Login dos nós "http in" e do dashboard.
    httpNodeAuth: autenticacaoBasica,

    logging: {
        // audit registra cada chamada à API de administração (login, deploy), útil para rastrear acessos.
        console: { level: "info", metrics: false, audit: true },
    },

    editorTheme: {
        projects: { enabled: false },
    },
};
