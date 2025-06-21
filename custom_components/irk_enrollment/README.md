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
# Enable BLE functionality
esp32_ble:
  # Set IO capability for pairing
  io_capability: keyboard_only

# Enable BLE server
esp32_ble_server:
  # Set device appearance to Heart Rate Sensor for better iOS visibility
  appearance: 0x0341
  # Add manufacturer data for better iOS visibility
  manufacturer_data: [0xFF, 0xFF, 0x01, 0x02]
  # Define BLE services
  services:
    - uuid: 0x180D  # Heart Rate Service
      characteristics:
        - uuid: 0x2A37  # Heart Rate Measurement
          read: true
          notify: true
          value: "0600"  # Initial value (flags + heart rate)
    
    - uuid: 0x180A  # Device Information Service
      characteristics:
        - uuid: 0x2A29  # Manufacturer Name
          read: true
          value: "ESPHome"
        - uuid: 0x2A24  # Model Number
          read: true
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
