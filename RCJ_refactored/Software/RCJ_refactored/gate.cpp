// Используем порт Serial2 (пины 16 и 17 на Arduino Mega Pro)
#include "Arduino.h"

#define piSerial Serial2 

// int currentAngle = 0; // Переменная для постоянного хранения текущего угла

void initRpi() {
  // Serial.begin(9600);   // Монитор порта компьютера для отладки
  piSerial.begin(9600); // Связь с Raspberry Pi 4
  Serial.println("Система готова. Ожидание углов от Raspberry Pi...");
}

int getGateAngle() {
  // Если от Raspberry Pi прилетел новый измененный угол
  int gateAngle = 0;
  static int lastGateAngle = gateAngle; 
  if (piSerial.available() > 0) {
    String inputData = piSerial.readStringUntil('\n');
    int newAngle = inputData.toInt();
    
    // Обновляем глобальную переменную только новым значением
    gateAngle = newAngle; 
    Serial.print("-> ПОЛУЧЕН НОВЫЙ УГОЛ: ");
    Serial.println(gateAngle);
    if (gateAngle > 180) {
      gateAngle -= 360;
    }
    lastGateAngle = gateAngle;
    return gateAngle;
  } else {
    return lastGateAngle;
  }

  // --- Тут ваша основная логика работы с углом ---
  // Пример: непрерывная печать или управление сервоприводом.
  // Так как Raspberry шлет данные только при изменениях, Arduino крутится 
  // в loop со своим последним сохраненным значением 'currentAngle'
  
  /* 
  Serial.print("Текущий рабочий угол системы: ");
  Serial.println(currentAngle);
  delay(100); 
  */
}

void printGateAngle() {
  Serial.print("Gate angle = ");
  Serial.println(getGateAngle());
  delay(200);
}