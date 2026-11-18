unsigned long lastReceiveTime = 0;
bool ledState = LOW;
const unsigned long TIMEOUT = 2000;

// Состояние двух типов ворот: 0 = G1, 1 = G2.
int lastAngle[2] = {0, 0};
long lastArea[2] = {0, 0};
bool gateSeen[2] = {false, false};

void setup() {
  Serial.begin(115200);
  Serial2.begin(115200);
  while (!Serial2) { ; }

  while (Serial2.available()) {
    Serial2.read();
  }

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  Serial.println("Arduino готов (UART2, 2 gates)");
  lastReceiveTime = millis();
}

// Разбирает строки:
//   G1,<angle>,<area>  — ворота 1
//   G2,<angle>,<area>  — ворота 2
//   <angle>,<area>     — старый формат, считается воротами 1
void parseInput(String input) {
  int firstComma = input.indexOf(',');
  if (firstComma < 0) {
    // Сообщение без запятой — например, просто угол.
    lastAngle[0] = input.toInt();
    gateSeen[0] = true;
    Serial.print("G1 Angle: ");
    Serial.print(lastAngle[0]);
    Serial.print(" Area: ");
    Serial.println(lastArea[0]);
    return;
  }

  int secondComma = input.indexOf(',', firstComma + 1);
  if (secondComma < 0) {
    // Старый формат: angle,area
    lastAngle[0] = input.substring(0, firstComma).toInt();
    lastArea[0] = input.substring(firstComma + 1).toInt();
    gateSeen[0] = true;
    Serial.print("G1 Angle: ");
    Serial.print(lastAngle[0]);
    Serial.print(" Area: ");
    Serial.println(lastArea[0]);
    return;
  }

  String tag = input.substring(0, firstComma);
  int gate = 0;
  if (tag.startsWith("G") || tag.startsWith("g")) {
    gate = tag.substring(1).toInt();
  } else {
    gate = tag.toInt();
  }

  int angle = input.substring(firstComma + 1, secondComma).toInt();
  long area = input.substring(secondComma + 1).toInt();

  if (gate >= 1 && gate <= 2) {
    int idx = gate - 1;
    lastAngle[idx] = angle;
    lastArea[idx] = area;
    gateSeen[idx] = true;

    Serial.print("G");
    Serial.print(gate);
    Serial.print(" Angle: ");
    Serial.print(lastAngle[idx]);
    Serial.print(" Area: ");
    Serial.println(lastArea[idx]);
  }
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
        parseInput(input);
        ledState = !ledState;
        digitalWrite(LED_BUILTIN, ledState);
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
