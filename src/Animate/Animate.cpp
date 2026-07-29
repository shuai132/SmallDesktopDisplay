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
  frame.firstPatch = 0;
  frame.patchCount = 0;
  return true;
#elif Animate_Choice == 2
  constexpr int16_t frameCount = 32;
  frameIndex_ = (frameIndex_ + 1) % frameCount;
  frame.data = hutao[frameIndex_];
  frame.size = hutao_size[frameIndex_];
  frame.firstPatch = 0;
  frame.patchCount = 0;
  return true;
#elif Animate_Choice == 3
  if (!initialized_)
  {
    initialized_ = true;
    frameIndex_ = 0;
    frame.data = longmaoKeyframe;
    frame.size = sizeof(longmaoKeyframe);
    frame.firstPatch = 0;
    frame.patchCount = 0;
    return true;
  }

  frameIndex_ = (frameIndex_ + 1) % LONGMAO_FRAME_COUNT;
  frame.data = nullptr;
  frame.size = 0;
  frame.firstPatch = pgm_read_word(&longmaoFramePatchStarts[frameIndex_]);
  frame.patchCount = pgm_read_byte(&longmaoFramePatchCounts[frameIndex_]);
  return true;
#else
  (void)frame;
  return false;
#endif
}

bool AnimationPlayer::getPatch(uint16_t patchIndex, AnimationPatch &patch) const
{
#if Animate_Choice == 3
  if (patchIndex >= LONGMAO_PATCH_COUNT)
    return false;

  patch.data = reinterpret_cast<const uint8_t *>(
      pgm_read_ptr(&longmaoPatches[patchIndex].data));
  patch.size = pgm_read_dword(&longmaoPatches[patchIndex].size);
  patch.x = static_cast<int16_t>(pgm_read_word(&longmaoPatches[patchIndex].x));
  patch.y = static_cast<int16_t>(pgm_read_word(&longmaoPatches[patchIndex].y));
  return true;
#else
  (void)patchIndex;
  (void)patch;
  return false;
#endif
}
