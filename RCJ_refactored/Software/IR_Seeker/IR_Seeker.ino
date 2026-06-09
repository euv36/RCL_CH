#include <Arduino.h>  
#include <Wire.h>

#define printf(x) Serial.print(x);
#define printfs(x) Serial.print(x); Serial.print(" ");
#define printflt(x) Serial.print(x); Serial.print("\t");
#define printfln(x) Serial.println(x);

int SensorPins[] = {4,3,2,13,  12,A3,A2,A1,  A0,11,10,9,  8,7,6,5   };
int rersensorpins[] = {20,19,18,5,  4,11,10,9,  8,3,2,1,  0,23,22,21 };
int err_pins[] = {0,0,0,1,  1,0,0,0,  0,1,0,0,  0,0,0,0}; 
int readEE[16] = {0};
unsigned long raw_valueBCD[300] = {0};

short ir, rast = 0, qrast = 0, max_ = 0;
double cos_side, sin_side;
float angle = 0;
int data[2];
unsigned long pind = 0, pinc = 0,  one = 1;

float _err_measure = 3;  
float _q = 0.2;   

int b = 12, max_i  = 0;
float a = 0;

float simpleKalman(float newVal) {
  float _kalman_gain, _current_estimate;
  static float _err_estimate = _err_measure;
  static float _last_estimate;
  _kalman_gain = (float)_err_estimate / (_err_estimate + _err_measure);
  _current_estimate = _last_estimate + (float)_kalman_gain * (newVal - _last_estimate);
  _err_estimate =  (1.0 - _kalman_gain) * _err_estimate + fabs(_last_estimate - _current_estimate) * _q;
  _last_estimate = _current_estimate;
  return _current_estimate;
}


void sendData() {
  Wire.write((byte*) &readEE, 16 * sizeof(int));

}

void setup() {
  Serial.begin(115200);
  Wire.begin(0x09);
  Wire.onRequest(sendData);
  for (int i = 0; i < 16; ++i) {
    pinMode(SensorPins[i], INPUT);
  }
  for (int i = 0; i < 16; i++) {
    readEE[0] = 0;
  }

}

void loop() {
  // data[0] = 90;
  // data[1] = 10;
  // while(1);  
  // while(1);
  cos_side = 0, sin_side = 0, rast = 0, max_ =  0;
  ir = 0;

  unsigned long startTime_us = micros() ;

  pind = PIND;
  pinc = PINC;
  raw_valueBCD [ir] = PINB + (pinc << 8 + (pind << 16));
  startTime_us = micros() ;

  while ((micros() - startTime_us) < 833*2) {
    pind = PIND;
    pinc = PINC;
    raw_valueBCD[ir] = PINB + (pinc << 8 + (pind << 16));
    ir += 1;
  }


  for (int i = 0; i < 16; i++) {
    int shift = rersensorpins[i];
    for (int j = 0; j < ir-1; j++) {
      readEE[i] += ((raw_valueBCD[j] & (one << shift)) == 0);
    }
  }


  int rett = 0;
  for (int i = 0; i < 16; i++) {
    if (err_pins[i]){
      continue;
    }
    rett += (readEE[i] > 0);
    cos_side += (cos(i * 22.5 * 3.1415926 / 180.0) * readEE[i]  * 100 );
    sin_side += (sin(i * 22.5 * 3.1415926 / 180.0) * readEE[i] * 100 );
    if (readEE[i] > max_) {
      max_ = max(max_, readEE[i]);
      max_i = i;
    }
  }


  printflt(ir);
  angle = (atan2(sin_side, cos_side) * 180 / 3.1415);//simpleKalman((atan2(sin_side, cos_side) * 180 / 3.1415));
  printflt(angle);
  a = a + 0.03 * (max_ - min(readEE[max_i + 1], readEE[(max_i + 15)%16 ]) - a);
  

  for (int i = 0; i < 16; i++) {
    printfs(readEE[i]);
    readEE[i] = 0;
  }
  for (int i = 0; i < ir+1; i++) {
    raw_valueBCD[i] = 0;
  }

  
  if (rett  == 0) {
    data[0] = 999;
    data[1] = -1;
  } else {
    data[0] = angle;
    data[1] = ir;
  }


  printfln("");
}