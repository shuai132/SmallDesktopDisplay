#ifndef ANIMATE_H
#define ANIMATE_H

#include <Arduino.h>

struct AnimationFrame
{
  const uint8_t *data;
  uint32_t size;
};

class AnimationPlayer
{
public:
  bool nextFrame(AnimationFrame &frame);

private:
  int16_t frameIndex_ = -1;
};

#endif
