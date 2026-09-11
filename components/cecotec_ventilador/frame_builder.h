// Generador de trama por campos, sin ninguna dependencia de ESPHome.
//
// Esta separado a proposito para poder compilarlo en el PC con g++ y comparar
// su salida contra la implementacion de referencia en Python, sin hardware.
//
// OJO: esta ruta genera la trama a partir de los campos que entendemos y deja
// congelados los que no. El receptor NO la acepta; solo se usa con
// `use_bank: false`, para investigar. Ver el README, seccion "El resultado
// negativo que cambio el diseño".
#pragma once

#include <cstdint>
#include <cstring>

#include "protocol.h"

namespace esphome {
namespace cecotec_ventilador {

/// Escribe `len` bits de `value` (MSB primero) en `buf` a partir del bit
/// `bit_off`, contando bits desde el MSB del byte 0.
inline void frame_set_bits(uint8_t *buf, uint16_t bit_off, uint16_t len, uint32_t value) {
  for (uint16_t i = 0; i < len; i++) {
    uint16_t b = bit_off + i;
    uint8_t mask = 0x80 >> (b & 7);
    bool v = (value >> (len - 1 - i)) & 1;
    if (v) {
      buf[b >> 3] |= mask;
    } else {
      buf[b >> 3] &= ~mask;
    }
  }
}

/// Construye los 40 bytes de la subvariante `b6d`: plantilla mas parcheo de los
/// cuatro campos que sabemos interpretar. Ver el aviso de arriba.
inline void frame_build_b6d(uint8_t *out, uint8_t family, uint16_t param, uint8_t light_flag,
                            uint8_t counter) {
  memcpy(out, CECOTEC_TEMPLATE_B6D, PAYLOAD_BYTES);
  frame_set_bits(out, BIT_FAMILY, LEN_FAMILY, family);
  frame_set_bits(out, BIT_PARAM, LEN_PARAM, param);
  frame_set_bits(out, BIT_CTR, LEN_CTR, counter);
  frame_set_bits(out, BIT_LIGHT_FLAG, 1, light_flag ? 1 : 0);
}

/// Busca un boton por nombre en la tabla. Devuelve nullptr si no existe.
inline const CecotecCommand *frame_find_command(const char *name) {
  for (size_t i = 0; i < CECOTEC_COMMAND_COUNT; i++) {
    if (strcmp(CECOTEC_COMMANDS[i].name, name) == 0) {
      return &CECOTEC_COMMANDS[i];
    }
  }
  return nullptr;
}

}  // namespace cecotec_ventilador
}  // namespace esphome
