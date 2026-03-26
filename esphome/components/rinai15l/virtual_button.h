#ifndef VIRTUAL_BUTTON_BN_h
#define VIRTUAL_BUTTON_BN_h

#include "esphome/core/component.h"
#include "esphome/components/esp32/gpio.h"

namespace esphome {
namespace rinai15l {

class VirtualButton {
 public:
  enum class State : uint8_t {
    IDLE,
    PRESSING,
    WAITING,
    BLOCKED,
  };

  void set_pin(esp32::ESP32InternalGPIOPin *pin) { this->pin_ = pin; }
  void set_alias(const char *alias) { this->alias_ = alias; }

  void set_press_duration(uint32_t ms) { this->default_press_ms_ = ms; }
  void set_gap_duration(uint32_t ms) { this->default_gap_ms_ = ms; }
  void set_block_duration(uint32_t ms) { this->default_block_ms_ = ms; }

  void set_on_finished(std::function<void()> fn) { this->on_finished_ = fn; }

  void setup();
  void loop();

  bool press(uint32_t press_ms = 0, uint32_t block_ms = 0);
  bool press_multiple(uint8_t clicks, uint32_t press_ms = 0, uint32_t gap_ms = 0, uint32_t block_ms = 0);

  bool is_busy() const { return this->state_ != State::IDLE; }

 private:
  esp32::ESP32InternalGPIOPin *pin_{nullptr};
  const char *alias_{"UKN"};
  std::function<void()> on_finished_;
  bool was_clicked_{false};
  State state_{State::IDLE};

  // Temporization
  uint32_t state_start_ms_{0};

  // Defaults
  uint32_t default_press_ms_{150};
  uint32_t default_gap_ms_{300};
  uint32_t default_block_ms_{300};

  // Sequencies
  uint8_t remaining_clicks_{0};
  uint32_t press_duration_{0};
  uint32_t gap_duration_{0};
  uint32_t block_duration_{0};

  void set_state_(State new_state);
  void log_button_config_();
};

}  // namespace rinai15l
}  // namespace esphome

#endif
