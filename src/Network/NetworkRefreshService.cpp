#include "NetworkRefreshService.h"

#include <ESP8266HTTPClient.h>
#include <TimeLib.h>

#include "config.h"

namespace
{
const char *const ntpServerNames[] = {
    "ntp.aliyun.com",
    "ntp.tencent.com",
    "pool.ntp.org"};
constexpr size_t ntpServerCount = sizeof(ntpServerNames) / sizeof(ntpServerNames[0]);
constexpr int timeZone = 8;
constexpr uint32_t ntpTimeoutMs = 3000;
}

void NetworkRefreshService::startWeatherRequest(const String &url, WeatherState state)
{
  if (!weatherRequest_.open("GET", url.c_str()))
  {
    Serial.println("Unable to start weather request");
    weatherDone_ = true;
    weatherState_ = WeatherState::Idle;
    return;
  }

  weatherRequest_.setTimeout(3);
  weatherRequest_.setReqHeader("User-Agent", "Mozilla/5.0");
  weatherRequest_.setReqHeader("Referer", "http://www.weather.com.cn/");
  weatherState_ = state;
  if (!weatherRequest_.send())
  {
    Serial.println("Unable to send weather request");
    weatherDone_ = true;
    weatherState_ = WeatherState::Idle;
  }
}

bool NetworkRefreshService::startNtpRequest()
{
  while (ntpServerIndex_ < ntpServerCount)
  {
    const char *serverName = ntpServerNames[ntpServerIndex_];
    IPAddress serverIp;
    if (!WiFi.hostByName(serverName, serverIp, 1000))
    {
      Serial.print("NTP DNS lookup failed: ");
      Serial.println(serverName);
      ntpServerIndex_++;
      continue;
    }

    memset(ntpPacket_, 0, sizeof(ntpPacket_));
    ntpPacket_[0] = 0b11100011;
    ntpPacket_[2] = 6;
    ntpPacket_[3] = 0xEC;
    ntpPacket_[12] = 49;
    ntpPacket_[13] = 0x4E;
    ntpPacket_[14] = 49;
    ntpPacket_[15] = 52;

    udp_.beginPacket(serverIp, 123);
    udp_.write(ntpPacket_, sizeof(ntpPacket_));
    udp_.endPacket();
    ntpStartedAt_ = millis();
    ntpWaiting_ = true;
    Serial.print("NTP request sent: ");
    Serial.println(serverName);
    return true;
  }

  ntpDone_ = true;
  ntpWaiting_ = false;
  Serial.println("All NTP servers failed");
  return false;
}

void NetworkRefreshService::start(String &cityCode)
{
  active_ = true;
  weatherDone_ = false;
  ntpDone_ = false;
  ntpWaiting_ = false;
  ntpServerIndex_ = 0;

  const int cityNumber = cityCode.toInt();
  if (cityNumber >= 101000000 && cityNumber <= 102000000)
  {
    const String url = "http://d1.weather.com.cn/weather_index/" + cityCode + ".html?_=" + String(now());
    startWeatherRequest(url, WeatherState::WeatherRequest);
  }
  else
  {
    const String url = "http://wgeo.weather.com.cn/ip/?_=" + String(now());
    startWeatherRequest(url, WeatherState::CityRequest);
  }

  startNtpRequest();
}

void NetworkRefreshService::serviceWeather(String &cityCode, WeatherHandler weatherHandler)
{
  if (weatherDone_ || weatherRequest_.readyState() != 4)
    return;

  if (weatherRequest_.responseHTTPcode() != HTTP_CODE_OK)
  {
    Serial.print("Async weather request failed: ");
    Serial.println(weatherRequest_.responseHTTPcode());
    weatherDone_ = true;
    weatherState_ = WeatherState::Idle;
    return;
  }

  const String response = weatherRequest_.responseText();
  if (weatherState_ == WeatherState::CityRequest)
  {
    const int cityIndex = response.indexOf("id=");
    if (cityIndex < 0)
    {
      Serial.println("Unable to find city code");
      weatherDone_ = true;
      weatherState_ = WeatherState::Idle;
      return;
    }

    cityCode = response.substring(cityIndex + 4, cityIndex + 13);
    const String url = "http://d1.weather.com.cn/weather_index/" + cityCode + ".html?_=" + String(now());
    startWeatherRequest(url, WeatherState::WeatherRequest);
    return;
  }

  weatherHandler(response);
  weatherDone_ = true;
  weatherState_ = WeatherState::Idle;
}

bool NetworkRefreshService::serviceNtp()
{
  if (ntpDone_ || !ntpWaiting_)
    return false;

  const int size = udp_.parsePacket();
  if (size >= static_cast<int>(ntpPacketSize_))
  {
    udp_.read(ntpPacket_, sizeof(ntpPacket_));
    unsigned long secondsSince1900 = static_cast<unsigned long>(ntpPacket_[40]) << 24;
    secondsSince1900 |= static_cast<unsigned long>(ntpPacket_[41]) << 16;
    secondsSince1900 |= static_cast<unsigned long>(ntpPacket_[42]) << 8;
    secondsSince1900 |= static_cast<unsigned long>(ntpPacket_[43]);
    setTime(secondsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR);
    Serial.println("NTP time synchronized");
    ntpDone_ = true;
    ntpWaiting_ = false;
    return true;
  }

  if (millis() - ntpStartedAt_ >= ntpTimeoutMs)
  {
    Serial.print("No NTP response: ");
    Serial.println(ntpServerNames[ntpServerIndex_]);
    ntpWaiting_ = false;
    ntpServerIndex_++;
    startNtpRequest();
  }

  return false;
}

bool NetworkRefreshService::service(String &cityCode, WeatherHandler weatherHandler)
{
  if (!active_)
    return false;

  serviceWeather(cityCode, weatherHandler);
  const bool timeUpdated = serviceNtp();

  if (!weatherDone_ || !ntpDone_)
    return timeUpdated;

#if !OTA_EN
  WiFi.forceSleepBegin();
  Serial.println("WIFI sleep......");
#endif
  active_ = false;
  return timeUpdated;
}
