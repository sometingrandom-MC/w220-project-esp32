#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <cstdint>
#include <esp_sleep.h>

// ESP32 Pin Configuration
#define RELAY_PIN 18
#define BATTERY_VOLTAGE_PIN 3

// Bluetooth UUID Configuration
#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define AUTOMATIC_INVERTER_MODE_CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define BATTERY_VOLTAGE_CHARACTERISTIC_UUID "0859e192-00ba-4690-bb66-94d2d132a255"
#define INVERTER_STATE_CHARACTERISTIC_UUID "83a4e976-f0ac-49b7-8212-27724af75ab6"

// Voltage Thresholds
#define INVERTER_ON_THRESHOLD 13.2
#define INVERTER_OFF_THRESHOLD 12.8
#define INVERTER_EMERGENCY_THRESHOLD 12.0
#define DEBUG_MODE true // Emergency threshold bypass

// Sleep Configuration
#define AWAKE_DURATION_SECONDS 2
#define SLEEP_DURATION_SECONDS 10

bool deviceConnected = false;
bool oldDeviceConnected = false;

// Class that represents the physical vehicle and all of its systems
class Car {
  private:
    int _batteryAdcInput;
    int _inverterRelayOutput;
    bool _inverterState = false;
    bool _inverterAutomaticState = true;

  public:
    Car(int batteryInput, int inverterOutput) {
      _batteryAdcInput = batteryInput;
      _inverterRelayOutput = inverterOutput;
    }

    // Initialize pins and default states
    void begin() {
      pinMode(_inverterRelayOutput, OUTPUT);
      digitalWrite(_inverterRelayOutput, LOW); // Start OFF (Active-High)
    }

    // Controls the relay state
    void setInverterState(bool turnOn) {
      if (turnOn) {
        Serial.println("System -> Inverter: ON");
        _inverterState = true;
        digitalWrite(_inverterRelayOutput, HIGH);
      } else {
        Serial.println("System -> Inverter: OFF");
        _inverterState = false;
        digitalWrite(_inverterRelayOutput, LOW);
      }
    }

    bool getInverterState() {
        return _inverterState;
    }

    // Controls if automatic mode (for the inverter) is turned on
    void setAutomaticState(bool enable) {
        if (enable) {
            Serial.println("System -> Automatic Inverter Control: ON");
        } else {
            Serial.println("System -> Automatic Inverter Control: OFF");
        }
        _inverterAutomaticState = enable;
    }

    bool getAutomaticState() {
        return _inverterAutomaticState;
    }

    // Reads the battery voltage from the ADC pin and does proper conversions.
    float getBatteryVoltage() {
      int rawValue = analogRead(_batteryAdcInput);

      // 12-bit ADC conversion. Converts to 12v
      float voltage = (rawValue / 4095.0) * 3.3 * 5;

      //Prints data to serial for debugging
      Serial.print("Battery Input -> Raw: ");
      Serial.print(rawValue);
      Serial.print(" | Voltage: ");
      Serial.println(voltage);

      return voltage;
    }
};

// Instantiate with both pins
Car car(BATTERY_VOLTAGE_PIN, RELAY_PIN);

unsigned long lastReadTime = 0;
const long interval = 1000;

// Handles BLE client connections and disconnections
class ServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      Serial.println("Client connected.");
      deviceConnected = true;
    }

    void onDisconnect(BLEServer* pServer) {
      Serial.println("Client disconnected.");
      deviceConnected = false;
    }
};

// Sets inverter state when the BLE characteristic is written to
class InverterStateCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String value = pCharacteristic->getValue();
      if (value.length() > 0) {
          if (value[0] == '1') {
            car.setInverterState(true);
            car.setAutomaticState(false);
          }
          else if (value[0] == '0') {
            car.setInverterState(false);
            car.setAutomaticState(false);
          }
      }
    }
};

// Sets automatic mode's state when the BLE characteristic is written to
class InverterAutomaticCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String value = pCharacteristic->getValue();
      if (value.length() > 0) {
          if (value[0] == '1') {
            car.setAutomaticState(true);
          }
          else if (value[0] == '0') {
            car.setAutomaticState(false);
          }
      }
    }
};

// Initializes the BLE Characteristics
BLECharacteristic *pVoltageChar;
BLECharacteristic *pInverterState;
BLECharacteristic *pAutomaticInverterMode;

void setup() {
  Serial.begin(115200);

  // Use the class to handle hardware initialization
  car.begin();

  // Bluetooth Initialization
  BLEDevice::init("Mercedes-Benz S430");
  BLEServer *pServer = BLEDevice::createServer();

  // Assign the server callbacks here so advertising restarts on disconnect
  pServer->setCallbacks(new ServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);

  pInverterState = pService->createCharacteristic(
      INVERTER_STATE_CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_WRITE
  );
  pAutomaticInverterMode = pService->createCharacteristic(
      AUTOMATIC_INVERTER_MODE_CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY |BLECharacteristic::PROPERTY_WRITE
  );
  pVoltageChar = pService->createCharacteristic(
      BATTERY_VOLTAGE_CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );

  pAutomaticInverterMode->setCallbacks(new InverterAutomaticCallbacks());
  pInverterState->setCallbacks(new InverterStateCallbacks());
  pService->start();
  BLEDevice::startAdvertising();

  Serial.println("System Ready.");
}

// Called to update states over BLE immediately after changes
void bleUpdateStates() {
    // Stop crash by preventing notify() when no client is connected
    if (!deviceConnected) return;

    // Sends inverter state
    bool inverterState = car.getInverterState();
    pInverterState->setValue((uint8_t*)&inverterState, 1);
    pInverterState->notify();

    // Sends inverter auto mode state
    bool inverterAutomaticState = car.getAutomaticState();
    pAutomaticInverterMode->setValue((uint8_t*)&inverterAutomaticState, 1);
    pAutomaticInverterMode->notify();
}

void loop() {
  // Handle BLE disconnect safely
  if (!deviceConnected && oldDeviceConnected) {
      delay(500); // Give the Bluetooth stack time to clean up
      BLEDevice::startAdvertising();
      Serial.println("Restarting BLE advertising...");
      oldDeviceConnected = deviceConnected;
  }

  // Handle BLE connect
  if (deviceConnected && !oldDeviceConnected) {
      oldDeviceConnected = deviceConnected;
  }

  // Serial debugging controls
  if (Serial.available() > 0) {
    char incomingByte = Serial.read();
    if (incomingByte == '1') {
        car.setInverterState(true);
        car.setAutomaticState(false);
        bleUpdateStates();
    } else if (incomingByte == '0') {
        car.setInverterState(false);
        car.setAutomaticState(false);
        bleUpdateStates();
    } else if (incomingByte == '2') {
        car.setAutomaticState(true);
        bleUpdateStates();
    }
  }

  unsigned long currentTime = millis();
  if (currentTime - lastReadTime >= interval) {
    lastReadTime = currentTime;

    // Sends battery voltage on interval
    float currentVoltage = car.getBatteryVoltage();

    if (deviceConnected) {
        pVoltageChar->setValue((uint8_t*)&currentVoltage, sizeof(currentVoltage));
        pVoltageChar->notify();
    }

    // Automatic inverter state control based on battery voltage and automatic mode state
    if (currentVoltage < INVERTER_OFF_THRESHOLD && car.getAutomaticState()) {
        if (car.getInverterState() != false) {
            car.setInverterState(false);
            bleUpdateStates();
        }
    } else if (currentVoltage >= INVERTER_ON_THRESHOLD && car.getAutomaticState()) {
        if (car.getInverterState() != true) {
            car.setInverterState(true);
            bleUpdateStates();
        }
    } else if (currentVoltage < INVERTER_EMERGENCY_THRESHOLD && !DEBUG_MODE) {
        if (car.getInverterState() != false) {
            car.setInverterState(false);
            bleUpdateStates();
        }
    }

    // Initiates deep sleep if voltage is low, not connected, inverter is off, and AWAKE_DURATION_SECONDS have passed
    if (currentVoltage < INVERTER_OFF_THRESHOLD && !deviceConnected && millis() > (AWAKE_DURATION_SECONDS * 1000) && !car.getInverterState()) {
        Serial.printf("Voltage below threshold and no BLE connection. Sleeping for %d seconds...\n", SLEEP_DURATION_SECONDS);
        esp_sleep_enable_timer_wakeup(SLEEP_DURATION_SECONDS * 1000000ULL);
        esp_deep_sleep_start();
    }
  }

  delay(10);
}
