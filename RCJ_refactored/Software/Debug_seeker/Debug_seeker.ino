#include <Wire.h>

int SensorPins[] = {4,3,2,13,  12,A3,A2,A1,  A0,11,10,9,  8,7,6,5   };
bool data[16];

void sendData() {
  Wire.write((byte*) &data, 16 * sizeof(bool));

}

void setup() {
  Wire.begin(0x09);
  Wire.onRequest(sendData);
  for (int i = 0; i < 16; i++) {
    pinMode(SensorPins[i], INPUT);
  }
}

void loop() {
  for (int i = 0; i < 16; i++) {
    data[i] = digitalRead(SensorPins[i]);
  }
}
