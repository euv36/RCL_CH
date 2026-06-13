#pragma once

void initRpi();
int getGateAngle();
void printGateAngle();
void receiveEvent(int howMany);
extern int gateAngle;