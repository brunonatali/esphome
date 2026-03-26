#include "rinai15l_climate.h"

namespace esphome {
namespace rinai15l {

static const char *TAG = "rinai15l";

// Static pointer to instance
RINAI15LClimate *RINAI15LClimate::instance_ = nullptr;

// ================= SETUP =================
void RINAI15LClimate::setup() {
  // Default / Security states
  this->mode = climate::CLIMATE_MODE_OFF;
  this->action = climate::CLIMATE_ACTION_OFF;
  this->power_status_ = false;

  instance_ = this;

  // Init display
  this->display_stb_pin_->setup();
  this->display_clk_pin_->setup();
  this->display_dio_pin_->setup();
  // Force pins setup (this must be just INPUT)
  this->display_stb_pin_->pin_mode(gpio::FLAG_INPUT);
  this->display_clk_pin_->pin_mode(gpio::FLAG_INPUT);
  this->display_dio_pin_->pin_mode(gpio::FLAG_INPUT);
  attachInterrupt(digitalPinToInterrupt(this->display_clk_pin_->get_pin()), RINAI15LClimate::clk_isr, RISING);

  // Init buttons
  this->btn_power_.set_pin(this->power_switch_pin_);
  this->btn_power_.set_alias("power");
  this->btn_power_.setup();
  this->btn_power_.set_on_finished([this]() {
    ESP_LOGD(TAG, "BTN PWR finished");
    this->adjust_state_ = TempAdjustState::VALIDATE_PWR;
    this->state_ts_ = millis();
  });

  this->btn_up_.set_pin(this->btn_up_pin_);
  this->btn_up_.set_alias("up");
  this->btn_up_.setup();
  this->btn_up_.set_on_finished([this]() {
    ESP_LOGD(TAG, "BTN UP finished");
    this->adjust_state_ = TempAdjustState::WAIT_STABLE;
    this->state_ts_ = millis();
  });
  this->btn_down_.set_pin(this->btn_down_pin_);
  this->btn_down_.set_alias("down");
  this->btn_down_.setup();
  this->btn_down_.set_on_finished([this]() {
    ESP_LOGD(TAG, "BTN DOWN finished");
    this->adjust_state_ = TempAdjustState::WAIT_STABLE;
    this->state_ts_ = millis();
  });

  ESP_LOGI(TAG, "Setup DONE. CFG: D=%d,P=%d", ESPHOME_LOG_LEVEL, this->display_clk_pin_->get_pin());
}

// ================= LOOP =================
void RINAI15LClimate::loop() {
  if (this->frame_ready_) {
    if (this->adjust_state_ == TempAdjustState::PRESSING) {
      // Discard frames
      this->frame_ready_ = false;
      this->frame_len_ = 0;
    } else {
      // Copies ISR -> LOOP
      uint8_t local_frame[MAX_FRAME_BYTES];
      uint8_t local_len = 0;

      noInterrupts();
      local_len = this->frame_len_;
      memcpy(local_frame, (const void *) this->frame_, local_len);
      this->frame_ready_ = false;
      this->frame_len_ = 0;
      interrupts();

      // Process copied frame
      this->process_frame_(local_frame, local_len);
    }
  }

  // Button traits
  this->btn_power_.loop();
  this->btn_up_.loop();
  this->btn_down_.loop();

  if (this->adjust_state_ != TempAdjustState::IDLE) {
    switch (this->adjust_state_) {
      case TempAdjustState::WAIT_STABLE:
        // aguarda display parar de piscar / estabilizar leitura
        if (millis() - this->state_ts_ > 800) {  // ajuste fino depois
          this->adjust_state_ = TempAdjustState::VALIDATE_TEMP;
          this->stable_temp_ = -1;
        }
        break;

      case TempAdjustState::VALIDATE_PWR: {
        bool waiting_on = this->mode != climate::CLIMATE_MODE_OFF;
        if (waiting_on == this->power_status_) {
          ESP_LOGI(TAG, "Mode adjustment finished");
          this->adjust_state_ = TempAdjustState::IDLE;
        }
        break;
      }

      case TempAdjustState::VALIDATE_TEMP: {
        if (this->stable_temp_ < this->min_temperature_) {
          // ainda não temos leitura confiável
          break;
        }

        int delta = this->desired_temp_ - this->stable_temp_;

        ESP_LOGI(TAG, "Validation: stable=%d desired=%d delta=%d", this->stable_temp_, this->desired_temp_, delta);

        if (delta == 0) {
          ESP_LOGI(TAG, "Temperature adjustment finished");
          this->adjust_state_ = TempAdjustState::IDLE;
        } else {
          // micro-ajuste
          uint8_t clicks = abs(delta) + 1;

          if (clicks > 3) {
            ESP_LOGW(TAG, "Temperature adjustment ERROR. Diff %d > 2", delta);
          }

          this->adjust_state_ = TempAdjustState::PRESSING;

          if (delta > 0)
            this->btn_up_.press_multiple(clicks);
          else
            this->btn_down_.press_multiple(clicks);
        }
        break;
      }

      default:
        break;
    }
  }
}

// ================= TRAITS =================
climate::ClimateTraits RINAI15LClimate::traits() {
  climate::ClimateTraits traits;

  traits.set_supports_current_temperature(true);
  traits.set_supports_two_point_target_temperature(false);

  traits.set_supported_modes({
      climate::CLIMATE_MODE_OFF,
      climate::CLIMATE_MODE_HEAT,
  });

  traits.set_supported_fan_modes({});
  traits.set_supported_swing_modes({});

  traits.set_visual_min_temperature(this->min_temperature_);
  traits.set_visual_max_temperature(this->max_temperature_);
  traits.set_visual_temperature_step(1.0f);

  traits.set_supported_presets({});

  return traits;
}

// ================= DISPLAY → CLIMATE =================
void RINAI15LClimate::update_display_temperature(int temperature) {
  float temp = static_cast<float>(temperature);

  if (this->current_temperature == temp)
    return;

  if (temperature > 0)
    this->set_power_status(true, false);

  this->current_temperature = temp;
  this->target_temperature = temp;
  this->publish_state();
}

// ================= HA → DISPOSITIVO =================
void RINAI15LClimate::control(const climate::ClimateCall &call) {
  // Intend to adjust mode (OFF / HEAT)
  if (call.get_mode().has_value()) {
    if (this->adjust_state_ != TempAdjustState::IDLE) {
      ESP_LOGW(TAG, "Cannot change mode while adjusting, aborting");
      return;
    }

    this->mode = *call.get_mode();

    if (this->mode == climate::CLIMATE_MODE_OFF) {
      if (this->power_status_) {
        this->btn_power_.press();
      }
    } else if (this->mode == climate::CLIMATE_MODE_HEAT) {
      if (!this->power_status_) {
        this->btn_power_.press();
      }
    }

    ESP_LOGI(TAG, "Requested mode change: %d", mode);
  }

  // Intend to adjust temperature
  if (call.get_target_temperature().has_value()) {
    int target = static_cast<int>(roundf(*call.get_target_temperature()));

    ESP_LOGI(TAG, "Requested target temperature: %d", target);

    if (this->stable_temp_ < this->min_temperature_) {
      ESP_LOGW(TAG, "Stable temperature %d < %d, aborting", this->stable_temp_, this->min_temperature_);
      return;
    }

    if (this->adjust_state_ != TempAdjustState::IDLE) {
      ESP_LOGW(TAG, "Already adjusting temperature, ignoring request");
      return;
    }

    // Clamp de segurança
    if (target < this->min_temperature_)
      target = this->min_temperature_;
    if (target > this->max_temperature_)
      target = this->max_temperature_;

    int delta = target - this->stable_temp_;

    if (delta == 0) {
      ESP_LOGI(TAG, "Desired Temp. %d = %d, aborting", target, this->stable_temp_);
      return;
    }

    this->adjust_state_ = TempAdjustState::PRESSING;
    this->desired_temp_ = target;
    ESP_LOGI(TAG, "Adjusting temperature: stable=%d target=%d delta=%d", this->stable_temp_, target, delta);

    uint8_t clicks = abs(delta) + 1;  // 1 click to activate edit
    if (delta > 0) {
      this->btn_up_.press_multiple(clicks);
    } else {
      this->btn_down_.press_multiple(clicks);
    }

    // Atualiza estado interno do Climate (otimista)
    this->target_temperature = target;
    this->publish_state();

    return;
  }

  this->publish_state();
}

void RINAI15LClimate::set_power_status(bool status, bool publish) {
  if (this->power_status_ == status)
    return;

  this->power_status_ = status;

  if (status) {
    this->mode = climate::CLIMATE_MODE_HEAT;
    this->action = climate::CLIMATE_ACTION_HEATING;
    this->aux_power_cnt_ = 5;
  } else {
    this->mode = climate::CLIMATE_MODE_OFF;
    this->action = climate::CLIMATE_ACTION_OFF;
    this->aux_power_cnt_ = 0;
  }

  if (publish)
    this->publish_state();
}

int RINAI15LClimate::decode_frame_digit(uint8_t up_seg, uint8_t rest_seg, bool aux) {
  // Auxiliar package
  if (aux) {
    // All numbers except 1 and 4
    if (up_seg == 0x80) {
      switch (rest_seg) {
        case 0x3B:
          return 0;
        case 0x35:
          return 2;
        case 0x2D:
          return 3;
        case 0x2E:
          return 5;
        case 0x3E:
          return 6;
        case 0x09:
          return 7;
        case 0x3F:
          return 8;
        case 0x2F:
          return 9;

        default:
          ESP_LOGD(TAG, "Unknown up seg: 0x%02X", rest_seg);
          return -1;
      }
    } else {
      switch (rest_seg) {
        case 0x09:
          return 1;
        case 0x0F:
          return 4;

        default:
          ESP_LOGD(TAG, "Unknown down seg: 0x%02X", rest_seg);
          return -1;
      }
    }
  } else {
    if (up_seg != 0x00) {
      ESP_LOGD(TAG, "Wrong seg: 0x%02X", rest_seg);
      return -1;
    }

    switch (rest_seg) {
      case 0x77:
        return 0;
      case 0x12:
        return 1;
      case 0x6B:
        return 2;
      case 0x5B:
        return 3;
      case 0x1E:
        return 4;
      case 0x5D:
        return 5;
      case 0x7D:
        return 6;
      case 0x13:
        return 7;
      case 0x7F:
        return 8;
      case 0x5F:
        return 9;

      default:
        ESP_LOGD(TAG, "Unknown seg: 0x%02X", rest_seg);
        return -1;
    }
  }
}

void RINAI15LClimate::process_frame_(const uint8_t *data, size_t len) {
#if ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_DEBUG
  this->dump_frame(data, len);
#endif

  // Min length
  if (len < 32)
    return;

  // check start bytes
  if (data[0] != 0x01)
    return;

  if (data[1] != 0x20 && data[1] != 0x40)
    return;

  if (this->power_status_ && data[5] == 0x00 && data[8] == 0x00 && data[9] == 0x00 && data[10] == 0x00 &&
      data[11] == 0x00) {
    this->set_power_status(false);
  }

  bool aux_pkg = data[5] == 0x08;

  int tens = this->decode_frame_digit(data[8], data[9], aux_pkg);
  int units = this->decode_frame_digit(data[10], data[11], aux_pkg);

  int temp = tens * 10 + units;

  ESP_LOGD(TAG, "Temp: Curr=%d Candidate=%d", this->stable_temp_, temp);

  if (temp < 35 || temp > 60)
    return;

  if (!this->power_status_)
    this->set_power_status(true);

  if (temp != this->last_candidate_temp_) {
    this->last_candidate_temp_ = temp;
    return;
  }

  if (temp != this->stable_temp_) {
    this->stable_temp_ = temp;

    this->update_display_temperature(this->stable_temp_);

    ESP_LOGI(TAG, "Stable temperature detected: %d", temp);
  }
}

void IRAM_ATTR RINAI15LClimate::clk_isr() {
  auto *self = instance_;
  if (!self)
    return;

  bool stb = self->display_stb_pin_->digital_read();

  // STB HIGH → ends frame
  if (stb) {
    if (self->stb_active_) {
      self->stb_active_ = false;

      if (self->frame_len_ > 0) {
        self->frame_ready_ = true;
      }

      self->bit_count_ = 0;
      self->current_byte_ = 0;
    }
    return;
  }

  // STB LOW → active frame
  self->stb_active_ = true;

  bool bit = self->display_dio_pin_->digital_read();
  self->current_byte_ |= (bit << self->bit_count_);
  self->bit_count_++;

  if (self->bit_count_ == 8) {
    if (self->frame_len_ < MAX_FRAME_BYTES) {
      self->frame_[self->frame_len_++] = self->current_byte_;
    }
    self->bit_count_ = 0;
    self->current_byte_ = 0;
  }
}

void RINAI15LClimate::dump_frame(const uint8_t *data, size_t len) {
  char line[128];
  int idx = 0;

  idx += snprintf(line + idx, sizeof(line) - idx, "Frame (%u): ", len);

  for (uint8_t i = 0; i < len; i++) {
    idx += snprintf(line + idx, sizeof(line) - idx, "%02X ", data[i]);
  }

  ESP_LOGD(TAG, "%s", line);
}

}  // namespace rinai15l
}  // namespace esphome
