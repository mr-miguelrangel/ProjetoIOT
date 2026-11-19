#pragma once
#include <Preferences.h>

namespace PerfilSono {
constexpr uint16_t MIN_MINUTOS = 1;
constexpr uint16_t MAX_MINUTOS = 1440;
constexpr uint16_t ATIVO_PADRAO = 10;
constexpr uint16_t SONO_PADRAO = 50;
constexpr char CHAVE[] = "perfilSono";
constexpr uint32_t BIT_PONTA = 0x80000000UL;

inline bool tempoValido(uint32_t minutos) {
  return minutos >= MIN_MINUTOS && minutos <= MAX_MINUTOS;
}

inline void carregar(Preferences& prefs, bool& ponta, uint16_t& ativo, uint16_t& sono) {
  const uint32_t perfil = prefs.isKey(CHAVE) ? prefs.getUInt(CHAVE, 0) : 0;
  ativo = (perfil & ~BIT_PONTA) >> 16;
  sono = uint16_t(perfil);
  if (tempoValido(ativo) && tempoValido(sono)) {
    ponta = (perfil & BIT_PONTA) != 0;
    return;
  }
  ponta = false;
  ativo = prefs.isKey("tAtivoMin") ? prefs.getUShort("tAtivoMin", ATIVO_PADRAO) : ATIVO_PADRAO;
  sono = prefs.isKey("tSleepMin") ? prefs.getUShort("tSleepMin", SONO_PADRAO) : SONO_PADRAO;
  if (!tempoValido(ativo)) ativo = ATIVO_PADRAO;
  if (!tempoValido(sono)) sono = SONO_PADRAO;
}

inline bool salvar(Preferences& prefs, bool ponta, uint16_t ativo, uint16_t sono) {
  // Uma chave para publicar papel e tempos juntos.
  const uint32_t perfil = (ponta ? BIT_PONTA : 0) | (uint32_t(ativo) << 16) | sono;
  return prefs.putUInt(CHAVE, perfil) == sizeof(perfil);
}
}
