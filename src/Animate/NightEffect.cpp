#include "NightEffect.h"

#include <TFT_eSPI.h>

namespace
{
constexpr uint16_t NIGHT_SKY_COLOR = 0x0842;
constexpr uint16_t STAR_DIM = 0x3186;
constexpr uint16_t STAR_MEDIUM = 0x8410;
constexpr uint16_t STAR_BRIGHT = 0xFFFF;
constexpr uint16_t STAR_WARM = 0xFF59;
constexpr uint16_t MOON_COLOR = 0xFFE0;
constexpr uint16_t METEOR_COLORS[] = {0xFFE0, 0x07FF, 0xFD5F};
constexpr uint16_t METEOR_TAIL_COLORS[] = {0x8410, 0x4A69, 0x820C};
constexpr uint8_t SKY_TOP = 5;
constexpr uint8_t SKY_BOTTOM = 208;

bool overlapsClock(uint8_t x, uint8_t y)
{
  if (y > 92)
    return false;

  if ((x >= 18 && x <= 97) || (x >= 99 && x <= 178))
    return true;

  return y >= 28 && y <= 62 && x >= 180 && x <= 222;
}
}

void NightEffect::update(TFT_eSPI &tft, uint32_t now)
{
  if (!started_)
    begin(tft, now);

  updateTwinkles(tft, now);
  updateMeteors(tft, now);
}

void NightEffect::begin(TFT_eSPI &tft, uint32_t now)
{
  tft.fillScreen(NIGHT_SKY_COLOR);
  initializeStars();
  initializeMeteors(now);
  drawStaticSky(tft);

  nextTwinkleAt_ = now + 70;
  started_ = true;
}

void NightEffect::initializeStars()
{
  for (uint8_t index = 0; index < STAR_COUNT; index++)
  {
    bool invalidPosition;
    do
    {
      randomState_ = randomState_ * 1664525UL + 1013904223UL;
      stars_[index].x = 7 + (randomState_ % 226);
      randomState_ = randomState_ * 1664525UL + 1013904223UL;
      stars_[index].y =
          index < STATIC_STAR_COUNT
              ? SKY_TOP + (randomState_ % 85)
              : SKY_TOP + (randomState_ % (SKY_BOTTOM - SKY_TOP));

      const int16_t moonDx = static_cast<int16_t>(stars_[index].x) - 32;
      const int16_t moonDy = static_cast<int16_t>(stars_[index].y) - 128;
      invalidPosition = moonDx * moonDx + moonDy * moonDy < 24 * 24;
      if (index >= STATIC_STAR_COUNT)
        invalidPosition = invalidPosition ||
                          overlapsClock(stars_[index].x, stars_[index].y);
    } while (invalidPosition);

    stars_[index].phase = (randomState_ >> 24) % 4;
  }
}

void NightEffect::initializeMeteors(uint32_t now)
{
  for (uint8_t index = 0; index < METEOR_COUNT; index++)
  {
    meteors_[index].active = false;
    meteors_[index].nextLaunchAt = now + 700 + index * 1050;
    meteors_[index].nextStepAt = now;
  }
}

void NightEffect::drawStaticSky(TFT_eSPI &tft)
{
  for (uint8_t index = 0; index < STAR_COUNT; index++)
    drawStar(tft, stars_[index]);

  tft.fillCircle(31, 128, 17, MOON_COLOR);
  tft.fillCircle(38, 122, 17, NIGHT_SKY_COLOR);

  // A quiet horizon gives the lower half depth without requiring animation.
  tft.fillTriangle(0, 239, 62, 211, 124, 239, 0x1082);
  tft.fillTriangle(74, 239, 151, 216, 224, 239, 0x1082);
  tft.fillTriangle(164, 239, 211, 220, 239, 239, 0x18C3);
}

void NightEffect::updateTwinkles(TFT_eSPI &tft, uint32_t now)
{
  if (static_cast<int32_t>(now - nextTwinkleAt_) < 0)
    return;

  for (uint8_t count = 0; count < 3; count++)
  {
    Star &star = stars_[nextStar_];
    star.phase = (star.phase + 1 + (nextStar_ & 1)) % 4;
    drawStar(tft, star);
    nextStar_ = STATIC_STAR_COUNT +
                ((nextStar_ - STATIC_STAR_COUNT + 17) %
                 (STAR_COUNT - STATIC_STAR_COUNT));
  }
  nextTwinkleAt_ += 70;
}

void NightEffect::updateMeteors(TFT_eSPI &tft, uint32_t now)
{
  for (uint8_t index = 0; index < METEOR_COUNT; index++)
  {
    Meteor &meteor = meteors_[index];
    if (!meteor.active)
    {
      if (static_cast<int32_t>(now - meteor.nextLaunchAt) >= 0)
        launchMeteor(meteor, index, now);
      continue;
    }

    if (static_cast<int32_t>(now - meteor.nextStepAt) < 0)
      continue;

    eraseMeteor(tft, meteor);
    meteor.x += meteor.vx;
    meteor.y += meteor.vy;
    meteor.nextStepAt += 40;

    if (meteor.x - meteor.tailLength > 244 || meteor.y > 211)
    {
      meteor.active = false;
      meteor.nextLaunchAt = now + 2100 + index * 650 + (randomState_ % 1200);
      continue;
    }

    drawMeteor(tft, meteor, index);
  }
}

void NightEffect::launchMeteor(Meteor &meteor, uint8_t index, uint32_t now)
{
  randomState_ = randomState_ * 1664525UL + 1013904223UL;
  meteor.x = -24 - index * 11;
  meteor.y = 150 + (randomState_ % 28);
  meteor.vx = 6 + index;
  meteor.vy = 2 + (index & 1);
  meteor.tailLength = 18 + index * 5;
  meteor.nextStepAt = now;
  meteor.active = true;
}

void NightEffect::eraseMeteor(TFT_eSPI &tft, const Meteor &meteor)
{
  const int16_t tailX = meteor.x - meteor.tailLength;
  const int16_t tailY =
      meteor.y - (meteor.tailLength * meteor.vy) / meteor.vx;
  tft.drawLine(tailX, tailY, meteor.x, meteor.y, NIGHT_SKY_COLOR);
  tft.drawLine(tailX, tailY + 1, meteor.x, meteor.y + 1, NIGHT_SKY_COLOR);
}

void NightEffect::drawMeteor(TFT_eSPI &tft, const Meteor &meteor, uint8_t index)
{
  const int16_t tailX = meteor.x - meteor.tailLength;
  const int16_t tailY =
      meteor.y - (meteor.tailLength * meteor.vy) / meteor.vx;
  const int16_t coreX = meteor.x - meteor.tailLength / 3;
  const int16_t coreY =
      meteor.y - ((meteor.tailLength / 3) * meteor.vy) / meteor.vx;

  tft.drawLine(
      tailX,
      tailY,
      meteor.x,
      meteor.y,
      METEOR_TAIL_COLORS[index]);
  tft.drawLine(
      coreX,
      coreY,
      meteor.x,
      meteor.y,
      METEOR_COLORS[index]);
  tft.drawPixel(meteor.x, meteor.y + 1, STAR_BRIGHT);
}

void NightEffect::drawStar(TFT_eSPI &tft, Star &star)
{
  tft.drawPixel(star.x - 1, star.y, NIGHT_SKY_COLOR);
  tft.drawPixel(star.x + 1, star.y, NIGHT_SKY_COLOR);
  tft.drawPixel(star.x, star.y - 1, NIGHT_SKY_COLOR);
  tft.drawPixel(star.x, star.y + 1, NIGHT_SKY_COLOR);

  const uint16_t color =
      star.phase == 0 ? STAR_DIM
      : star.phase == 1 ? STAR_MEDIUM
      : star.phase == 2 ? STAR_BRIGHT
                        : STAR_WARM;
  tft.drawPixel(star.x, star.y, color);
  if (star.phase >= 2)
  {
    tft.drawPixel(star.x - 1, star.y, color);
    tft.drawPixel(star.x + 1, star.y, color);
    tft.drawPixel(star.x, star.y - 1, color);
    tft.drawPixel(star.x, star.y + 1, color);
  }
}
