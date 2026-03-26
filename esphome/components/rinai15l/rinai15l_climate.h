#ifndef RINAI15L_BN_h
#define RINAI15L_BN_h

#include "esphome/core/component.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/esp32/gpio.h"
#include <cstdint>
#include "virtual_button.h"

namespace esphome {
namespace rinai15l {

static constexpr uint8_t MAX_FRAME_BYTES = 32;

static constexpr uint32_t CLICK_INTERVAL_MS = 300;

enum class TempAdjustState { IDLE, PRESSING, WAIT_STABLE, VALIDATE_TEMP, VALIDATE_PWR };

class RINAI15LClimate : public climate::Climate, public Component {
 public:
  // ===== ESPHome lifecycle =====
  void setup() override;
  void loop() override;

  // ===== Climate interface =====
  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall &call) override;

  // ===== API interna (display físico) =====
  void update_display_temperature(int temperature);

  /**
   * Functions used only by __init__.py
   */
  // Buttons
  void set_power_pin(esp32::ESP32InternalGPIOPin *pin) { this->power_switch_pin_ = pin; }
  void set_up_pin(esp32::ESP32InternalGPIOPin *pin) { this->btn_up_pin_ = pin; }
  void set_down_pin(esp32::ESP32InternalGPIOPin *pin) { this->btn_down_pin_ = pin; }

  // Display communication
  void set_display_stb_pin(esp32::ESP32InternalGPIOPin *pin) { this->display_stb_pin_ = pin; }
  void set_display_clk_pin(esp32::ESP32InternalGPIOPin *pin) { this->display_clk_pin_ = pin; }
  void set_display_dio_pin(esp32::ESP32InternalGPIOPin *pin) { this->display_dio_pin_ = pin; }

  // Temperature range
  void set_min_temperature(float temp) { this->min_temperature_ = temp; }
  void set_max_temperature(float temp) { this->max_temperature_ = temp; }

 protected:
  // local cache
  static RINAI15LClimate *instance_;
  float last_display_temperature_{NAN};
  bool power_status_{false};
  int last_candidate_temp_{-1};
  int stable_temp_{-1};

  // AUX
  uint8_t aux_power_cnt_{5};
  TempAdjustState adjust_state_{TempAdjustState::IDLE};
  int desired_temp_{0};
  uint32_t state_ts_{0};

  // min-max temperature
  float min_temperature_{35};
  float max_temperature_{60};

  // GPIO for Buttons
  esp32::ESP32InternalGPIOPin *power_switch_pin_{nullptr};
  esp32::ESP32InternalGPIOPin *btn_up_pin_{nullptr};
  esp32::ESP32InternalGPIOPin *btn_down_pin_{nullptr};

  // GPIO for Display communication
  esp32::ESP32InternalGPIOPin *display_stb_pin_{nullptr};
  esp32::ESP32InternalGPIOPin *display_clk_pin_{nullptr};
  esp32::ESP32InternalGPIOPin *display_dio_pin_{nullptr};
  VirtualButton btn_power_;
  VirtualButton btn_up_;
  VirtualButton btn_down_;

  // ===== ISR / loop =====
  volatile bool stb_active_{false};
  volatile bool frame_ready_{false};
  volatile uint8_t bit_count_{0};
  volatile uint8_t current_byte_{0};
  volatile uint8_t frame_[MAX_FRAME_BYTES];
  volatile uint8_t frame_len_{0};

  // ISR hook
  static void IRAM_ATTR clk_isr();

  int decode_frame_digit(uint8_t up_seg, uint8_t rest_seg, bool aux);

  void process_frame_(const uint8_t *data, size_t len);

  void set_power_status(bool status, bool publish = true);

  void dump_frame(const uint8_t *data, size_t len);
};

}  // namespace rinai15l
}  // namespace esphome

#endif