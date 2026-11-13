// Configuração do Node-RED do ProjetoIOT.
// O docker-compose.yml monta este arquivo em /data/settings.js (somente leitura).
// Opções que não aparecem aqui ficam com o valor padrão do Node-RED.
//
// O editor fica atrás do Caddy na 443, aberta para a internet, então ele exige login.
// Usuário, senha e segredo das credenciais vêm do .env (ver .env.example).

const crypto = require("crypto");

function obrigatoria(nome) {
    const valor = process.env[nome];
    if (!valor) {
        throw new Error(`Variável de ambiente ${nome} não definida. Veja o .env.example.`);
    }
    return valor;
}

const ADMIN_USER = obrigatoria("NODE_RED_ADMIN_USER");
const ADMIN_PASSWORD = obrigatoria("NODE_RED_ADMIN_PASSWORD");

if (ADMIN_PASSWORD.length < 12) {
    throw new Error("NODE_RED_ADMIN_PASSWORD precisa ter pelo menos 12 caracteres.");
}

// Compara em tempo constante. Os dois lados passam pelo SHA-256 para terem o mesmo tamanho,
// que é exigido pelo timingSafeEqual.
function iguais(a, b) {
    const ha = crypto.createHash("sha256").update(String(a)).digest();
    const hb = crypto.createHash("sha256").update(String(b)).digest();
    return crypto.timingSafeEqual(ha, hb);
}

const admin = { username: ADMIN_USER, permissions: "*" };

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

    logging: {
        console: { level: "info", metrics: false, audit: false },
    },
};
