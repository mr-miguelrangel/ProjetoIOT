#pragma once
#include <string.h>

struct PerfilWifi {
  char ssid[33], senha[65], ip[16], gateway[16], mascara[16];

  bool camposValidos() const {
    return ssid[0] && memchr(ssid, 0, sizeof(ssid)) && memchr(senha, 0, sizeof(senha)) &&
           memchr(ip, 0, sizeof(ip)) && memchr(gateway, 0, sizeof(gateway)) &&
           memchr(mascara, 0, sizeof(mascara));
  }
};
