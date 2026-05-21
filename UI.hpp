#pragma once

const int ModeAmount = 5;

const String strMode[] = {
  "Play",
  "Test drive",
  "Ball pos",
  "Show angle",
  "Follow ball"
};

enum Modes: byte {
  play = 0,
  test_drive,
  ball_pos,
  show_angle,
  follow_ball
};

bool displayInit();
void displayMenu(byte m);
void menuControl();
void readEnc();
void showMessageNum(float num, byte cursorX, byte cursorY, bool display_state, byte i_f);
void debugEnc();
void printBallPos();