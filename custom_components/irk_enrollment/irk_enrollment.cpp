#include "irk_enrollment.h"
#include "esphome/components/esp32_ble/ble.h"
#include "esphome/core/application.h"
#include "esphome/core/log.h"
#include "esphome/core/version.h"

#ifdef USE_ESP32

#include <nvs_flash.h>
#include <freertos/FreeRTOSConfig.h>
#include <esp_bt_main.h>
#include <esp_bt.h>
#include <freertos/task.h>
#include <esp_gap_ble_api.h>
#include <esp_gatts_api.h>
#include <esp_bt_defs.h>

namespace esphome {
namespace irk_enrollment {

static const char *const TAG = "irk_enrollment.component";

// Static instance pointer for callbacks
IrkEnrollmentComponent *IrkEnrollmentComponent::instance_ = nullptr;

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

// Set static instance for callbacks
instance_ = this;

// Register BLE event handlers
esp_ble_gap_register_callback(IrkEnrollmentComponent::gap_event_handler);
esp_ble_gatts_register_callback(IrkEnrollmentComponent::gatts_event_handler);

// Setup BLE security parameters
this->setup_ble_security();

delay(200);  // NOLINT
}

void IrkEnrollmentComponent::setup_ble_security() {
esp_ble_auth_req_t auth_req = ESP_LE_AUTH_BOND;  // bonding with peer device after authentication
uint8_t key_size = 16;                           // the key size should be 7~16 bytes
uint8_t init_key = ESP_BLE_ID_KEY_MASK;
uint8_t rsp_key = ESP_BLE_ID_KEY_MASK;

esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(uint8_t));
esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(uint8_t));
esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(uint8_t));
esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rsp_key, sizeof(uint8_t));
}

void IrkEnrollmentComponent::dump_config() {
ESP_LOGCONFIG(TAG, "ESP32 IRK Enrollment:");
LOG_TEXT_SENSOR(" ", "Latest IRK", this->latest_irk_);
}

void IrkEnrollmentComponent::loop() {
//ESP_LOGD(TAG, "In our loop");
this->process_bonded_devices();
}

void IrkEnrollmentComponent::process_bonded_devices() {
int dev_num = esp_ble_get_bond_device_num();
if (dev_num > 1) {
ESP_LOGW(TAG, "We have %d bonds, where we expect to only ever have 0 or 1", dev_num);
}

if (dev_num == 0) {
  //ESP_LOGD(TAG, "No devices, returning");
  return;  // No bonded devices
}

esp_ble_bond_dev_t bond_devs[dev_num];
esp_ble_get_bond_device_list(&dev_num, bond_devs);

for (int i = 0; i < dev_num; i++) {
  ESP_LOGI(TAG, "    remote BD_ADDR: %08x%04x",
  (bond_devs[i].bd_addr[0] << 24) + (bond_devs[i].bd_addr[1] << 16) +
  (bond_devs[i].bd_addr[2] << 8) + bond_devs[i].bd_addr[3],
  (bond_devs[i].bd_addr[4] << 8) + bond_devs[i].bd_addr[5]);


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

// Static callback wrappers
static void IrkEnrollmentComponent::gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
  if (instance_ != nullptr) {
    instance_->handle_gap_event(event, param);
  }
}

static void IrkEnrollmentComponent::gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param) {
  if (instance_ != nullptr) {
    instance_->handle_gatts_event(event, gatts_if, param);
  }
}

// Instance event handlers
void IrkEnrollmentComponent::handle_gap_event(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
  switch (event) {
    case ESP_GAP_BLE_KEY_EVT:
      // shows the ble key info share with peer device to the user.
      // ESP_LOGI(TAG, "key type = %s", esp_key_type_to_str(param->ble_security.ble_key.key_type));
      break;
    case ESP_GAP_BLE_SEC_REQ_EVT:
      esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
      break;
    case ESP_GAP_BLE_AUTH_CMPL_EVT: {
    // Authentication complete event
    // You can add additional logging here if needed
      break;
    }
    default:
      break;
  }
}

void IrkEnrollmentComponent::handle_gatts_event(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param) {
  switch (event) {
    case ESP_GATTS_CONNECT_EVT:
      // start security connect with peer device when receive the connect event sent by the master.
      esp_ble_set_encryption(param->connect.remote_bda, ESP_BLE_SEC_ENCRYPT_MITM);
      ESP_LOGD(TAG, "BLE device connected, starting encryption");
      break;    
    default:
      break;
  }
}

}  // namespace irk_enrollment
}  // namespace esphome

#endif
