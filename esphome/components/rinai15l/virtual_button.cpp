#include "virtual_button.h"
#include "esphome/core/log.h"

namespace esphome {
namespace rinai15l {

static const char *const TAG = "virtual_button";

void VirtualButton::setup() {
  if (this->pin_ == nullptr) {
    ESP_LOGE(TAG, "GPIO pin not set for %s", this->alias_);
    return;
  }

  this->pin_->setup();
  this->pin_->digital_write(true);  // solto (pull-up)
  this->log_button_config_();
  this->state_ = State::IDLE;
}

bool VirtualButton::press(uint32_t press_ms, uint32_t block_ms) {
  return this->press_multiple(1, press_ms ? press_ms : this->default_press_ms_, this->default_gap_ms_,
                              block_ms ? block_ms : this->default_block_ms_);
}

bool VirtualButton::press_multiple(uint8_t clicks, uint32_t press_ms, uint32_t gap_ms, uint32_t block_ms) {
  if (this->state_ != State::IDLE || clicks == 0)
    return false;

  this->remaining_clicks_ = clicks;
  this->press_duration_ = press_ms ? press_ms : this->default_press_ms_;
  this->gap_duration_ = gap_ms ? gap_ms : this->default_gap_ms_;
  this->block_duration_ = block_ms ? block_ms : this->default_block_ms_;

  ESP_LOGD(TAG, "Starting %u clicks of %d,%d,%d on %s", clicks, this->press_duration_, this->gap_duration_,
           this->block_duration_, this->alias_);
  this->set_state_(State::PRESSING);
  return true;
}

void VirtualButton::loop() {
  if (this->state_ == State::IDLE)
    return;

  uint32_t now = millis();

  switch (this->state_) {
    case State::IDLE:  // does nothing
      break;

    case State::PRESSING:
      if (this->press_duration_ == 0) {  // Wrong state
        this->set_state_(State::IDLE);
        return;
      }

      if (now - this->state_start_ms_ >= this->press_duration_) {
        this->pin_->digital_write(true);  // solta
        this->remaining_clicks_--;
        ESP_LOGW(TAG, "RC %d", this->remaining_clicks_);
        if (this->remaining_clicks_ > 0) {
          this->set_state_(State::WAITING);
        } else if (this->block_duration_ > 0) {
          this->set_state_(State::BLOCKED);
        } else {
          this->set_state_(State::IDLE);
        }
      }
      break;

    case State::WAITING:
      if (this->gap_duration_ == 0) {  // Wrong state
        this->set_state_(State::IDLE);
        return;
      }

      if (now - this->state_start_ms_ >= this->gap_duration_) {
        if (this->remaining_clicks_ > 0) {
          this->set_state_(State::PRESSING);
        } else {
          this->set_state_(State::IDLE);
        }
      }
      break;

    case State::BLOCKED:
      if (this->block_duration_ == 0) {  // Wrong state
        this->set_state_(State::IDLE);
        return;
      }

      if (now - this->state_start_ms_ >= this->block_duration_) {
        this->set_state_(State::IDLE);
      }
      break;
  }
}

void VirtualButton::set_state_(State new_state) {
  this->state_ = new_state;
  this->state_start_ms_ = millis();

  switch (new_state) {
    case State::PRESSING:
      ESP_LOGD(TAG, "%s PRESS", this->alias_);
      this->pin_->digital_write(false);
      this->was_clicked_ = true;
      break;

    case State::WAITING:
      ESP_LOGD(TAG, "%s WAIT", this->alias_);
      break;

    case State::BLOCKED:
      ESP_LOGD(TAG, "%s BLOCK", this->alias_);
      break;

    case State::IDLE:
      ESP_LOGD(TAG, "%s IDLE", this->alias_);

      if (this->was_clicked_ && this->on_finished_)
        this->on_finished_();

      this->was_clicked_ = false;  // reset click action
      this->press_duration_ = 0;   // reset press
      this->gap_duration_ = 0;     // reset gap
      this->block_duration_ = 0;   // reset block
      break;
  }
}

void VirtualButton::log_button_config_() {
  if (!this->pin_) {
    ESP_LOGE(TAG, "Button %s: pin is null", this->alias_);
    return;
  }

  uint8_t gpio = this->pin_->get_pin();
  auto flags = this->pin_->get_flags();

  ESP_LOGI(TAG, "Button %-5s | GPIO=%u | output=%d | input=%d | open_drain=%d | inverted=%d", this->alias_, gpio,
           (flags & gpio::FLAG_OUTPUT) != 0, (flags & gpio::FLAG_INPUT) != 0, (flags & gpio::FLAG_OPEN_DRAIN) != 0,
           this->pin_->is_inverted());
}

}  // namespace rinai15l
}  // namespace esphome
