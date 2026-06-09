#include <Wire.h>

const int Seeker = 0x09;

long ballPos[2];
int debugInfo[16];

void showBallPos();
void readSeeker();
int avg();
int mid();
int cmp(const void *a, const void *b);
void readDebug();
void showDebug();
void readAnotherInfo();



void showBallPos() {
  readSeeker();
  Serial.print("Angle = ");
  Serial.println(ballPos[0]);
  Serial.print("Distance = ");
  Serial.println(ballPos[1]);
  delay(500);
}

void readSeeker() {
  int res = Wire.requestFrom(Seeker, 4);
  Serial.println("Waiting for info...");
  Serial.print("Requested bytes = ");
  Serial.println(res);
  while(!Wire.available());
  uint8_t b1 = Wire.read();
  uint8_t b2 = Wire.read();
  ballPos[0] = ((int16_t)b2 << 8) | b1;
  b1 = Wire.read();
  b2 = Wire.read();
  ballPos[1] = ((int16_t)b2 << 8) | b1;
}


void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);
  Wire.begin();
  Wire.beginTransmission(Seeker);
}

void loop() {
  // put your main code here, to run repeatedly:
  // Serial.println("//////Ball pos//////");
  // showBallPos();
  // readDebug();
  readAnotherInfo();
  // for (int i = 0; i < 16; i++) {
  //   Serial.print(debugInfo[i]);
  //   Serial.print(" ");
  // }
  // Serial.println();
  // delay(500);
}


int avg() {
  int sum = 0;
  for (int i = 0; i < 10; i++) {
    readSeeker();
    sum += ballPos[0];
  }
  return sum / 10;
}

int mid() {
  int vals[10];
  for (int i = 0; i < 10; i++) {
    readSeeker();
    vals[i] = ballPos[0];
  }
  qsort(vals, 10, sizeof(int), cmp);
  return (vals[4] + vals[5]) / 2;
}

int cmp(const void *a, const void *b) {
  return *(int*)a - *(int*)b;
}

void readDebug() {
  int res = Wire.requestFrom(Seeker, 16 * sizeof(int));
  for (int i = 0; i < 16; i++) {
    // debugInfo[i] = Wire.read();
    uint8_t b1 = Wire.read();
    uint8_t b2 = Wire.read();
    debugInfo[i] = ((uint16_t)b2 << 8) | b1;
  }
}

void readAnotherInfo() {
  unsigned long pins =  0;
  int res = Wire.requestFrom(Seeker, sizeof(unsigned long));
  uint8_t b1 = Wire.read();
  uint8_t b2 = Wire.read();
  uint8_t b3 = Wire.read();
  // uint8_t b4 = Wire.read();
  pins = ((uint32_t)b3 << 16) + ((uint32_t)b2 << 8) + b1;
  unsigned long mask = 1;
  for (int i = 0; i < 24; i++) {
    mask = 1L << i;
    Serial.print(((pins & mask) ? 0 : 1));
  }
  Serial.println();
}

void showDebug() {
  readDebug();
  for (int i = 0; i < 16; i++) {
    Serial.print(debugInfo[i]);
    Serial.print(" ");
  }
  Serial.println();
}