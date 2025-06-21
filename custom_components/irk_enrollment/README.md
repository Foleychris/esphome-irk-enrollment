# ESPHome IRK Enrollment Component

This component allows you to enroll iOS devices with your ESPHome device to extract their Identity Resolving Key (IRK). This IRK can then be used with other BLE tracking systems to track iOS devices even when they use random MAC addresses.

## Installation

You can install this component by adding the following to your `configuration.yaml` file:

```yaml
external_components:
  - source: github://Foleychris/esphome-irk-enrollment@main
    components: [irk_enrollment]
```

## Usage

Add the following to your ESPHome configuration:

```yaml
# Enable BLE functionality with iOS-friendly settings
esp32_ble:
  advertising: true
  scan_parameters:
    active: true
  advertising_parameters:
    min_interval: 20ms  # 0x0020 in hex (faster advertising)
    max_interval: 40ms  # 0x0040 in hex
  # iOS-friendly configuration
  manufacturer_data:
    - manufacturer_id: 0xFFFF  # Generic manufacturer ID
      data: [0x01, 0x02]
  service_data:
    - uuid: 0x180D  # Heart Rate Service
      data: [0x00, 0x01]
  service_uuids:
    - 0x180D  # Heart Rate Service
  appearance: 0x0341  # Heart Rate Sensor
  security:
    auth: BOND  # Enable bonding
    io_capability: NO_INPUT_NO_OUTPUT
    encryption: MITM  # Enable MITM protection

# Enable BLE server functionality
esp32_ble_server:
  services:
    - uuid: 0x180D  # Heart Rate Service
      characteristics:
        - uuid: 0x2A37  # Heart Rate Measurement
          properties: READ | NOTIFY
          value: "0600"  # Initial value (flags + heart rate)
    
    - uuid: 0x180A  # Device Information Service
      characteristics:
        - uuid: 0x2A29  # Manufacturer Name
          properties: READ
          value: "ESPHome"
        - uuid: 0x2A24  # Model Number
          properties: READ
          value: "IRK Collector"

# Add the IRK enrollment component
irk_enrollment:

# Add a text sensor to display the latest IRK
text_sensor:
  - platform: irk_enrollment
    name: "Latest IRK"
    icon: "mdi:cellphone-key"
```

## How it works

1. The component sets up a BLE server with Heart Rate and Device Information services that iOS devices can connect to
2. The advertising parameters are optimized for iOS device discovery
3. When an iOS device connects and pairs with the ESPHome device, the IRK is extracted
4. The IRK is published to the text sensor
5. The bond is automatically removed to allow for new devices to be enrolled

## Troubleshooting

If your iOS device is not connecting to the ESPHome device, try the following:

1. Make sure the ESPHome device is close to the iOS device
2. Restart the ESPHome device
3. Restart the iOS device
4. Make sure Bluetooth is enabled on the iOS device
5. On iOS, go to Settings > Bluetooth and look for a device named "ESPHome IRK Collector" or similar
6. If the device is already paired, forget it and try pairing again

## Sample Configuration

A complete sample configuration file is provided in the repository as `ikr-enroller-sample.yaml`.
