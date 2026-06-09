const int EncA = 9;
const int EncB = 10;
const int EncKey = 6;

int mode = 0;
const int ModeAmount = 10;


void readEnc();

void setup() {
  pinMode(EncA, INPUT_PULLUP);
  pinMode(EncB, INPUT_PULLUP);
  pinMode(EncKey, INPUT_PULLUP);
}

void loop() {
  readEnc();
  Serial.print("Mode = ");
  Serial.println(mode);
}

void readEnc() {
  static int prevA = 0;
  int a = digitalRead(EncA);
  int b = digitalRead(EncB);
  if (prevA == b) {
    mode++;
  } else{
    mode--;
  }
  prevA = a;
  if (mode == ModeAmount) {
    mode = 0;
  } else if (mode < 0) {
    mode = ModeAmount - 1;
  }
}
