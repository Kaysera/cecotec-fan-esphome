#include "cecotec_light.h"

#include "esphome/core/log.h"

namespace esphome {
namespace cecotec_ventilador {

static const char *const TAG = "cecotec_ventilador.light";

// Familia de los tres botones de color de luz (light_cold/neutral/warm), que
// son los unicos que traen niveles de canal y por eso llevan el flag de luz.
static const uint8_t FAMILY_LIGHT_COLOR = 0x21;

light::LightTraits CecotecLight::get_traits() {
  auto traits = light::LightTraits();
  traits.set_supported_color_modes({light::ColorMode::COLD_WARM_WHITE});
  traits.set_min_mireds(this->cold_white_temperature_);
  traits.set_max_mireds(this->warm_white_temperature_);
  return traits;
}

// Los tres unicos niveles de luz que existen como pulsacion capturada. El
// mando no tiene mas, y el receptor solo acepta tramas reales
// (el README, seccion "El resultado negativo que cambio el diseño"), asi que HA elige el mas cercano.
struct LightPreset {
  const char *name;
  float cw, ww;
};
static const LightPreset LIGHT_PRESETS[] = {
    {"light_cold", 1.0f, 0.0f},
    {"light_neutral", 1.0f, 1.0f},
    {"light_warm", 0.0f, 1.0f},
};

static int nearest_preset(float cw, float ww) {
  int best = 0;
  float best_d = 1e9f;
  for (int i = 0; i < 3; i++) {
    float dc = cw - LIGHT_PRESETS[i].cw, dw = ww - LIGHT_PRESETS[i].ww;
    float d = dc * dc + dw * dw;
    if (d < best_d) {
      best_d = d;
      best = i;
    }
  }
  return best;
}

void CecotecLight::write_state(light::LightState *state) {
  if (this->parent_ == nullptr) {
    return;
  }

  bool on;
  state->current_values_as_binary(&on);

  uint16_t param = 0;
  int preset = -1;
  if (on) {
    float cw, ww;
    state->current_values_as_cwww(&cw, &ww, this->constant_brightness_);
    preset = nearest_preset(cw, ww);
    param = ((uint16_t) lroundf(cw * 255.0f) << 8) | (uint16_t) lroundf(ww * 255.0f);
    // En modo banco lo que identifica el estado es el preset, no el parametro
    // exacto: dos brillos distintos que caen en el mismo preset emiten lo mismo.
    if (this->parent_->use_bank()) {
      param = (uint16_t) preset;
    }
  }

  // La primera llamada es la del arranque: ESPHome restaura el estado y llama a
  // write_state() antes de que nadie haya tocado nada. Se ADOPTA ese estado sin
  // emitir. Un reinicio de la placa, o un corte de luz, no tiene por que
  // mandarle ordenes al ventilador; y como no hay realimentacion, emitir en el
  // arranque no sincroniza nada, solo dispara 613 ms de radio a ciegas.
  if (!this->have_last_) {
    this->have_last_ = true;
    this->last_on_ = on;
    this->last_param_ = param;
    ESP_LOGD(TAG, "estado inicial adoptado sin emitir (on=%s param=0x%04X)", YESNO(on), param);
    return;
  }

  // Filtro de estado: sin el, UNA sola orden de HA se convierte en ~50
  // pulsaciones. ESPHome llama a write_state() en cada iteracion del bucle
  // mientras dura la transicion (`default_transition_length`, 1 s por defecto),
  // y en la rampa cada iteracion trae un cw/ww distinto, asi que ni siquiera
  // coinciden entre si. Medido en P0: 50 tramas por arranque.
  if (on == this->last_on_ && (!on || param == this->last_param_)) {
    return;
  }

  if (!on) {
    if (this->absolute_off_ && !this->parent_->use_bank()) {
      // una pregunta abierta: parametro `00 00` con la familia de color. No existe
      // como pulsacion capturada, asi que solo se puede intentar generandola.
      this->parent_->send_command(FAMILY_LIGHT_COLOR, 0x0000, 1);
    } else {
      // `light_toggle` es un toggle: solo se manda en la transicion on -> off.
      this->parent_->send_button("light_toggle");
    }
  } else if (this->parent_->use_bank()) {
    this->parent_->send_button(LIGHT_PRESETS[preset].name);
  } else {
    this->parent_->send_command(FAMILY_LIGHT_COLOR, param, 1);
  }

  this->last_on_ = on;
  this->last_param_ = param;
}

void CecotecLight::dump_config() {
  ESP_LOGCONFIG(TAG, "Cecotec light:");
  ESP_LOGCONFIG(TAG, "  apagado absoluto: %s", YESNO(this->absolute_off_));
  if (!this->absolute_off_) {
    ESP_LOGCONFIG(TAG, "  OJO: light_toggle es un toggle, el estado se desincroniza");
  }
  ESP_LOGCONFIG(TAG, "  brillo intermedio pendiente de U3: puede que solo acepte 00/ff");
}

}  // namespace cecotec_ventilador
}  // namespace esphome
