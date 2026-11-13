/**
 * Configuração personalizada do Node-RED para o ProjetoIOT (T10)
 * Controla autenticação do editor (adminAuth), endpoints HTTP (httpNodeAuth)
 * e criptografia de credenciais (credentialSecret).
 */
module.exports = {
    // Porta e escuta
    uiPort: process.env.PORT || 1880,
    uiHost: "0.0.0.0",

    // Chave secreta para criptografia de credenciais (flows_cred.json)
    // Impede o uso de chave aleatória no primeiro boot garantindo reprodutibilidade
    credentialSecret: process.env.NODE_RED_CREDENTIAL_SECRET || "projetoiot-super-secret-key-change-me",

    // Autenticação do Editor (adminAuth)
    adminAuth: {
        type: "credentials",
        users: [
            {
                username: process.env.NODE_RED_ADMIN_USER || "admin",
                // Hash bcrypt gerado para a senha padrão de fallback: 'adminpassword123'
                password: process.env.NODE_RED_ADMIN_PASSWORD_HASH || "$2b$08$0P/7fEGBU3W2xq52D6BtPe/9xxBQOp4lH22/xIbDBv3jOfT3EsURS",
                permissions: "*"
            }
        ]
    },

    // Autenticação básica para nós HTTP In / Dashboard (httpNodeAuth)
    // Protege as telas de dashboard operacional e endpoints REST contra acesso anônimo
    httpNodeAuth: {
        user: process.env.NODE_RED_HTTP_NODE_USER || "viewer",
        // Hash bcrypt gerado para a senha padrão de fallback: 'adminpassword123'
        pass: process.env.NODE_RED_HTTP_NODE_PASSWORD_HASH || "$2b$08$0P/7fEGBU3W2xq52D6BtPe/9xxBQOp4lH22/xIbDBv3jOfT3EsURS"
    },

    // Configurações de logging
    logging: {
        console: {
            level: "info",
            metrics: false,
            audit: true
        }
    },

    // Editor e contexto
    editorTheme: {
        projects: {
            enabled: false // Será habilitado na T26
        }
    }
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
