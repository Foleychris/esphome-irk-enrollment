#pragma once

#include "esphome/core/defines.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/esp32_ble/ble.h"
#include "esphome/components/esp32_ble_server/ble_server.h"

#ifdef USE_ESP32

#include <esp_gap_ble_api.h>
#include <esp_bt_defs.h>

namespace esphome {
namespace irk_enrollment {

class IrkEnrollmentComponent : public esphome::Component {
public:
  IrkEnrollmentComponent() {}
  void dump_config() override;
  void loop() override;
  void setup() override;
  void set_latest_irk(text_sensor::TextSensor *latest_irk) { latest_irk_ = latest_irk; }
  
  float get_setup_priority() const override;

protected:
  text_sensor::TextSensor *latest_irk_{nullptr};
  
  // Process bonded devices to extract IRKs
  void process_bonded_devices();
  
  // Reference to the BLE server component
  esp32_ble_server::BLEServer *ble_server_{nullptr};
};

}  // namespace irk_enrollment
}  // namespace esphome

#endif
