#include "cecotec_button.h"

#include "esphome/core/log.h"

namespace esphome {
namespace cecotec_ventilador {

static const char *const TAG = "cecotec_ventilador.button";

void CecotecButton::press_action() {
  if (this->parent_ != nullptr && this->command_ != nullptr) {
    this->parent_->send_button(this->command_);
  }
}

void CecotecButton::dump_config() {
  ESP_LOGCONFIG(TAG, "Cecotec button: %s", this->command_ ? this->command_ : "(sin comando)");
}

}  // namespace cecotec_ventilador
}  // namespace esphome
