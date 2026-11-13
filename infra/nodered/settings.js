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
};
