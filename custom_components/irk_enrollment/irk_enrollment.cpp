#include "irk_enrollment.h"
#include "esphome/core/log.h"
#include "esphome/components/esp32_ble_server/ble_server.h"

#ifdef USE_ESP32

#include <esp_gap_ble_api.h>
#include <esp_bt_defs.h>

namespace esphome {
namespace irk_enrollment {

static const char *const TAG = "irk_enrollment.component";

constexpr char hexmap[] = {'0', '1', '2', '3', '4', '5', '6', '7',
                           '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

static std::string hexStr(unsigned char *data, int len) {
  std::string s(len * 2, ' ');
  for (int i = 0; i < len; ++i) {
    s[2 * i] = hexmap[(data[len - 1 - i] & 0xF0) >> 4];
    s[2 * i + 1] = hexmap[data[len - 1 - i] & 0x0F];
  }
  return s;
}

void IrkEnrollmentComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up IRK Enrollment Component");
  
  // Get a reference to the BLE server component
  auto *ble_server = App.get_ble_server();
  if (ble_server == nullptr) {
    ESP_LOGE(TAG, "BLE server not found! Make sure you've configured the esp32_ble_server component.");
    this->mark_failed();
    return;
  }
  
  this->ble_server_ = ble_server;
  
  ESP_LOGI(TAG, "IRK Enrollment Component setup complete");
}

void IrkEnrollmentComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "ESP32 IRK Enrollment:");
  LOG_TEXT_SENSOR("  ", "Latest IRK", this->latest_irk_);
}

void IrkEnrollmentComponent::loop() {
  this->process_bonded_devices();
}

void IrkEnrollmentComponent::process_bonded_devices() {
  int dev_num = esp_ble_get_bond_device_num();
  if (dev_num > 1) {
    ESP_LOGW(TAG, "We have %d bonds, where we expect to only ever have 0 or 1", dev_num);
  }

  if (dev_num == 0) {
    return;  // No bonded devices
  }

  esp_ble_bond_dev_t bond_devs[dev_num];
  esp_ble_get_bond_device_list(&dev_num, bond_devs);

  for (int i = 0; i < dev_num; i++) {
    ESP_LOGI(TAG, "    remote BD_ADDR: %02x:%02x:%02x:%02x:%02x:%02x",
             bond_devs[i].bd_addr[0], bond_devs[i].bd_addr[1], bond_devs[i].bd_addr[2],
             bond_devs[i].bd_addr[3], bond_devs[i].bd_addr[4], bond_devs[i].bd_addr[5]);

    auto irkStr = hexStr((unsigned char *) &bond_devs[i].bond_key.pid_key.irk, 16);
    ESP_LOGI(TAG, "      irk: %s", irkStr.c_str());

    if (this->latest_irk_ != nullptr && this->latest_irk_->get_state() != irkStr) {
      this->latest_irk_->publish_state(irkStr);
    }

    esp_ble_gap_disconnect(bond_devs[i].bd_addr);
    esp_ble_remove_bond_device(bond_devs[i].bd_addr);
    ESP_LOGI(TAG, "  Disconnected and removed bond");
  }
}

float IrkEnrollmentComponent::get_setup_priority() const {
  return setup_priority::AFTER_BLUETOOTH;
}

}  // namespace irk_enrollment
}  // namespace esphome

#endif
