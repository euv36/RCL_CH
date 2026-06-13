const int buttonPin = 35;
const int activatePin = 18;

bool lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

void setup() {
  pinMode(activatePin, OUTPUT);
  pinMode(buttonPin, INPUT_PULLUP);
  digitalWrite(activatePin, LOW);   // Изначально ВЫКЛЮЧЕН (LOW)
}

void loop() {
  bool reading = digitalRead(buttonPin);

  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading == LOW) {           // Кнопка нажата (замыкает на GND)
      digitalWrite(activatePin, LOW);
    } else {
      digitalWrite(activatePin, HIGH);
    }
  }

  lastButtonState = reading;
}