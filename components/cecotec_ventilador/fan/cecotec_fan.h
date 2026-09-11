#pragma once

#include "esphome/components/fan/fan.h"
#include "esphome/core/component.h"

#include "../cecotec_ventilador.h"

namespace esphome {
namespace cecotec_ventilador {

// Toda la parte "ventilador" del mando cabe en la entidad `fan`, cada cosa en
// el building block que le corresponde (verificado contra fan.h y
// fan_traits.h de ESPHome 2026.8.2):
//
//   - **6 velocidades -> escalones**, `set_speed(true)` +
//     `set_supported_speed_count(6)`. No van como presets: ESPHome implementa
//     la convencion de Home Assistant de que **poner una velocidad BORRA el
//     preset** (`Fan::apply_preset_mode_`), o sea que los presets son para
//     modos con nombre, no para numerar velocidades.
//   - **frio/calor -> direccion de giro**, `set_direction(true)`. Es un modo de
//     dos estados que alterna, y `FanDirection{FORWARD, REVERSE}` es
//     exactamente eso.
//   - **brisa -> preset**, que es el building block de un modo con nombre.
//     Se declara con `Fan::set_supported_preset_modes()`; el de `FanTraits`
//     esta deprecado y desaparece en ESPHome 2026.11.0.
//
// El apagado va con `fan_off` (familia 0x31), NO con `power_off` (0x6f), que
// apaga el aparato entero incluida la luz.
class CecotecFan : public fan::Fan, public Component {
 public:
  void setup() override;
  void dump_config() override;
  void set_parent(CecotecHub *parent) { this->parent_ = parent; }

  // Patron canonico de ESPHome (ver speed/fan/speed_fan.h): los traits son un
  // miembro y hay que "cablear" el puntero de los presets en cada consulta.
  fan::FanTraits get_traits() override {
    this->wire_preset_modes_(this->traits_);
    return this->traits_;
  }

 protected:
  void control(const fan::FanCall &call) override;

  CecotecHub *parent_{nullptr};
  fan::FanTraits traits_;
};

}  // namespace cecotec_ventilador
}  // namespace esphome
