#pragma once
/**
 * SensorModule.h
 * Interface genérica de sensor + empacotamento de frame para LoRa.
 * Independente do rádio: o código principal envia o buffer como quiser.
 *
 * Frame (big-endian):
 *  [0]    nodeId
 *  [1]    sensorType  (ver SensorType)
 *  [2]    seq         (contador, rollover em 255)
 *  [3]    len         (tamanho do payload de dados)
 *  [4..]  dados       (definido por cada sensor)
 *  [N-1]  crc8        (poly 0x07, sobre todos os bytes anteriores)
 */
#include <Arduino.h>

enum SensorType : uint8_t {
  SENSOR_DHT11 = 0x01,
  SENSOR_DHT22 = 0x02,
  // adicione novos tipos aqui
};

enum SensorStatus : uint8_t {
  SENSOR_OK = 0,
  SENSOR_ERR_READ,
  SENSOR_ERR_NOT_INIT,
  SENSOR_ERR_BUFFER,
};

inline uint8_t crc8(const uint8_t *data, size_t len) {
  uint8_t crc = 0x00;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t b = 0; b < 8; b++)
      crc = (crc & 0x80) ? (crc << 1) ^ 0x07 : (crc << 1);
  }
  return crc;
}

class ISensor {
public:
  virtual ~ISensor() {}
  virtual bool begin() = 0;                       // inicializa o hardware
  virtual SensorStatus read() = 0;                // faz a leitura e guarda internamente
  virtual SensorType type() const = 0;
  virtual size_t encodeData(uint8_t *buf, size_t maxLen) const = 0;  // dados da última leitura
  virtual void printLast(Print &out) const = 0;   // debug legível
};

class FramePacker {
public:
  explicit FramePacker(uint8_t nodeId) : _nodeId(nodeId), _seq(0) {}

  // Retorna tamanho total do frame, ou 0 em caso de erro.
  size_t pack(const ISensor &s, uint8_t *out, size_t maxLen) {
    if (maxLen < 5) return 0;
    size_t dlen = s.encodeData(out + 4, maxLen - 5);
    if (dlen == 0) return 0;
    out[0] = _nodeId;
    out[1] = (uint8_t)s.type();
    out[2] = _seq++;
    out[3] = (uint8_t)dlen;
    size_t total = 4 + dlen;
    out[total] = crc8(out, total);
    return total + 1;
  }

  static bool validate(const uint8_t *frame, size_t len) {
    if (len < 5) return false;
    if (frame[3] + 5u != len) return false;
    return crc8(frame, len - 1) == frame[len - 1];
  }

private:
  uint8_t _nodeId;
  uint8_t _seq;
};
