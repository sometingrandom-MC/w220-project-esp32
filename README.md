# ESP32 Automation for Car Power Systems
This project uses BLE to sync the state of the power system with a BLE device, also allowing control over the system. It also automates the control of an inverter by reading the voltage of the car's battery and turning the inverter on/off depending on the state.

## Build/Upload Instructions
This code is designed for a ESP32-C3 MINI and may not work on other ESP32 models.

To compile and upload, use the following commands.
```
git clone https://github.com/sometingrandom-MC/w220-project-esp32.git
cd w220-project-esp32
arduino-cli compile --fqbn esp32:esp32:esp32c3 --port /dev/ttyUSB0 --upload .
```

## Debugging Instructions
To access the serial connection for debugging, use the following command after installing picocom.
```
picocom /dev/ttyUSB0 -b 115200
```
