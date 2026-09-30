#pragma once

#define piSerial Serial2

void setupGPIO();
void setupWire();
void setupSerial();
bool setupGyro();
void setupRPi();
void setupScreen();
void setupDribbler();
void kick();
void testKick();
void startDribbler();
void stopDribbler();