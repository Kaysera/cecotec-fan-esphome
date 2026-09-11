#pragma once

#include "esphome/components/button/button.h"
#include "esphome/core/component.h"

#include "../cecotec_ventilador.h"

namespace esphome {
namespace cecotec_ventilador {

class CecotecButton : public button::Button, public Component {
 public:
  void dump_config() override;
  void set_parent(CecotecHub *parent) { this->parent_ = parent; }
  void set_command(const char *command) { this->command_ = command; }

 protected:
  void press_action() override;

  CecotecHub *parent_{nullptr};
  const char *command_{nullptr};
};

}  // namespace cecotec_ventilador
}  // namespace esphome
