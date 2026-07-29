#ifndef ANIMATE_H
#define ANIMATE_H

#include <Arduino.h>

struct AnimationPatch
{
  const uint8_t *data;
  uint32_t size;
  int16_t x;
  int16_t y;
};

struct AnimationFrame
{
  const uint8_t *data;
  uint32_t size;
  uint16_t firstPatch;
  uint8_t patchCount;
};

class AnimationPlayer
{
public:
  bool nextFrame(AnimationFrame &frame);
  bool getPatch(uint16_t patchIndex, AnimationPatch &patch) const;

private:
  int16_t frameIndex_ = -1;
  bool initialized_ = false;
};

#endif
