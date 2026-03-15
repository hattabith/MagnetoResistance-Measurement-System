#include <Arduino.h>
#include <Adafruit_ADS1X15.h>

namespace {
constexpr uint8_t RELAY_PIN = 5;
constexpr uint32_t UART_BAUD = 115200;
constexpr size_t RX_BUFFER_SIZE = 64;

Adafruit_ADS1115 ads;

bool relayState = false;
char rxBuffer[RX_BUFFER_SIZE];
size_t rxIndex = 0;

String normalizeCommand(String cmd) {
  cmd.trim();
  cmd.toUpperCase();
  return cmd;
}

void setRelay(bool enabled) {
  relayState = enabled;
  digitalWrite(RELAY_PIN, relayState ? HIGH : LOW);
}

bool readHallSensor(int16_t &raw, float &voltage) {
  raw = ads.readADC_SingleEnded(0);
  voltage = ads.computeVolts(raw);
  return true;
}

void printHelp() {
  Serial.println(F("Commands:"));
  Serial.println(F("  HELP           - show this help"));
  Serial.println(F("  RELAY ON       - turn relay ON (pin 5 = HIGH)"));
  Serial.println(F("  RELAY OFF      - turn relay OFF (pin 5 = LOW)"));
  Serial.println(F("  RELAY TOGGLE   - toggle relay state"));
  Serial.println(F("  STATUS         - print relay state and Hall sensor value"));
  Serial.println(F("  READ           - same as STATUS"));
}

void printStatus() {
  int16_t raw = 0;
  float voltage = 0.0f;

  readHallSensor(raw, voltage);

  Serial.print(F("OK RELAY="));
  Serial.print(relayState ? F("ON") : F("OFF"));
  Serial.print(F(" HALL_RAW="));
  Serial.print(raw);
  Serial.print(F(" HALL_V="));
  Serial.println(voltage, 4);
}

void processCommand(const String &input) {
  String cmd = normalizeCommand(input);

  if (cmd.length() == 0) {
    return;
  }

  if (cmd == F("HELP")) {
    printHelp();
    return;
  }

  if (cmd == F("RELAY ON") || cmd == F("ON")) {
    setRelay(true);
    printStatus();
    return;
  }

  if (cmd == F("RELAY OFF") || cmd == F("OFF")) {
    setRelay(false);
    printStatus();
    return;
  }

  if (cmd == F("RELAY TOGGLE") || cmd == F("TOGGLE")) {
    setRelay(!relayState);
    printStatus();
    return;
  }

  if (cmd == F("STATUS") || cmd == F("READ")) {
    printStatus();
    return;
  }

  Serial.print(F("ERR UNKNOWN_COMMAND="));
  Serial.println(input);
  Serial.println(F("Type HELP"));
}

void readSerialCommands() {
  while (Serial.available() > 0) {
    char c = static_cast<char>(Serial.read());

    if (c == '\r') {
      continue;
    }

    if (c == '\n') {
      rxBuffer[rxIndex] = '\0';
      processCommand(String(rxBuffer));
      rxIndex = 0;
      continue;
    }

    if (rxIndex < RX_BUFFER_SIZE - 1) {
      rxBuffer[rxIndex++] = c;
    } else {
      rxIndex = 0;
      Serial.println(F("ERR COMMAND_TOO_LONG"));
    }
  }
}
} // namespace

void setup() {
  pinMode(RELAY_PIN, OUTPUT);
  setRelay(false);

  Serial.begin(UART_BAUD);
  uint32_t start = millis();
  while (!Serial && (millis() - start < 2500)) {
    delay(10);
  }

  if (!ads.begin()) {
    Serial.println(F("ERR ADS1115_NOT_FOUND"));
  } else {
    ads.setGain(GAIN_ONE);
    Serial.println(F("OK ADS1115_READY"));
  }

  Serial.println(F("MRMS controller ready. Type HELP"));
  printStatus();
}

void loop() {
  readSerialCommands();
}