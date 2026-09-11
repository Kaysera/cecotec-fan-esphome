#include "cecotec_fan.h"

#include "esphome/core/log.h"

namespace esphome {
namespace cecotec_ventilador {

static const char *const TAG = "cecotec_ventilador.fan";

static const int SPEED_COUNT = 6;
// Almacenamiento estatico: `Fan` guarda el puntero, no una copia.
static const char *const PRESET_BREEZE = "Brisa";

void CecotecFan::setup() {
  this->traits_.set_speed(true);
  this->traits_.set_supported_speed_count(SPEED_COUNT);
  this->traits_.set_oscillation(false);
  // El mando no invierte el giro de verdad: lo que alterna es el modo
  // frio/calor. Se modela como direccion porque es el unico modo de dos
  // estados que `fan` ofrece, y asi no hace falta un boton suelto.
  this->traits_.set_direction(true);
  // Antes de restore_state_(): la restauracion consulta los traits.
  this->set_supported_preset_modes({PRESET_BREEZE});

  // No hay realimentacion del ventilador: el estado es optimista y se
  // desincroniza en cuanto alguien usa el mando fisico. Se restaura el ultimo
  // conocido y se documenta la limitacion (P6 es la solucion de verdad).
  auto restore = this->restore_state_();
  if (restore.has_value()) {
    restore->apply(*this);
  }
}

void CecotecFan::control(const fan::FanCall &call) {
  // Home Assistant manda una llamada por intencion (velocidad, direccion o
  // preset), asi que se emite UNA pulsacion por llamada y con esta prioridad.
  bool dir_changed = false;
  if (call.get_direction().has_value() && *call.get_direction() != this->direction) {
    this->direction = *call.get_direction();
    dir_changed = true;
  }
  if (call.get_state().has_value()) {
    this->state = *call.get_state();
  }
  if (call.get_speed().has_value()) {
    this->speed = *call.get_speed();
  }
  const bool preset_asked = call.has_preset_mode();
  // Aplica el preset, y si la llamada traia velocidad lo borra (convencion de
  // Home Assistant que implementa `Fan::apply_preset_mode_`).
  this->apply_preset_mode_(call);
  if (this->state && this->speed < 1) {
    this->speed = 1;
  }

  if (this->parent_ != nullptr) {
    // Siempre por NOMBRE de boton: el hub emite la pulsacion real capturada,
    // que es la unica que el receptor acepta (el README, seccion "El resultado negativo que cambio el diseño").
    if (dir_changed) {
      this->parent_->send_button("cold_warm");
    } else if (preset_asked && this->has_preset_mode()) {
      this->parent_->send_button("breeze");
    } else if (!this->state) {
      this->parent_->send_button("fan_off");
    } else {
      char name[8] = "fan_0";
      name[4] = (char) ('0' + this->speed);
      this->parent_->send_button(name);
    }
  }

  this->publish_state();
  this->save_state_();
}

void CecotecFan::dump_config() {
  ESP_LOGCONFIG(TAG, "Cecotec fan:");
  ESP_LOGCONFIG(TAG, "  velocidades: %d escalones", SPEED_COUNT);
  ESP_LOGCONFIG(TAG, "  direccion: el modo frio/calor del mando (cold_warm)");
  ESP_LOGCONFIG(TAG, "  preset: %s (breeze)", PRESET_BREEZE);
  ESP_LOGCONFIG(TAG, "  el estado es optimista: no hay realimentacion del ventilador");
}

}  // namespace cecotec_ventilador
}  // namespace esphome
