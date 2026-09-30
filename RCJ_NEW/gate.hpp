#pragma once

struct Gate {
  private:
    int angle = 0, area = 0, lastAngle = 0;
    void countAngle();
    void countArea();
  public:
    int getAngle();
    void updateStatus();
    int getArea();
};

extern Gate gate;