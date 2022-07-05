#ifndef LED_SERVICE_H
#define LED_SERVICE_H

#include <Arduino.h>

#define CW_SPEED 15
#define TONE_FREQ 1000

/**
 * @brief 
 * @see https://www.arduino.cc/reference/en/language/functions/zero-due-mkr-family/analogwriteresolution/
 */
const int FRAME_DURATION = 1000 / 60; // 60 FPS
const int LED_BIT_RESOLUTION = 8;     // accepted values are between 8 and 12
const int LED_NUM_VALUES = (0x1 << LED_BIT_RESOLUTION);
const int LED_MAX_VALUE = LED_NUM_VALUES - 1;

class LEDService
{
private:
  float __valMin = 0;
  float __valMax = 1;
  float __position = 1;
  unsigned long __lastFrame = 0;
  unsigned long __start = millis();
  unsigned long __duration = 1000;
  float value = 0;
  bool isOn = false;

  float __none();
  float __blink();
  float __breathe();
  float __flash();
  float __wave();
  float (LEDService::*__fcnPtr)() = &LEDService::__none;

  uint16_t (&prepareGammaLUT(uint16_t (&gamma)[LED_NUM_VALUES]))[LED_NUM_VALUES]
  {
    for (float i = 0; i <= LED_MAX_VALUE; ++i)
      gamma[(int)i] = pow(i / LED_MAX_VALUE, 2.2) * LED_MAX_VALUE;
    return gamma;
  };
  uint16_t gammaCorrected[LED_NUM_VALUES];
  uint16_t *gammaPtr = prepareGammaLUT(gammaCorrected);

public:
  LEDService() {}

  void setup()
  {
    Serial.println("LED cpp setup");
    pinMode(LED_BUILTIN, OUTPUT);
    analogWriteResolution(LED_BIT_RESOLUTION);
    wave(5000, .125, 0);
  }

  void loop()
  {
    unsigned long __currentFrame = millis();
    if (__currentFrame - __lastFrame < FRAME_DURATION)
      return;

    __lastFrame = millis();
    float fVal = (*this.*__fcnPtr)();
    float newVal = isOn
                       ? (fVal * (__valMax - __valMin) + __valMin)
                       : 0;

    if (value != newVal)
    {
      analogWrite(LED_BUILTIN, gammaCorrected[(int)(LED_MAX_VALUE * (1 - newVal))]);
      if (isOn)
        value = newVal;
    }
  }
  
  void yield(void)
  {
    loop();
  }

  // most basic LED functions
  void reset();
  void on(float value);
  void off();
  void toggle();

  // and some more fancy ones
  void blink(int duration, double max, double min);
  void breathe(int duration, double max, double min);
  void flash(int duration, double max, double min);
  void wave(int duration, double max, double min);
};

#endif