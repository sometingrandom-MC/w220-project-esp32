#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <cstdint>

#define RELAY_PIN 10
#define BATTERY_VOLTAGE_PIN 3
#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define AUTOMATIC_INVERTER_MODE_CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define BATTERY_VOLTAGE_CHARACTERISTIC_UUID "0859e192-00ba-4690-bb66-94d2d132a255"
#define INVERTER_STATE_CHARACTERISTIC_UUID "83a4e976-f0ac-49b7-8212-27724af75ab6"
#define INVERTER_ON_THRESHOLD 13.2
#define INVERTER_OFF_THRESHOLD 12.8
#define INVERTER_EMERGENCY_THRESHOLD 12.0
#define DEBUG_MODE true // Emergency threshold bypass

// Class that represents the physical vehicle and all of its systems
class Car {
  private:
    int _batteryAdcInput;
    int _inverterRelayOutput;

  public:
    Car(int batteryInput, int inverterOutput) {
      _batteryAdcInput = batteryInput;
      _inverterRelayOutput = inverterOutput;
    }
    private:
        bool _inverterState = false;
        bool _inverterAutomaticState = true;

    public:

    // Initialize pins and default states
    void begin() {
      pinMode(_inverterRelayOutput, OUTPUT);
      digitalWrite(_inverterRelayOutput, HIGH); // Start OFF (Active-Low)
    }

    // Controls the relay state
    void setInverterState(bool turnOn) {
      if (turnOn) {
        Serial.println("System -> Inverter: ON");
        _inverterState = true;
        digitalWrite(_inverterRelayOutput, LOW);
      } else {
        Serial.println("System -> Inverter: OFF");
        _inverterState = false;
        digitalWrite(_inverterRelayOutput, HIGH);
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

      // 12-bit ADC conversion. Converts to 3.3v
      // $$V_{out} = \frac{ADC_{raw}}{4095} \times 3.3$$
      float voltage = (rawValue / 4095.0) * 3.3;

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


// Called to update states over BLE immedately after changes
void bleUpdateStates() {
    // Sends states to BLE device

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

    pVoltageChar->setValue((uint8_t*)&currentVoltage, sizeof(currentVoltage));
    pVoltageChar->notify();

    // Automatic inverter state control based on battery voltage and automatic mode state
    if (currentVoltage < INVERTER_OFF_THRESHOLD && car.getAutomaticState()) {
        car.setInverterState(false);
        bleUpdateStates();
    } else if (currentVoltage >= INVERTER_ON_THRESHOLD && car.getAutomaticState()) {
        car.setInverterState(true);
        bleUpdateStates();
    } else if (currentVoltage < INVERTER_EMERGENCY_THRESHOLD && !DEBUG_MODE) {
        car.setInverterState(false);
        bleUpdateStates();
    } else {
        bleUpdateStates();
    }


  }

  delay(10);
}
