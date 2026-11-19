#pragma once
#include <stdint.h>
#include <string.h>
#include <Preferences.h>

// Manter este arquivo identico nos dois sketches.
namespace ConfigRadio {
constexpr uint8_t SF_PADRAO = 7;
constexpr uint32_t FREQUENCIA_PADRAO = 915000000UL;
constexpr uint32_t FREQUENCIA_MIN = 902000000UL;
constexpr uint32_t FREQUENCIA_MAX = 928000000UL;

inline bool numeroValido(const char* texto, uint32_t minimo, uint32_t maximo, uint32_t& resultado) {
  if (texto == nullptr || *texto == '\0') return false;
  uint32_t valor = 0;
  for (const char* p = texto; *p; ++p) {
    if (*p < '0' || *p > '9') return false;
    uint32_t digito = (uint32_t)(*p - '0');
    if (valor > maximo / 10 || (valor == maximo / 10 && digito > maximo % 10)) return false;
    valor = valor * 10 + digito;
  }
  if (valor < minimo) return false;
  resultado = valor;
  return true;
}

inline bool identidadeValida(const char* identidade) {
  if (identidade == nullptr || strlen(identidade) != 32) return false;
  for (const char* p = identidade; *p; ++p) {
    if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F'))) return false;
  }
  return true;
}

// Cada binario diferente aplica os padroes do codigo uma vez. Reinicios com
// a mesma imagem conservam ajustes do portal. Nao depende de versao manual.
// O marcador e gravado por ultimo: uma falha permite tentar novamente no boot.
inline bool carregar(Preferences& prefs, const char* espaco, const char* chaveSF,
                     uint8_t& sf, uint32_t& frequencia, const char* identidadeFirmware) {
  sf = SF_PADRAO;
  frequencia = FREQUENCIA_PADRAO;
  if (!prefs.begin(espaco, false)) return false;
  bool ok = identidadeValida(identidadeFirmware);
  if (ok && prefs.getString("radio_img", "") != identidadeFirmware) {
    ok = prefs.putUChar(chaveSF, sf) == sizeof(uint8_t);
    ok = ok && prefs.putULong("freq", frequencia) == sizeof(uint32_t);
    ok = ok && prefs.putString("radio_img", identidadeFirmware) == strlen(identidadeFirmware);
  } else {
    uint8_t salvoSF = prefs.getUChar(chaveSF, SF_PADRAO);
    uint32_t salvaFreq = prefs.getULong("freq", FREQUENCIA_PADRAO);
    if (salvoSF >= 7 && salvoSF <= 12) sf = salvoSF;
    if (salvaFreq >= FREQUENCIA_MIN && salvaFreq <= FREQUENCIA_MAX) frequencia = salvaFreq;
  }
  prefs.end();
  return ok;
}
}
