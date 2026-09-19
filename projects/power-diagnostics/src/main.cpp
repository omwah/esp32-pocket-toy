// Temporary, read-only probe for locating an external-power indication signal.
#include <Arduino.h>
#include <Wire.h>

constexpr int SDA_PIN = 16;
constexpr int SCL_PIN = 15;
constexpr int BATTERY_PIN = 9;
// Only currently unassigned pins are sampled. All remain high-impedance inputs.
constexpr uint8_t DIGITAL_CANDIDATES[] = {2, 3, 6, 14, 21, 47, 48};
constexpr uint8_t ADC_CANDIDATES[] = {2, 3, 6, 14};

void scanI2c() {
  Serial.println("I2C scan:");
  for (uint8_t address = 1; address < 127; ++address) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) Serial.printf("  0x%02X\n", address);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Wire.begin(SDA_PIN, SCL_PIN);
  analogReadResolution(12);
  analogSetPinAttenuation(BATTERY_PIN, ADC_11db);
  for (uint8_t pin : DIGITAL_CANDIDATES) pinMode(pin, INPUT);
  for (uint8_t pin : ADC_CANDIDATES) analogSetPinAttenuation(pin, ADC_11db);
  Serial.println("POWER_DIAGNOSTICS v1 (all candidates are high-impedance inputs)");
  scanI2c();
}

void loop() {
  uint32_t total = 0;
  for (int i = 0; i < 16; ++i) total += analogReadMilliVolts(BATTERY_PIN);
  Serial.printf("ms=%lu battery_mv=%lu digital=", millis(), (total / 16) * 2);
  for (uint8_t pin : DIGITAL_CANDIDATES) Serial.printf("%u:%d,", pin, digitalRead(pin));
  Serial.print(" adc_mv=");
  for (uint8_t pin : ADC_CANDIDATES) Serial.printf("%u:%u,", pin, analogReadMilliVolts(pin));
  Serial.println();
  delay(1000);
}
