#include "Animate.h"
#include "config.h"

#if Animate_Choice == 1
#include "img/astronaut.h"
#elif Animate_Choice == 2
#include "img/hutao.h"
#elif Animate_Choice == 3
#include "img/longmao.h"
#endif

bool AnimationPlayer::nextFrame(AnimationFrame &frame)
{
#if Animate_Choice == 1
  constexpr int16_t frameCount = 10;
  frameIndex_ = (frameIndex_ + 1) % frameCount;
  frame.data = astronaut[frameIndex_];
  frame.size = astronaut_size[frameIndex_];
  return true;
#elif Animate_Choice == 2
  constexpr int16_t frameCount = 32;
  frameIndex_ = (frameIndex_ + 1) % frameCount;
  frame.data = hutao[frameIndex_];
  frame.size = hutao_size[frameIndex_];
  return true;
#elif Animate_Choice == 3
  frameIndex_ = (frameIndex_ + 1) % LONGMAO_FRAME_COUNT;
  frame.data = reinterpret_cast<const uint8_t *>(
      pgm_read_ptr(&longmaoFrames[frameIndex_]));
  frame.size = pgm_read_dword(&longmaoFrameSizes[frameIndex_]);
  return true;
#else
  (void)frame;
  return false;
#endif
}
