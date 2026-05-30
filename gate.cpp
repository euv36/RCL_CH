#include <Arduino.h>
#include <Wire.h>
#include "gate.hpp"

#define I2C_ADDRESS 0x64

// Целевая переменная, куда записывается каждое ИЗМЕНЕНИЕ данных
int targetVariable = 0;

// Переменная для отслеживания предыдущего значения (для фильтрации повторов)
int lastValue = -1;

// Флаг для вывода логов в основной цикл (опционально)
volatile bool newValueReceived = false;

void setupGate() {
  // Serial.begin(115200);  // Скорость монитора порта
  Wire.begin(I2C_ADDRESS);
  // Wire.setClock(400000);
  Wire.onReceive(receiveEvent);
  Serial.println("Arduino I2C Ready...");
}

void getGateAngle() {
  // Если пришло новое УНИКАЛЬНОЕ значение, выводим его в консоль
  // while (1) {
    if (newValueReceived) {
      Serial.print("Переменная обновлена! Новое значение: ");
      Serial.println(targetVariable);

      newValueReceived = false;  // Сбрасываем флаг
    }
  // }
}

// Обработчик прерывания I2C (вызывается при приеме данных)
void receiveEvent(int howMany) {
  // Нам должно прийти 3 байта: 1 байт регистра (0x00) + 2 байта данных.
  // Если пришло меньше, значит пакет поврежден.
  if (howMany < 3) {
    // Очищаем буфер, если там что-то осталось
    while (Wire.available()) Wire.read();
    return;
  }

  byte reg = Wire.read();   // Пропускаем первый байт (виртуальный регистр 0x00)
  byte high = Wire.read();  // Читаем старший байт
  byte low = Wire.read();   // Читаем младший байт

  // Собираем два байта обратно в одно целое число int
  int receivedNumber = (high << 8) | low;

  // Главное условие: проверяем на повтор
  if (receivedNumber != lastValue) {
    targetVariable = receivedNumber;  // Записываем в целевую переменную
    lastValue = receivedNumber;       // Запоминаем для следующего сравнения
    newValueReceived = true;          // Поднимаем флаг для loop()
  }
  // Если receivedNumber == lastValue, код просто проигнорирует его
}
