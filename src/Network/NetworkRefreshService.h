#ifndef NETWORK_REFRESH_SERVICE_H
#define NETWORK_REFRESH_SERVICE_H

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <asyncHTTPrequest.h>

class NetworkRefreshService
{
public:
  using WeatherHandler = void (*)(const String &response);

  explicit NetworkRefreshService(WiFiUDP &udp) : udp_(udp)
  {
  }

  void start(String &cityCode);
  void service(String &cityCode, WeatherHandler weatherHandler);
  bool isActive() const
  {
    return active_;
  }

private:
  enum class WeatherState
  {
    Idle,
    CityRequest,
    WeatherRequest
  };

  static constexpr size_t ntpPacketSize_ = 48;

  void startWeatherRequest(const String &url, WeatherState state);
  void startNtpRequest();
  void serviceWeather(String &cityCode, WeatherHandler weatherHandler);
  void serviceNtp();

  WiFiUDP &udp_;
  asyncHTTPrequest weatherRequest_;
  WeatherState weatherState_ = WeatherState::Idle;
  bool active_ = false;
  bool weatherDone_ = true;
  bool ntpDone_ = true;
  bool ntpWaiting_ = false;
  uint32_t ntpStartedAt_ = 0;
  byte ntpPacket_[ntpPacketSize_];
};

#endif
