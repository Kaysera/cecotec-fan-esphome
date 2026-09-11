#pragma once

#include "esphome/components/light/light_output.h"
#include "esphome/core/component.h"

#include "../cecotec_ventilador.h"

namespace esphome {
namespace cecotec_ventilador {

class CecotecLight : public light::LightOutput, public Component {
 public:
  void dump_config() override;
  void set_parent(CecotecHub *parent) { this->parent_ = parent; }
  void set_cold_white_temperature(float t) { this->cold_white_temperature_ = t; }
  void set_warm_white_temperature(float t) { this->warm_white_temperature_ = t; }
  void set_constant_brightness(bool v) { this->constant_brightness_ = v; }
  void set_absolute_off(bool v) { this->absolute_off_ = v; }

  light::LightTraits get_traits() override;
  void write_state(light::LightState *state) override;

 protected:
  CecotecHub *parent_{nullptr};
  float cold_white_temperature_{0};
  float warm_white_temperature_{0};
  bool constant_brightness_{false};
  bool absolute_off_{true};
  // Para no repetir la misma trama si HA reenvia el mismo estado: el receptor
  // ignora un comando que no cambia su estado, asi que reenviar es ruido.
  bool have_last_{false};
  uint16_t last_param_{0};
  bool last_on_{false};
};

}  // namespace cecotec_ventilador
}  // namespace esphome
