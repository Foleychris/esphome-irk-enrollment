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

  ESP_LOGI(TAG, "Registering BLE callbacks");
  // Register BLE event handlers with error checking
  esp_err_t gap_ret = esp_ble_gap_register_callback(IrkEnrollmentComponent::gap_event_handler);
  if (gap_ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register GAP callback: %s", esp_err_to_name(gap_ret));
    return;
  }

  esp_err_t gatts_ret = esp_ble_gatts_register_callback(IrkEnrollmentComponent::gatts_event_handler);
  if (gatts_ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register GATTS callback: %s", esp_err_to_name(gatts_ret));
    return;
  }
  
  ESP_LOGI(TAG, "Registering GATTS app");
  esp_err_t app_ret = esp_ble_gatts_app_register(0);
  if (app_ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register GATTS app: %s", esp_err_to_name(app_ret));
    return;
  }
  
  // Setup BLE security parameters
  this->setup_ble_security();
  
  // Setup GATT server with Device Information service
  this->setup_gatt_server();

  ESP_LOGI(TAG, "IRK Enrollment Component setup complete");
  delay(200);  // NOLINT
}

void IrkEnrollmentComponent::setup_gatt_server() {
  ESP_LOGI(TAG, "Setting up GATT server with Device Information service");
  
  // Device Information Service (0x180A)
  esp_bt_uuid_t device_info_uuid = {
    .len = ESP_UUID_LEN_16,
    .uuid = {.uuid16 = 0x180A}
  };
  
  // The GATT server will be created in the GATTS_REG_EVT event
  // We don't need to explicitly create it here as it will be handled in the event
  ESP_LOGI(TAG, "GATT server setup will continue after app registration");
}
void IrkEnrollmentComponent::setup_advertising() {
  // First configure advertising data
  esp_ble_adv_data_t adv_data = {};
  adv_data.set_scan_rsp = false;
  adv_data.include_name = true;
  adv_data.include_txpower = true;  // Include TX power for better iOS discovery
  adv_data.min_interval = 0x0006;
  adv_data.max_interval = 0x0010;
  adv_data.appearance = 0x0341;  // Heart Rate Sensor - more recognizable by iOS
  
  // Use a generic manufacturer ID that's not Apple's
  // 0xFFFF is for development/testing
  static uint8_t manufacturer_data[4] = {0xFF, 0xFF, 0x01, 0x02};
  adv_data.manufacturer_len = sizeof(manufacturer_data);
  adv_data.p_manufacturer_data = manufacturer_data;
  
  adv_data.service_data_len = 0;
  adv_data.p_service_data = nullptr;
  
  // Use a 128-bit UUID for better iOS compatibility
  // This is a standard Heart Rate service UUID converted to 128-bit format
  // which is more likely to be recognized by iOS
  static uint8_t service_uuid128[ESP_UUID_LEN_128] = {
    0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, 
    0x00, 0x10, 0x00, 0x00, 0x0D, 0x18, 0x00, 0x00  // 0x180D is Heart Rate Service
  };
  adv_data.service_uuid_len = sizeof(service_uuid128);
  adv_data.p_service_uuid = service_uuid128;
  
  // Set flags for iOS compatibility
  adv_data.flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT);

  esp_err_t ret = esp_ble_gap_config_adv_data(&adv_data);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to config adv data: %s", esp_err_to_name(ret));
    return;
  }

  // Configure scan response data with device name
  esp_ble_adv_data_t scan_rsp_data = {};
  scan_rsp_data.set_scan_rsp = true;
  scan_rsp_data.include_name = true;
  scan_rsp_data.include_txpower = true;
  scan_rsp_data.flag = 0;
  
  ret = esp_ble_gap_config_adv_data(&scan_rsp_data);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to config scan response data: %s", esp_err_to_name(ret));
    return;
  }

  // Set device name - use a more descriptive name for iOS
  esp_ble_gap_set_device_name("ESPHome IRK Collector");

  // Configure advertising parameters with faster intervals for better discovery
  esp_ble_adv_params_t adv_params = {};
  adv_params.adv_int_min = 0x20;  // Faster advertising (20ms)
  adv_params.adv_int_max = 0x40;  // to 40ms
  adv_params.adv_type = ADV_TYPE_IND;
  adv_params.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
  adv_params.channel_map = ADV_CHNL_ALL;
  adv_params.adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;

  // Start advertising
  ret = esp_ble_gap_start_advertising(&adv_params);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start advertising: %s", esp_err_to_name(ret));
  } else {
    ESP_LOGI(TAG, "Started BLE advertising with iOS-optimized parameters");
  }
}

void IrkEnrollmentComponent::setup_ble_security() {
  // Use MITM protection for better iOS compatibility
  esp_ble_auth_req_t auth_req = ESP_LE_AUTH_REQ_SC_MITM_BOND;  // Secure Connection, MITM protection, and bonding
  uint8_t key_size = 16;  // Maximum key size
  
  // Request all key types for better compatibility
  uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
  uint8_t rsp_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
  
  // Enable I/O capabilities for MITM
  esp_ble_io_cap_t iocap = ESP_IO_CAP_NONE;  // No input/output capability
  
  // Set security parameters
  esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(uint8_t));
  esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(uint8_t));
  esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(uint8_t));
  esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(uint8_t));
  esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rsp_key, sizeof(uint8_t));
  
  ESP_LOGI(TAG, "BLE security parameters set with MITM protection");
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
void IrkEnrollmentComponent::gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
  if (instance_ != nullptr) {
    instance_->handle_gap_event(event, param);
  }
}

void IrkEnrollmentComponent::gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param) {
  if (instance_ != nullptr) {
    instance_->handle_gatts_event(event, gatts_if, param);
  }
}

// Instance event handlers
void IrkEnrollmentComponent::handle_gap_event(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
  ESP_LOGI(TAG, "GAP event: %d", event);
  switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
      ESP_LOGI(TAG, "Advertising data set complete");
      // Start advertising after data is set
      break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
      ESP_LOGI(TAG, "Advertising start complete");
      break;
    case ESP_GAP_BLE_KEY_EVT:
      ESP_LOGI(TAG, "BLE key exchange event");
      break;
    case ESP_GAP_BLE_SEC_REQ_EVT:
      ESP_LOGI(TAG, "BLE security request event - responding with accept");
      esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
      break;
    case ESP_GAP_BLE_AUTH_CMPL_EVT:
      ESP_LOGI(TAG, "BLE authentication complete - success: %s", 
               param->ble_security.auth_cmpl.success ? "true" : "false");
      break;
    default:
      ESP_LOGD(TAG, "Unhandled GAP event: %d", event);
      break;
  }
}

void IrkEnrollmentComponent::handle_gatts_event(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param) {
  ESP_LOGI(TAG, "GATTS event received: %d, gatts_if: %d", event, gatts_if);
  
  static esp_gatt_if_t device_info_if = ESP_GATT_IF_NONE;
  static uint16_t device_info_service_handle = 0;
  static uint16_t heart_rate_service_handle = 0;
  static bool heart_rate_service_added = false;
  
  switch (event) {
    case ESP_GATTS_REG_EVT: {
      ESP_LOGI(TAG, "GATTS app registered with status: %d", param->reg.status);
      if (param->reg.status == ESP_GATT_OK) {
        device_info_if = gatts_if;
        
        // Create Device Information service
        esp_bt_uuid_t device_info_uuid = {
          .len = ESP_UUID_LEN_16,
          .uuid = {.uuid16 = 0x180A}  // Device Information Service UUID
        };
        
        esp_err_t ret = esp_ble_gatts_create_service(gatts_if, &device_info_uuid, 10);
        if (ret != ESP_OK) {
          ESP_LOGE(TAG, "Failed to create Device Information service: %s", esp_err_to_name(ret));
        } else {
          ESP_LOGI(TAG, "Device Information service creation initiated");
        }
      }
      break;
    }
    
    case ESP_GATTS_CREATE_EVT: {
      ESP_LOGI(TAG, "GATTS service created with status: %d", param->create.status);
      if (param->create.status == ESP_GATT_OK) {
        if (param->create.service_id.id.uuid.uuid.uuid16 == 0x180A) {
          // Device Information service
          device_info_service_handle = param->create.service_handle;
          ESP_LOGI(TAG, "Device Information service created with handle: %d", device_info_service_handle);
          
          // Start the Device Information service
          esp_err_t ret = esp_ble_gatts_start_service(device_info_service_handle);
          if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start Device Information service: %s", esp_err_to_name(ret));
          } else {
            ESP_LOGI(TAG, "Device Information service started");
            
            // Add Manufacturer Name characteristic
            esp_bt_uuid_t manuf_uuid = {
              .len = ESP_UUID_LEN_16,
              .uuid = {.uuid16 = 0x2A29}  // Manufacturer Name UUID
            };
            
            esp_gatt_perm_t perm = ESP_GATT_PERM_READ;
            esp_gatt_char_prop_t property = ESP_GATT_CHAR_PROP_BIT_READ;
            
            std::string manufacturer_value = "ESPHome";
            esp_attr_value_t manuf_val = {
              .attr_max_len = static_cast<uint16_t>(manufacturer_value.length()),
              .attr_len = static_cast<uint16_t>(manufacturer_value.length()),
              .attr_value = (uint8_t*)manufacturer_value.c_str()
            };
            
            ret = esp_ble_gatts_add_char(device_info_service_handle, &manuf_uuid, perm, property, &manuf_val, NULL);
            if (ret != ESP_OK) {
              ESP_LOGE(TAG, "Failed to add Manufacturer Name characteristic: %s", esp_err_to_name(ret));
            } else {
              ESP_LOGI(TAG, "Manufacturer Name characteristic added");
            }
            
            // Add Model Number characteristic
            esp_bt_uuid_t model_uuid = {
              .len = ESP_UUID_LEN_16,
              .uuid = {.uuid16 = 0x2A24}  // Model Number UUID
            };
            
            std::string model_value = "IRK Collector";
            esp_attr_value_t model_val = {
              .attr_max_len = static_cast<uint16_t>(model_value.length()),
              .attr_len = static_cast<uint16_t>(model_value.length()),
              .attr_value = (uint8_t*)model_value.c_str()
            };
            
            ret = esp_ble_gatts_add_char(device_info_service_handle, &model_uuid, perm, property, &model_val, NULL);
            if (ret != ESP_OK) {
              ESP_LOGE(TAG, "Failed to add Model Number characteristic: %s", esp_err_to_name(ret));
            } else {
              ESP_LOGI(TAG, "Model Number characteristic added");
            }
          }
        } else if (param->create.service_id.id.uuid.uuid.uuid16 == 0x180D) {
          // Heart Rate service
          heart_rate_service_handle = param->create.service_handle;
          ESP_LOGI(TAG, "Heart Rate service created with handle: %d", heart_rate_service_handle);
          
          // Start the Heart Rate service
          esp_err_t ret = esp_ble_gatts_start_service(heart_rate_service_handle);
          if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start Heart Rate service: %s", esp_err_to_name(ret));
          } else {
            ESP_LOGI(TAG, "Heart Rate service started");
            
            // Add Heart Rate Measurement characteristic
            esp_bt_uuid_t hr_meas_uuid = {
              .len = ESP_UUID_LEN_16,
              .uuid = {.uuid16 = 0x2A37}  // Heart Rate Measurement UUID
            };
            
            esp_gatt_perm_t perm = ESP_GATT_PERM_READ;
            esp_gatt_char_prop_t property = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
            
            uint8_t hr_measurement[2] = {0x06, 0x40};  // Format flags and dummy heart rate value
            esp_attr_value_t hr_val = {
              .attr_max_len = sizeof(hr_measurement),
              .attr_len = sizeof(hr_measurement),
              .attr_value = hr_measurement
            };
            
            ret = esp_ble_gatts_add_char(heart_rate_service_handle, &hr_meas_uuid, perm, property, &hr_val, NULL);
            if (ret != ESP_OK) {
              ESP_LOGE(TAG, "Failed to add Heart Rate Measurement characteristic: %s", esp_err_to_name(ret));
            } else {
              ESP_LOGI(TAG, "Heart Rate Measurement characteristic added");
            }
            
            // Start advertising after all services are set up
            this->setup_advertising();
          }
        }
        
        // If this is the Device Information service, create the Heart Rate service next
        if (param->create.service_id.id.uuid.uuid.uuid16 == 0x180A && !heart_rate_service_added) {
          heart_rate_service_added = true;
          
          // Create Heart Rate service
          esp_bt_uuid_t heart_rate_uuid = {
            .len = ESP_UUID_LEN_16,
            .uuid = {.uuid16 = 0x180D}  // Heart Rate Service UUID
          };
          
          esp_err_t ret = esp_ble_gatts_create_service(gatts_if, &heart_rate_uuid, 10);
          if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create Heart Rate service: %s", esp_err_to_name(ret));
            // Start advertising even if Heart Rate service creation failed
            this->setup_advertising();
          } else {
            ESP_LOGI(TAG, "Heart Rate service creation initiated");
          }
        }
      }
      break;
    }
    
    case ESP_GATTS_CONNECT_EVT:
      ESP_LOGI(TAG, "GATTS_CONNECT_EVT - Starting encryption");
      esp_ble_set_encryption(param->connect.remote_bda, ESP_BLE_SEC_ENCRYPT_MITM);
      break;
      
    case ESP_GATTS_DISCONNECT_EVT:
      ESP_LOGI(TAG, "GATTS_DISCONNECT_EVT - Restarting advertising");
      this->setup_advertising();  // Restart advertising when disconnected
      break;
      
    default:
      ESP_LOGD(TAG, "Unhandled GATTS event: %d", event);
      break;
  }
}

}  // namespace irk_enrollment
}  // namespace esphome

#endif
