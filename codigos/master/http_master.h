#pragma once
#include <stddef.h>

namespace HttpMaster {
enum class Resultado { Completa, Desconectada, Expirada, Invalida, Grande };

inline bool nomeIgual(const char* inicio, const char* fim, const char* nome) {
  while (inicio != fim && *nome) {
    char c = *inicio++;
    if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    if (c != *nome++) return false;
  }
  return inicio == fim && *nome == '\0';
}

// Executado exclusivamente na tarefa de rede. O prazo cobre toda a leitura.
template<class Cliente>
Resultado ler(Cliente& cliente, String& pedido, String& corpo, String& autorizacao) {
  constexpr unsigned long PRAZO_MS = 2000;
  constexpr size_t MAX_CABECALHO = 2048, MAX_LINHA = 512, MAX_CORPO = 1024;
  const unsigned long inicio = millis();
  size_t cabecalho = 0, tamanhoCorpo = 0;
  bool temTamanho = false, temAutorizacao = false, lendoCorpo = false;
  String linha;

  while (millis() - inicio < PRAZO_MS) {
    if (cliente.available() <= 0) {
      if (!cliente.connected()) return Resultado::Desconectada;
      delay(1);
      continue;
    }
    const int byteLido = cliente.read();
    if (byteLido < 0) {
      delay(1);
      continue;
    }
    const char c = (char)byteLido;
    if (c == '\0') return Resultado::Invalida;
    if (lendoCorpo) {
      corpo += c;
      if (corpo.length() == tamanhoCorpo) return Resultado::Completa;
      continue;
    }
    if (++cabecalho > MAX_CABECALHO) return Resultado::Grande;
    if (c != '\n') {
      if (linha.length() >= MAX_LINHA) return Resultado::Grande;
      linha += c;
      continue;
    }

    if (linha.length() && linha[linha.length() - 1] == '\r')
      linha = linha.substring(0, linha.length() - 1);
    if (!pedido.length()) {
      const bool post = linha.startsWith("POST ");
      if (!post && !linha.startsWith("GET ")) return Resultado::Invalida;
      const int inicioAlvo = post ? 5 : 4;
      const int fimAlvo = linha.indexOf(' ', inicioAlvo);
      if (fimAlvo <= inicioAlvo || linha[inicioAlvo] != '/') return Resultado::Invalida;
      const String versao = linha.substring(fimAlvo + 1);
      if (versao != "HTTP/1.0" && versao != "HTTP/1.1") return Resultado::Invalida;
      pedido = linha;
    } else if (!linha.length()) {
      if (pedido.startsWith("POST ") && !temTamanho) return Resultado::Invalida;
      if (tamanhoCorpo == 0) return Resultado::Completa;
      lendoCorpo = true;
    } else {
      const int separador = linha.indexOf(':');
      if (separador <= 0) return Resultado::Invalida;
      const char* nome = linha.c_str();
      for (const char* p = nome; p != nome + separador; ++p) {
        const unsigned char caractere = (unsigned char)*p;
        if (caractere <= ' ' || caractere >= 127) return Resultado::Invalida;
      }
      if (nomeIgual(nome, nome + separador, "transfer-encoding"))
        return Resultado::Invalida;
      if (nomeIgual(nome, nome + separador, "authorization")) {
        if (temAutorizacao) return Resultado::Invalida;
        temAutorizacao = true;
        int inicioValor = separador + 1;
        while (linha[inicioValor] == ' ' || linha[inicioValor] == '\t') ++inicioValor;
        autorizacao = linha.substring(inicioValor);
      }
      if (nomeIgual(nome, nome + separador, "content-length")) {
        if (temTamanho) return Resultado::Invalida;
        temTamanho = true;
        const char* valor = nome + separador + 1;
        while (*valor == ' ' || *valor == '\t') ++valor;
        if (*valor < '0' || *valor > '9') return Resultado::Invalida;
        while (*valor >= '0' && *valor <= '9') {
          tamanhoCorpo = tamanhoCorpo * 10 + (*valor++ - '0');
          if (tamanhoCorpo > MAX_CORPO) return Resultado::Grande;
        }
        while (*valor == ' ' || *valor == '\t') ++valor;
        if (*valor) return Resultado::Invalida;
      }
    }
    linha = "";
  }
  return Resultado::Expirada;
}

inline int digitoHex(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

// Valida duplicidade e escapes antes de usar um valor do formulário.
inline bool campo(const String& dados, const char* nome, String& valor, bool& presente) {
  presente = false;
  valor = "";
  for (int pos = 0; pos < (int)dados.length();) {
    int fim = dados.indexOf('&', pos);
    if (fim < 0) fim = dados.length();
    int igual = dados.indexOf('=', pos);
    if (igual < 0 || igual >= fim) return false;
    if (dados.substring(pos, igual) == nome) {
      if (presente) return false;
      presente = true;
      for (int i = igual + 1; i < fim; ++i) {
        char c = dados[i];
        if (c == '+') c = ' ';
        else if (c == '%') {
          if (i + 2 >= fim) return false;
          int alto = digitoHex(dados[i + 1]), baixo = digitoHex(dados[i + 2]);
          if (alto < 0 || baixo < 0) return false;
          c = char(alto * 16 + baixo);
          i += 2;
        }
        if ((unsigned char)c < 32 || c == 127) return false;
        valor += c;
      }
    }
    pos = fim + 1;
  }
  return true;
}
}
