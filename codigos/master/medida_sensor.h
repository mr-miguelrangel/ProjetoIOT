#pragma once
#include <stdint.h>
#include <math.h>

// Os 4 bytes do campo "medida" do SlotTelemetria: temperatura e umidade.
// Arquivo idêntico no Master e no Slave: os dois lados precisam concordar.
//
//   bits  0..15  temperatura x 10, com sinal (int16)  -> -3276.7 .. 3276.7 °C
//   bits 16..31  umidade x 10, sem sinal (uint16)     ->  0.0 .. 100.0 %
//
// Temperatura = SEM_LEITURA indica que o nó ainda não tem leitura válida;
// nesse caso o Master não publica nada para o nó.
namespace MedidaSensor {
constexpr int16_t SEM_LEITURA = INT16_MIN;  // 0x8000

inline uint32_t semLeitura() { return uint16_t(SEM_LEITURA); }

inline uint32_t codificar(float temperatura, float umidade) {
  if (isnan(temperatura) || isnan(umidade)) return semLeitura();
  long t = lroundf(temperatura * 10.0f);
  long u = lroundf(umidade * 10.0f);
  if (t <= SEM_LEITURA) t = SEM_LEITURA + 1;
  if (t > INT16_MAX) t = INT16_MAX;
  if (u < 0) u = 0;
  if (u > 1000) u = 1000;
  return uint32_t(uint16_t(int16_t(t))) | (uint32_t(u) << 16);
}

inline bool valida(uint32_t medida) { return int16_t(medida & 0xFFFF) != SEM_LEITURA; }
inline float temperatura(uint32_t medida) { return int16_t(medida & 0xFFFF) / 10.0f; }
inline float umidade(uint32_t medida) { return uint16_t(medida >> 16) / 10.0f; }
}
