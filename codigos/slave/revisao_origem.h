#pragma once
#include <nvs.h>
#include <stdint.h>

// Reserva uma época por boot. O contador de amostras fica em RAM.
inline uint32_t reservarEpocaOrigem() {
  nvs_handle_t nvs;
  if (nvs_open("mesh_sample", NVS_READWRITE, &nvs) != ESP_OK) return 0;
  uint32_t anterior = 0;
  esp_err_t status = nvs_get_u32(nvs, "epoch", &anterior);
  if (status == ESP_ERR_NVS_NOT_FOUND) {
    anterior = 0;
    status = ESP_OK;
  }
  if (status == ESP_OK && anterior == UINT32_MAX) status = ESP_ERR_INVALID_STATE;
  if (status == ESP_OK) status = nvs_set_u32(nvs, "epoch", anterior + 1);
  if (status == ESP_OK) status = nvs_commit(nvs);
  nvs_close(nvs);
  return status == ESP_OK ? anterior + 1 : 0;
}
