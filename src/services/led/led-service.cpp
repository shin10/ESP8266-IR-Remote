#include "led-service.h"

float LEDService::__none()
{
  return value;
}

float LEDService::__blink()
{
  __position = (float)((millis() - __start) % __duration) / __duration;
  return __position < 0.5 ? 0.0 : 1.0;
};

float LEDService::__breathe()
{
  const float inhale = .15;
  __position = (float)((millis() - __start) % __duration) / __duration;
  if (__position < inhale)
  {
    return sin(PI * .5 * (__position / inhale));
  }
  else
  {
    return 1 - sin(PI * .5 * ((__position - inhale) / (1 - inhale)));
  }
};

float LEDService::__flash()
{
  __position = (float)((millis() - __start) % __duration) / __duration;
  return 1 - __position;
};

float LEDService::__wave()
{
  __position = (float)((millis() - __start) % __duration) / __duration;
  return sin(__position * PI * 2) * .5 + .5;
};

void LEDService::reset()
{
  __start = millis();
  __position = 1;
  __valMin = 0;
  __valMax = 1;
  __fcnPtr = &LEDService::__none;
  isOn = true;
  loop();
}

void LEDService::on(float value = 1.0)
{
  reset();
  analogWrite(LED_BUILTIN, LED_MAX_VALUE * (1 - value));
  LEDService::loop();
}

void LEDService::off()
{
  reset();
  analogWrite(LED_BUILTIN, LED_MAX_VALUE);
  isOn = false;
  LEDService::loop();
}

void LEDService::toggle()
{
  isOn = !isOn;
  LEDService::loop();
}

void LEDService::blink(int duration = 1000, double max = 1.0, double min = 0.0)
{
  reset();
  __valMin = min;
  __valMax = max;
  __duration = duration;
  __fcnPtr = &LEDService::__blink;
}

void LEDService::breathe(int duration = 5000, double max = 1.0, double min = 0.0)
{
  reset();
  __valMin = min;
  __valMax = max;
  __duration = duration;
  __fcnPtr = &LEDService::__breathe;
}

void LEDService::flash(int duration = 1000, double max = 1.0, double min = 0.0)
{
  reset();
  __valMin = min;
  __valMax = max;
  __duration = duration;
  __fcnPtr = &LEDService::__flash;
}

void LEDService::wave(int duration = 5000, double max = 1.0, double min = 0.0)
{
  reset();
  __valMin = min;
  __valMax = max;
  __duration = duration;
  __fcnPtr = &LEDService::__wave;
}
