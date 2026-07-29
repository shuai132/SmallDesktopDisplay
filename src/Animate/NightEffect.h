#ifndef NIGHT_EFFECT_H
#define NIGHT_EFFECT_H

#include <Arduino.h>

class TFT_eSPI;

class NightEffect
{
public:
  void update(TFT_eSPI &tft, uint32_t now);

private:
  static constexpr uint8_t STAR_COUNT = 64;
  static constexpr uint8_t STATIC_STAR_COUNT = 20;
  static constexpr uint8_t METEOR_COUNT = 10;

  struct Star
  {
    uint8_t x;
    uint8_t y;
    uint8_t phase;
  };

  struct Meteor
  {
    int16_t x;
    int16_t y;
    int8_t vx;
    int8_t vy;
    uint32_t nextStepAt;
    uint32_t nextLaunchAt;
    bool active;
  };

  void begin(TFT_eSPI &tft, uint32_t now);
  void initializeStars();
  void initializeMeteors(uint32_t now);
  void drawStaticSky(TFT_eSPI &tft);
  void updateTwinkles(TFT_eSPI &tft, uint32_t now);
  void updateMeteors(TFT_eSPI &tft, uint32_t now);
  void launchMeteor(Meteor &meteor, uint8_t index, uint32_t now);
  void eraseMeteor(TFT_eSPI &tft, const Meteor &meteor);
  void drawMeteor(TFT_eSPI &tft, const Meteor &meteor, uint8_t index);
  void drawStar(TFT_eSPI &tft, Star &star);

  Star stars_[STAR_COUNT];
  Meteor meteors_[METEOR_COUNT];
  bool started_ = false;
  uint8_t nextStar_ = STATIC_STAR_COUNT;
  uint32_t nextTwinkleAt_ = 0;
  uint32_t randomState_ = 0x72A4B91D;
};

#endif
