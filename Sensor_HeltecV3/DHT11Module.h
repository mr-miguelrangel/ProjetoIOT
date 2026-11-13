#pragma once
/**
 * DHT11Module.h
 * Implementação de ISensor para DHT11 (lib "DHT sensor library" da Adafruit).
 *
 * Dados codificados (5 bytes):
 *  int16  temperatura * 10  (°C)
 *  uint16 umidade * 10      (%)
 *  uint8  flags             (bit0 = última leitura inválida/dados antigos)
 *
 * Observações DHT11: resolução de 1 °C / 1 %, intervalo mínimo entre leituras 1 s.
 */
#include <DHT.h>
#include "SensorModule.h"

class DHT11Module : public ISensor {
public:
  DHT11Module(uint8_t pin, uint8_t retries = 3, uint16_t minIntervalMs = 1100)
      : _dht(pin, DHT11), _retries(retries), _minInterval(minIntervalMs) {}

  bool begin() override {
    _dht.begin();
    _init = true;
    _lastReadMs = millis() - _minInterval;  // permite leitura imediata
    return true;
  }

  SensorStatus read() override {
    if (!_init) return SENSOR_ERR_NOT_INIT;

    // respeita o intervalo mínimo do sensor
    uint32_t elapsed = millis() - _lastReadMs;
    if (elapsed < _minInterval) delay(_minInterval - elapsed);

    for (uint8_t i = 0; i < _retries; i++) {
      float h = _dht.readHumidity();
      float t = _dht.readTemperature();
      _lastReadMs = millis();
      if (!isnan(h) && !isnan(t)) {
        _temp = t;
        _hum = h;
        _valid = true;
        _stale = false;
        return SENSOR_OK;
      }
      delay(_minInterval);
    }
    _stale = true;  // mantém o último valor bom, mas sinaliza
    return SENSOR_ERR_READ;
  }

  SensorType type() const override { return SENSOR_DHT11; }

  size_t encodeData(uint8_t *buf, size_t maxLen) const override {
    if (maxLen < 5 || !_valid) return 0;
    int16_t t = (int16_t)lroundf(_temp * 10.0f);
    uint16_t h = (uint16_t)lroundf(_hum * 10.0f);
    buf[0] = t >> 8;  buf[1] = t & 0xFF;
    buf[2] = h >> 8;  buf[3] = h & 0xFF;
    buf[4] = _stale ? 0x01 : 0x00;
    return 5;
  }

  void printLast(Print &out) const override {
    out.printf("DHT11 -> T=%.1f C  H=%.1f %%  %s\n", _temp, _hum, _stale ? "(STALE)" : "");
  }

  float temperature() const { return _temp; }
  float humidity() const { return _hum; }

  // Útil no gateway para decodificar os dados de um frame válido.
  static bool decode(const uint8_t *frame, size_t len, float &temp, float &hum, bool &stale) {
    if (!FramePacker::validate(frame, len) || frame[1] != SENSOR_DHT11 || frame[3] != 5) return false;
    temp = (int16_t)((frame[4] << 8) | frame[5]) / 10.0f;
    hum  = (uint16_t)((frame[6] << 8) | frame[7]) / 10.0f;
    stale = frame[8] & 0x01;
    return true;
  }

private:
  DHT _dht;
  uint8_t _retries;
  uint16_t _minInterval;
  uint32_t _lastReadMs = 0;
  float _temp = NAN, _hum = NAN;
  bool _init = false, _valid = false, _stale = false;
};
