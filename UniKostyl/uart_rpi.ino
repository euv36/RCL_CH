unsigned long lastReceiveTime = 0;
bool ledState = LOW;
const unsigned long TIMEOUT = 2000;

int lastAngle = 0;
long lastArea = 0;

void setup() {
  Serial.begin(115200);
  Serial2.begin(115200);
  while (!Serial2) { ; }

  while (Serial2.available()) {
    Serial2.read();
  }

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  Serial.println("Arduino готов (UART2)");
  lastReceiveTime = millis();
}

void loop() {
  if (Serial2.available()) {
    String input = Serial2.readStringUntil('\n');
    input.trim();

    if (input.length() > 0) {
      lastReceiveTime = millis();

      if (input == "HB") {
        ledState = !ledState;
        digitalWrite(LED_BUILTIN, ledState);
        Serial.println("HB");
      } else {
        int commaIdx = input.indexOf(',');
        if (commaIdx >= 0) {
          lastAngle = input.substring(0, commaIdx).toInt();
          lastArea = input.substring(commaIdx + 1).toInt();
        } else {
          lastAngle = input.toInt();
        }
        ledState = !ledState;
        digitalWrite(LED_BUILTIN, ledState);
        Serial.print("Angle: ");
        Serial.print(lastAngle);
        Serial.print(" Area: ");
        Serial.println(lastArea);
      }
    } else {
      while (Serial2.available()) Serial2.read();
    }
  }

  if (millis() - lastReceiveTime > TIMEOUT) {
    digitalWrite(LED_BUILTIN, LOW);
    ledState = LOW;
  }
}
