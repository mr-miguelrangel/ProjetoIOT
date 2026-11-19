#pragma once
#include <stdint.h>

namespace ProtocoloMesh {
// Tipo e token do salto nos cinco bytes reservados do primeiro slot.
constexpr uint8_t Dados = 0xD7, Ack = 0xA7, Busca = 0xB7, Oferta = 0xC7;

inline uint32_t sequencia(const uint8_t* bytes) {
  return uint32_t(bytes[1]) | (uint32_t(bytes[2]) << 8) |
         (uint32_t(bytes[3]) << 16) | (uint32_t(bytes[4]) << 24);
}

inline void marcar(uint8_t* bytes, uint8_t tipo, uint32_t numero = 0) {
  bytes[0] = tipo;
  for (int i = 1; i <= 4; ++i) {
    bytes[i] = uint8_t(numero);
    numero >>= 8;
  }
}

inline bool idValido(uint16_t id) { return id != 0 && id != 65535; }

inline unsigned long janelaBuscaMs(uint8_t sf) { return sf >= 10 ? 14000UL : 10000UL; }
inline long limiteSorteioOfertaMs(uint8_t sf) { return sf >= 10 ? 8501L : 1401L; }

// Uma oferta pendente por rádio. O prazo evita respostas de uma busca expirada.
template<class Slot>
struct OfertaAgendada {
  Slot pacote = {};
  unsigned long inicioMs = 0;
  unsigned long esperaMs = 0;

  void cancelar() { pacote.idDestinatario = 0; }

  void agendar(const Slot& resposta, unsigned long agora, unsigned long espera) {
    if (pacote.idDestinatario != 0) return;
    pacote = resposta;
    inicioMs = agora;
    esperaMs = espera;
  }

  bool pronta(unsigned long agora, uint8_t sf) {
    if (agora - inicioMs > (sf >= 10 ? 9500UL : 2400UL)) cancelar();
    return pacote.idDestinatario != 0 && agora - inicioMs >= esperaMs;
  }
};

template<class Slot, class Conhecido>
bool aceitarBloco(const Slot* slots, int quantidade, uint16_t receptor,
                  int vagas, Conhecido conhecido, bool aceitarReflexo = false) {
  for (int i = 0; i < quantidade; ++i) {
    const uint16_t origem = slots[i].idOrigem;
    const uint64_t revisao = slots[i].revisaoOrigem;
    if (!idValido(origem) || slots[i].idDestinatario != receptor ||
        (revisao >> 32) == 0 || uint32_t(revisao) == 0 ||
        (origem == receptor && !aceitarReflexo)) return false;
    for (int j = 0; j < i; ++j) if (slots[j].idOrigem == origem) return false;
    if (origem != receptor && !conhecido(origem) && --vagas < 0) return false;
  }
  return true;
}

// Um bloco em voo por remetente. Guardar o ultimo recebido permite repetir
// somente o ACK quando o remetente reenvia esse bloco.
struct CacheRecepcao {
  struct Entrada { uint16_t origem; uint32_t numero; };
  Entrada entradas[100] = {};
  unsigned proxima = 0;

  bool repetido(uint16_t origem, uint32_t numero) const {
    for (const auto& entrada : entradas)
      if (entrada.origem == origem) return entrada.numero == numero;
    return false;
  }

  void registrar(uint16_t origem, uint32_t numero) {
    for (auto& entrada : entradas) {
      if (entrada.origem == origem) {
        entrada.numero = numero;
        return;
      }
    }
    entradas[proxima] = {origem, numero};
    proxima = (proxima + 1) % (sizeof(entradas) / sizeof(entradas[0]));
  }
};
}
