#pragma once
#include <Preferences.h>
#include <esp_random.h>

namespace PortalSeguro {
inline String gerarChave() {
  char chave[25];
  const auto a = (unsigned long)esp_random();
  const auto b = (unsigned long)esp_random();
  const auto c = (unsigned long)esp_random();
  snprintf(chave, sizeof(chave), "%08lx%08lx%08lx", a, b, c);
  return String(chave);
}

inline String carregarSenha(const char* chave) {
  Preferences prefs;
  if (!prefs.begin("portal_auth", false)) return "";
  String senha = prefs.isKey(chave) ? prefs.getString(chave) : "";
  if (senha.length() != 24) {
    senha = gerarChave();
    if (senha.length() != 24 || prefs.putString(chave, senha) != senha.length()) senha = "";
  }
  prefs.end();
  return senha;
}

inline bool confere(const String& recebido, const String& esperado) {
  if (!esperado.length() || recebido.length() != esperado.length()) return false;
  unsigned diferenca = 0;
  for (size_t i = 0; i < esperado.length(); ++i) diferenca |= recebido[i] ^ esperado[i];
  return diferenca == 0;
}
}
