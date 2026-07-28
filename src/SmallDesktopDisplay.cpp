/* *****************************************************************
 *
 * SmallDesktopDisplay
 *    小型桌面显示器
 *
 * 原  作  者：Misaka
 * 修      改：微车游
 * 再次  修改：丘山鹤
 * 讨  论  群：811058758、887171863、720661626
 * 创 建 日 期：2021.07.19
 * 最后更改日期：2022.1.17
 *
 *
 * 引 脚 分 配：SCK   GPIO14
 *              MOSI  GPIO13
 *              RES   GPIO2
 *              DC    GPIO0
 *              LCDBL GPIO5
 *
 *             增加DHT11温湿度传感器，传感器接口为 GPIO 12
 *
 *    感谢群友 @你别失望  提醒发现WiFi保存后无法重置的问题，目前已解决。详情查看更改说明！
 * *****************************************************************/

/* *****************************************************************
 *  库文件、头文件
 * *****************************************************************/
#include <ArduinoJson.h>
#include <TimeLib.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>
#include <WiFiUdp.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <TJpg_Decoder.h>
#include <EEPROM.h>                 //内存
#include <Button2.h>                //按钮库

#include "config.h"                //配置文件
#include "weatherNum/weatherNum.h" //天气图库
#include "Animate/Animate.h"       //动画模块
#include "Network/NetworkRefreshService.h"
#include "Scheduler/PeriodicTask.h"

#define Version "SDD V1.4.3"
/* *****************************************************************
 *  配置使能位
 * *****************************************************************/

#if WM_EN
#include <WiFiManager.h>
// WiFiManager 参数
WiFiManager wm; // global wm instance
// WiFiManagerParameter custom_field; // global param ( for non blocking w params )
#endif

#if DHT_EN
#include "DHT.h"
#define DHTPIN 12
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);
#endif

//定义按钮引脚
Button2 Button_sw1 = Button2(4);

/* *****************************************************************
 *  字库、图片库
 * *****************************************************************/
#include "font/ZdyLwFont_20.h"  //字体库
#include "font/timeClockFont.h" //字体库
#include "img/temperature.h"    //温度图标
#include "img/humidity.h"       //湿度图标

//函数声明
void digitalClockDisplay(bool forceRefresh = false);
void printDigits(int digits);
String num2str(int digits);
void refreshDisplay();
void savewificonfig();         // wifi ssid，psw保存到eeprom
void readwificonfig();         //从eeprom读取WiFi信息ssid，psw
void deletewificonfig();       //删除原有eeprom中的信息
void parseWeatherResponse(const String &response);
void wifi_reset(Button2 &btn); // WIFI重设
void saveParamCallback();
void esp_reset(Button2 &btn);
void scrollBanner();
void renderWeatherData(String *cityDZ, String *dataSK, String *dataFC);
void refresh_AnimatedImage();                                    //更新右下角
void drawMainScreen();
void showWifiStatus(const String &status);
void refreshConnectingScreen();
void refreshTime();
void refreshBanner();
void wakeWifi();
void serviceScheduledTasks();
void setWeatherUpdateInterval();

/* *****************************************************************
 *  参数设置
 * *****************************************************************/
struct config_type
{
  char stassid[32]; //定义配网得到的WIFI名长度(最大32字节)
  char stapsw[64];  //定义配网得到的WIFI密码长度(最大64字节)
};
//---------------修改此处""内的信息--------------------
//如开启WEB配网则可不用设置这里的参数，前一个为wifi ssid，后一个为密码
config_type wificonf = {{"WiFi名"}, {"密码"}};

//天气更新时间  X 分钟
unsigned int weatherUpdateMinutes = 1;

//----------------------------------------------------

// LCD屏幕相关设置
TFT_eSPI tft = TFT_eSPI(); // 引脚请自行配置tft_espi库中的 User_Setup.h文件
TFT_eSprite clk = TFT_eSprite(&tft);
#define LCD_BL_PIN 5 // LCD背光引脚
uint16_t bgColor = 0x0000;

//其余状态标志位
int LCD_Rotation = 0;        // LCD屏幕方向
int LCD_BL_PWM = 50;         //屏幕亮度0-100，默认50
bool wifiRefreshRequested = true;
int DHT_img_flag = 0; // DHT传感器使用标志位

// EEPROM参数存储地址位
int BL_addr = 1;    //被写入数据的EEPROM地址编号  1亮度
int Ro_addr = 2;    //被写入数据的EEPROM地址编号  2 旋转方向
int DHT_addr = 3;   // 3 DHT使能标志位
int CC_addr = 10;   //被写入数据的EEPROM地址编号  10城市
int wifi_addr = 30; //被写入数据的EEPROM地址编号  20wifi-ssid-psw

/*** Component objects ***/
WeatherNum wrat;
AnimationPlayer animationPlayer;

String cityCode = "101090609"; //天气城市代码
int tempnum = 0;               //温度百分比
int huminum = 0;               //湿度百分比
int tempcol = 0xffff;          //温度显示颜色
int humicol = 0xffff;          //湿度显示颜色

// wifi连接UDP设置参数
WiFiUDP Udp;
unsigned int localPort = 8000;
NetworkRefreshService networkRefresh(Udp);

//星期
String week()
{
  String wk[7] = {"日", "一", "二", "三", "四", "五", "六"};
  String s = "周" + wk[weekday() - 1];
  return s;
}

//月日
String monthDay()
{
  String s = String(month());
  s = s + "月" + day() + "日";
  return s;
}

/* *****************************************************************
 *  函数
 * *****************************************************************/

// wifi ssid，psw保存到eeprom
void savewificonfig()
{
  //开始写入
  uint8_t *p = (uint8_t *)(&wificonf);
  for (unsigned int i = 0; i < sizeof(wificonf); i++)
  {
    EEPROM.write(i + wifi_addr, *(p + i)); //在闪存内模拟写入
  }
  delay(10);
  EEPROM.commit(); //执行写入ROM
  delay(10);
}

// TFT屏幕输出函数
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap)
{
  if (y >= tft.height())
    return 0;
  tft.pushImage(x, y, w, h, bitmap);
  // Return 1 to decode next block
  return 1;
}

//在主界面的信息栏显示 WiFi 状态
void showWifiStatus(const String &status)
{
  clk.setColorDepth(8);
  clk.createSprite(150, 30);
  clk.fillSprite(bgColor);
  clk.setTextDatum(CC_DATUM);
  clk.setTextColor(TFT_GREEN, bgColor);
  clk.drawString(status, 74, 16, 2);
  clk.pushSprite(10, 45);
  clk.deleteSprite();
}

// Keep the live parts of the main screen moving during blocking WiFi setup.
void refreshConnectingScreen()
{
  static uint32_t lastClockRefresh = 0;
  const uint32_t currentTime = millis();
  if (currentTime - lastClockRefresh >= 300)
  {
    lastClockRefresh = currentTime;
    digitalClockDisplay();
  }
  refresh_AnimatedImage();
  yield();
}

//先绘制主界面，联网和数据更新在界面显示后继续进行
void drawMainScreen()
{
  tft.fillScreen(TFT_BLACK);
  refresh_AnimatedImage();
}

//湿度图标显示函数
void humidityWin()
{
  clk.setColorDepth(8);

  huminum = huminum / 2;
  clk.createSprite(52, 6);                         //创建窗口
  clk.fillSprite(0x0000);                          //填充率
  clk.drawRoundRect(0, 0, 52, 6, 3, 0xFFFF);       //空心圆角矩形  起始位x,y,长度，宽度，圆弧半径，颜色
  clk.fillRoundRect(1, 1, huminum, 4, 2, humicol); //实心圆角矩形
  clk.pushSprite(45, 222);                         //窗口位置
  clk.deleteSprite();
}

//温度图标显示函数
void tempWin()
{
  clk.setColorDepth(8);

  clk.createSprite(52, 6);                         //创建窗口
  clk.fillSprite(0x0000);                          //填充率
  clk.drawRoundRect(0, 0, 52, 6, 3, 0xFFFF);       //空心圆角矩形  起始位x,y,长度，宽度，圆弧半径，颜色
  clk.fillRoundRect(1, 1, tempnum, 4, 2, tempcol); //实心圆角矩形
  clk.pushSprite(45, 192);                         //窗口位置
  clk.deleteSprite();
}

#if DHT_EN
//外接DHT11传感器，显示数据
void IndoorTem()
{
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  String s = "内温";
  /***绘制相关文字***/
  clk.setColorDepth(8);
  clk.loadFont(ZdyLwFont_20);

  //位置
  clk.createSprite(58, 30);
  clk.fillSprite(bgColor);
  clk.setTextDatum(CC_DATUM);
  clk.setTextColor(TFT_WHITE, bgColor);
  clk.drawString(s, 29, 16);
  clk.pushSprite(172, 150);
  clk.deleteSprite();

  //温度
  clk.createSprite(60, 24);
  clk.fillSprite(bgColor);
  clk.setTextDatum(CC_DATUM);
  clk.setTextColor(TFT_WHITE, bgColor);
  clk.drawFloat(t, 1, 20, 13);
  //  clk.drawString(sk["temp"].as<String>()+"℃",28,13);
  clk.drawString("℃", 50, 13);
  clk.pushSprite(170, 184);
  clk.deleteSprite();

  //湿度
  clk.createSprite(60, 24);
  clk.fillSprite(bgColor);
  clk.setTextDatum(CC_DATUM);
  clk.setTextColor(TFT_WHITE, bgColor);
  //  clk.drawString(sk["SD"].as<String>(),28,13);
  clk.drawFloat(h, 1, 20, 13);
  clk.drawString("%", 50, 13);
  // clk.drawString("100%",28,13);
  clk.pushSprite(170, 214);
  clk.deleteSprite();
}
#endif

#if !WM_EN
//微信配网函数
void SmartConfig(void)
{
  WiFi.mode(WIFI_STA); //设置STA模式
  // tft.pushImage(0, 0, 240, 240, qr);
  tft.pushImage(0, 0, 240, 240, qr);
  Serial.println("\r\nWait for Smartconfig..."); //打印log信息
  WiFi.beginSmartConfig();                       //开始SmartConfig，等待手机端发出用户名和密码
  while (1)
  {
    Serial.print(".");
    delay(100);                 // wait for a second
    if (WiFi.smartConfigDone()) //配网成功，接收到SSID和密码
    {
      Serial.println("SmartConfig Success");
      Serial.printf("SSID:%s\r\n", WiFi.SSID().c_str());
      Serial.printf("PSW:%s\r\n", WiFi.psk().c_str());
      break;
    }
  }
}
#endif

String SMOD = ""; // 0亮度
//串口调试设置函数
void serviceSerialCommands()
{
  String incomingByte = "";
  if (Serial.available() > 0)
  {
    while (Serial.available() > 0) //监测串口缓存，当有数据输入时，循环赋值给incomingByte
    {
      incomingByte += char(Serial.read()); //读取单个字符值，转换为字符，并按顺序一个个赋值给incomingByte
      delay(2);                            //不能省略，因为读取缓冲区数据需要时间
    }
    if (SMOD == "0x01") //设置1亮度设置
    {
      int LCDBL = atoi(incomingByte.c_str()); // int n = atoi(xxx.c_str());//String转int
      if (LCDBL >= 0 && LCDBL <= 100)
      {
        EEPROM.write(BL_addr, LCDBL); //亮度地址写入亮度值
        EEPROM.commit();              //保存更改的数据
        delay(5);
        LCD_BL_PWM = EEPROM.read(BL_addr);
        delay(5);
        SMOD = "";
        Serial.printf("亮度调整为：");
        analogWrite(LCD_BL_PIN, 1023 - (LCD_BL_PWM * 10));
        Serial.println(LCD_BL_PWM);
        Serial.println("");
      }
      else
        Serial.println("亮度调整错误，请输入0-100");
    }
    if (SMOD == "0x02") //设置2地址设置
    {
      int CityCODE = 0;
      int CityC = atoi(incomingByte.c_str()); // int n = atoi(xxx.c_str());//String转int
      if (((CityC >= 101000000) && (CityC <= 102000000)) || (CityC == 0))
      {
        for (int cnum = 0; cnum < 5; cnum++)
        {
          EEPROM.write(CC_addr + cnum, CityC % 100); //城市地址写入城市代码
          EEPROM.commit();                           //保存更改的数据
          CityC = CityC / 100;
          delay(5);
        }
        for (int cnum = 5; cnum > 0; cnum--)
        {
          CityCODE = CityCODE * 100;
          CityCODE += EEPROM.read(CC_addr + cnum - 1);
          delay(5);
        }

        cityCode = CityCODE;

        if (cityCode == "0")
        {
          Serial.println("城市代码调整为：自动");
        }
        else
        {
          Serial.printf("城市代码调整为：");
          Serial.println(cityCode);
        }
        Serial.println("");
        wifiRefreshRequested = true;
        SMOD = "";
      }
      else
        Serial.println("城市调整错误，请输入9位城市代码，自动获取请输入0");
    }
    if (SMOD == "0x03") //设置3屏幕显示方向
    {
      int RoSet = atoi(incomingByte.c_str());
      if (RoSet >= 0 && RoSet <= 3)
      {
        EEPROM.write(Ro_addr, RoSet); //屏幕方向地址写入方向值
        EEPROM.commit();              //保存更改的数据
        SMOD = "";
        //设置屏幕方向后重新刷屏并显示
        tft.setRotation(RoSet);
        tft.fillScreen(0x0000);
        refreshDisplay();
        TJpgDec.drawJpg(15, 183, temperature, sizeof(temperature)); //温度图标
        TJpgDec.drawJpg(15, 213, humidity, sizeof(humidity));       //湿度图标

        Serial.print("屏幕方向设置为：");
        Serial.println(RoSet);
      }
      else
      {
        Serial.println("屏幕方向值错误，请输入0-3内的值");
      }
    }
    if (SMOD == "0x04") //设置天气更新时间
    {
      int wtup = atoi(incomingByte.c_str()); // int n = atoi(xxx.c_str());//String转int
      if (wtup >= 1 && wtup <= 60)
      {
        weatherUpdateMinutes = wtup;
        setWeatherUpdateInterval();
        SMOD = "";
        Serial.printf("天气更新时间更改为：");
        Serial.print(weatherUpdateMinutes);
        Serial.println("分钟");
      }
      else
        Serial.println("更新时间太长，请重新设置（1-60）");
    }
    else
    {
      SMOD = incomingByte;
      delay(2);
      if (SMOD == "0x01")
        Serial.println("请输入亮度值，范围0-100");
      else if (SMOD == "0x02")
        Serial.println("请输入9位城市代码，自动获取请输入0");
      else if (SMOD == "0x03")
      {
        Serial.println("请输入屏幕方向值，");
        Serial.println("0-USB接口朝下");
        Serial.println("1-USB接口朝右");
        Serial.println("2-USB接口朝上");
        Serial.println("3-USB接口朝左");
      }
      else if (SMOD == "0x04")
      {
        Serial.print("当前天气更新时间：");
        Serial.print(weatherUpdateMinutes);
        Serial.println("分钟");
        Serial.println("请输入天气更新时间（1-60）分钟");
      }
      else if (SMOD == "0x05")
      {
        Serial.println("重置WiFi设置中......");
        delay(10);
        wm.resetSettings();
        deletewificonfig();
        delay(10);
        Serial.println("重置WiFi成功");
        SMOD = "";
        ESP.restart();
      }
      else
      {
        Serial.println("");
        Serial.println("请输入需要修改的代码：");
        Serial.println("亮度设置输入        0x01");
        Serial.println("地址设置输入        0x02");
        Serial.println("屏幕方向设置输入    0x03");
        Serial.println("更改天气更新时间    0x04");
        Serial.println("重置WiFi(会重启)    0x05");
        Serial.println("");
      }
    }
  }
}

#if WM_EN
// WEB配网LCD显示函数
void Web_win()
{
  clk.setColorDepth(8);

  clk.createSprite(200, 60); //创建窗口
  clk.fillSprite(0x0000);    //填充率

  clk.setTextDatum(CC_DATUM); //设置文本数据
  clk.setTextColor(TFT_GREEN, 0x0000);
  clk.drawString("WiFi Connect Fail!", 100, 10, 2);
  clk.drawString("SSID:", 45, 40, 2);
  clk.setTextColor(TFT_WHITE, 0x0000);
  clk.drawString("AutoConnectAP", 125, 40, 2);
  clk.pushSprite(20, 50); //窗口位置

  clk.deleteSprite();
}

// WEB配网函数
void Webconfig()
{
  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP

  delay(3000);
  wm.resetSettings(); // wipe settings

  // add a custom input field
  // int customFieldLength = 40;

  // new (&custom_field) WiFiManagerParameter("customfieldid", "Custom Field Label", "Custom Field Value", customFieldLength,"placeholder=\"Custom Field Placeholder\"");

  // test custom html input type(checkbox)
  //  new (&custom_field) WiFiManagerParameter("customfieldid", "Custom Field Label", "Custom Field Value", customFieldLength,"placeholder=\"Custom Field Placeholder\" type=\"checkbox\""); // custom html type

  // test custom html(radio)
  // const char* custom_radio_str = "<br/><label for='customfieldid'>Custom Field Label</label><input type='radio' name='customfieldid' value='1' checked> One<br><input type='radio' name='customfieldid' value='2'> Two<br><input type='radio' name='customfieldid' value='3'> Three";
  // new (&custom_field) WiFiManagerParameter(custom_radio_str); // custom html input

  const char *set_rotation = "<br/><label for='set_rotation'>显示方向设置</label>\
                              <input type='radio' name='set_rotation' value='0' checked> USB接口朝下<br>\
                              <input type='radio' name='set_rotation' value='1'> USB接口朝右<br>\
                              <input type='radio' name='set_rotation' value='2'> USB接口朝上<br>\
                              <input type='radio' name='set_rotation' value='3'> USB接口朝左<br>";
  WiFiManagerParameter custom_rot(set_rotation); // custom html input
  WiFiManagerParameter custom_bl("LCDBL", "屏幕亮度（1-100）", "10", 3);
#if DHT_EN
  WiFiManagerParameter custom_DHT11_en("DHT11_en", "Enable DHT11 sensor", "0", 1);
#endif
  WiFiManagerParameter custom_weatertime("WeaterUpdateTime", "天气刷新时间（分钟）", "10", 3);
  WiFiManagerParameter custom_cc("CityCode", "城市代码", "0", 9);
  WiFiManagerParameter p_lineBreak_notext("<p></p>");

  // wm.addParameter(&p_lineBreak_notext);
  // wm.addParameter(&custom_field);
  wm.addParameter(&p_lineBreak_notext);
  wm.addParameter(&custom_cc);
  wm.addParameter(&p_lineBreak_notext);
  wm.addParameter(&custom_bl);
  wm.addParameter(&p_lineBreak_notext);
  wm.addParameter(&custom_weatertime);
  wm.addParameter(&p_lineBreak_notext);
  wm.addParameter(&custom_rot);
#if DHT_EN
  wm.addParameter(&p_lineBreak_notext);
  wm.addParameter(&custom_DHT11_en);
#endif
  wm.setSaveParamsCallback(saveParamCallback);

  // custom menu via array or vector
  //
  // menu tokens, "wifi","wifinoscan","info","param","close","sep","erase","restart","exit" (sep is seperator) (if param is in menu, params will not show up in wifi page!)
  // const char* menu[] = {"wifi","info","param","sep","restart","exit"};
  // wm.setMenu(menu,6);
  std::vector<const char *> menu = {"wifi", "restart"};
  wm.setMenu(menu);

  // set dark theme
  wm.setClass("invert");

  // set static ip
  //  wm.setSTAStaticIPConfig(IPAddress(10,0,1,99), IPAddress(10,0,1,1), IPAddress(255,255,255,0)); // set static ip,gw,sn
  //  wm.setShowStaticFields(true); // force show static ip fields
  //  wm.setShowDnsFields(true);    // force show dns field always

  // wm.setConnectTimeout(20); // how long to try to connect for before continuing
  //  wm.setConfigPortalTimeout(30); // auto close configportal after n seconds
  // wm.setCaptivePortalEnable(false); // disable captive portal redirection
  // wm.setAPClientCheck(true); // avoid timeout if client connected to softap

  // wifi scan settings
  // wm.setRemoveDuplicateAPs(false); // do not remove duplicate ap names (true)
  wm.setMinimumSignalQuality(20); // set min RSSI (percentage) to show in scans, null = 8%
  // wm.setShowInfoErase(false);      // do not show erase button on info page
  // wm.setScanDispPerc(true);       // show RSSI as percentage not graph icons

  // wm.setBreakAfterConfig(true);   // always exit configportal even if wifi save fails

  bool res;
  // res = wm.autoConnect(); // auto generated AP name from chipid
  res = wm.autoConnect("AutoConnectAP"); // anonymous ap
  //  res = wm.autoConnect("AutoConnectAP","password"); // password protected ap

  while (!res)
    ;
}

String getParam(String name)
{
  // read parameter from server, for customhmtl input
  String value;
  if (wm.server->hasArg(name))
  {
    value = wm.server->arg(name);
  }
  return value;
}

//删除原有eeprom中的信息
void deletewificonfig()
{
  config_type deletewifi = {{""}, {""}};
  uint8_t *p = (uint8_t *)(&deletewifi);
  for (unsigned int i = 0; i < sizeof(deletewifi); i++)
  {
    EEPROM.write(i + wifi_addr, *(p + i)); //在闪存内模拟写入
  }
  delay(10);
  EEPROM.commit(); //执行写入ROM
  delay(10);
}

//从eeprom读取WiFi信息ssid，psw
void readwificonfig()
{
  uint8_t *p = (uint8_t *)(&wificonf);
  for (unsigned int i = 0; i < sizeof(wificonf); i++)
  {
    *(p + i) = EEPROM.read(i + wifi_addr);
  }
  // EEPROM.commit();
  // ssid = wificonf.stassid;
  // pass = wificonf.stapsw;
  Serial.printf("Read WiFi Config.....\r\n");
  Serial.printf("SSID:%s\r\n", wificonf.stassid);
  Serial.printf("PSW:%s\r\n", wificonf.stapsw);
  Serial.printf("Connecting.....\r\n");
}

void saveParamCallback()
{
  int CCODE = 0, cc;

  Serial.println("[CALLBACK] saveParamCallback fired");
  // Serial.println("PARAM customfieldid = " + getParam("customfieldid"));
  // Serial.println("PARAM CityCode = " + getParam("CityCode"));
  // Serial.println("PARAM LCD BackLight = " + getParam("LCDBL"));
  // Serial.println("PARAM WeaterUpdateTime = " + getParam("WeaterUpdateTime"));
  // Serial.println("PARAM Rotation = " + getParam("set_rotation"));
  // Serial.println("PARAM DHT11_en = " + getParam("DHT11_en"));
//将从页面中获取的数据保存
#if DHT_EN
  DHT_img_flag = getParam("DHT11_en").toInt();
#endif
  weatherUpdateMinutes = getParam("WeaterUpdateTime").toInt();
  setWeatherUpdateInterval();
  cc = getParam("CityCode").toInt();
  LCD_Rotation = getParam("set_rotation").toInt();
  LCD_BL_PWM = getParam("LCDBL").toInt();

  //对获取的数据进行处理
  //城市代码
  Serial.print("CityCode = ");
  Serial.println(cc);
  if (((cc >= 101000000) && (cc <= 102000000)) || (cc == 0))
  {
    for (int cnum = 0; cnum < 5; cnum++)
    {
      EEPROM.write(CC_addr + cnum, cc % 100); //城市地址写入城市代码
      EEPROM.commit();                        //保存更改的数据
      cc = cc / 100;
      delay(5);
    }
    for (int cnum = 5; cnum > 0; cnum--)
    {
      CCODE = CCODE * 100;
      CCODE += EEPROM.read(CC_addr + cnum - 1);
      delay(5);
    }
    cityCode = CCODE;
  }
  //屏幕方向
  Serial.print("LCD_Rotation = ");
  Serial.println(LCD_Rotation);
  if (EEPROM.read(Ro_addr) != LCD_Rotation)
  {
    EEPROM.write(Ro_addr, LCD_Rotation);
    EEPROM.commit();
    delay(5);
  }
  tft.setRotation(LCD_Rotation);
  drawMainScreen();
  showWifiStatus("WiFi Connecting...");
  if (EEPROM.read(BL_addr) != LCD_BL_PWM)
  {
    EEPROM.write(BL_addr, LCD_BL_PWM);
    EEPROM.commit();
    delay(5);
  }
  // 屏幕亮度
  Serial.printf("亮度调整为：");
  analogWrite(LCD_BL_PIN, 1023 - (LCD_BL_PWM * 10));
  Serial.println(LCD_BL_PWM);
  // 天气更新时间
  Serial.printf("天气更新时间调整为：");
  Serial.println(weatherUpdateMinutes);

#if DHT_EN
  // 是否使用DHT11传感器
  Serial.printf("DHT11传感器：");
  EEPROM.write(DHT_addr, DHT_img_flag);
  EEPROM.commit(); //保存更改的数据
  Serial.println((DHT_img_flag ? "已启用" : "未启用"));
#endif
}
#endif

void parseWeatherResponse(const String &response)
{
  int indexStart = response.indexOf("weatherinfo\":");
  int indexEnd = response.indexOf("};var alarmDZ");
  String jsonCityDZ = response.substring(indexStart + 13, indexEnd);

  indexStart = response.indexOf("dataSK =");
  indexEnd = response.indexOf(";var dataZS");
  String jsonDataSK = response.substring(indexStart + 8, indexEnd);

  indexStart = response.indexOf("\"f\":[");
  indexEnd = response.indexOf(",{\"fa");
  String jsonFC = response.substring(indexStart + 5, indexEnd);

#if Animate_Choice != 3
  renderWeatherData(&jsonCityDZ, &jsonDataSK, &jsonFC);
#endif
  Serial.println("获取成功");
}

String scrollText[7];
// int scrollTextWidth = 0;

// 天气信息写到屏幕上
void renderWeatherData(String *cityDZ, String *dataSK, String *dataFC)
{
  // 解析第一段JSON
  DynamicJsonDocument doc(1024);
  deserializeJson(doc, *dataSK);
  JsonObject sk = doc.as<JsonObject>();

  // TFT_eSprite clkb = TFT_eSprite(&tft);

  /***绘制相关文字***/
  clk.setColorDepth(8);
  clk.loadFont(ZdyLwFont_20);

  // 温度
  clk.createSprite(58, 24);
  clk.fillSprite(bgColor);
  clk.setTextDatum(CC_DATUM);
  clk.setTextColor(TFT_WHITE, bgColor);
  clk.drawString(sk["temp"].as<String>() + "℃", 28, 13);
  clk.pushSprite(100, 184);
  clk.deleteSprite();
  tempnum = sk["temp"].as<int>();
  tempnum = tempnum + 10;
  if (tempnum < 10)
    tempcol = 0x00FF;
  else if (tempnum < 28)
    tempcol = 0x0AFF;
  else if (tempnum < 34)
    tempcol = 0x0F0F;
  else if (tempnum < 41)
    tempcol = 0xFF0F;
  else if (tempnum < 49)
    tempcol = 0xF00F;
  else
  {
    tempcol = 0xF00F;
    tempnum = 50;
  }
  tempWin();

  // 湿度
  clk.createSprite(58, 24);
  clk.fillSprite(bgColor);
  clk.setTextDatum(CC_DATUM);
  clk.setTextColor(TFT_WHITE, bgColor);
  clk.drawString(sk["SD"].as<String>(), 28, 13);
  // clk.drawString("100%",28,13);
  clk.pushSprite(100, 214);
  clk.deleteSprite();
  // String A = sk["SD"].as<String>();
  huminum = atoi((sk["SD"].as<String>()).substring(0, 2).c_str());

  if (huminum > 90)
    humicol = 0x00FF;
  else if (huminum > 70)
    humicol = 0x0AFF;
  else if (huminum > 40)
    humicol = 0x0F0F;
  else if (huminum > 20)
    humicol = 0xFF0F;
  else
    humicol = 0xF00F;
  humidityWin();

  // 城市名称
  clk.createSprite(94, 30);
  clk.fillSprite(bgColor);
  clk.setTextDatum(CC_DATUM);
  clk.setTextColor(TFT_WHITE, bgColor);
  clk.drawString(sk["cityname"].as<String>(), 44, 16);
  clk.pushSprite(15, 15);
  clk.deleteSprite();

  // PM2.5空气指数
  uint16_t pm25BgColor = tft.color565(156, 202, 127); //优
  String aqiTxt = "优";
  int pm25V = sk["aqi"];
  if (pm25V > 200)
  {
    pm25BgColor = tft.color565(136, 11, 32); //重度
    aqiTxt = "重度";
  }
  else if (pm25V > 150)
  {
    pm25BgColor = tft.color565(186, 55, 121); //中度
    aqiTxt = "中度";
  }
  else if (pm25V > 100)
  {
    pm25BgColor = tft.color565(242, 159, 57); //轻
    aqiTxt = "轻度";
  }
  else if (pm25V > 50)
  {
    pm25BgColor = tft.color565(247, 219, 100); //良
    aqiTxt = "良";
  }
  clk.createSprite(56, 24);
  clk.fillSprite(bgColor);
  clk.fillRoundRect(0, 0, 50, 24, 4, pm25BgColor);
  clk.setTextDatum(CC_DATUM);
  clk.setTextColor(0x0000);
  clk.drawString(aqiTxt, 25, 13);
  clk.pushSprite(104, 18);
  clk.deleteSprite();

  scrollText[0] = "实时天气 " + sk["weather"].as<String>();
  scrollText[1] = "空气质量 " + aqiTxt;
  scrollText[2] = "风向 " + sk["WD"].as<String>() + sk["WS"].as<String>();

  // scrollText[6] = atoi((sk["weathercode"].as<String>()).substring(1,3).c_str()) ;

  //天气图标
  wrat.printfweather(170, 15, atoi((sk["weathercode"].as<String>()).substring(1, 3).c_str()));

  //左上角滚动字幕
  //解析第二段JSON
  deserializeJson(doc, *cityDZ);
  JsonObject dz = doc.as<JsonObject>();
  // Serial.println(sk["ws"].as<String>());
  //横向滚动方式
  // String aa = "今日天气:" + dz["weather"].as<String>() + "，温度:最低" + dz["tempn"].as<String>() + "，最高" + dz["temp"].as<String>() + " 空气质量:" + aqiTxt + "，风向:" + dz["wd"].as<String>() + dz["ws"].as<String>();
  // scrollTextWidth = clk.textWidth(scrollText);
  // Serial.println(aa);
  scrollText[3] = "今日" + dz["weather"].as<String>();

  deserializeJson(doc, *dataFC);
  JsonObject fc = doc.as<JsonObject>();

  scrollText[4] = "最低温度" + fc["fd"].as<String>() + "℃";
  scrollText[5] = "最高温度" + fc["fc"].as<String>() + "℃";

  // Serial.println(scrollText[0]);

  clk.unloadFont();
}

int currentIndex = 0;
TFT_eSprite clkb = TFT_eSprite(&tft);

void scrollBanner()
{
  if (scrollText[currentIndex])
  {
    clkb.setColorDepth(8);
    clkb.loadFont(ZdyLwFont_20);
    clkb.createSprite(150, 30);
    clkb.fillSprite(bgColor);
    clkb.setTextWrap(false);
    clkb.setTextDatum(CC_DATUM);
    clkb.setTextColor(TFT_WHITE, bgColor);
    clkb.drawString(scrollText[currentIndex], 74, 16);
    clkb.pushSprite(10, 45);

    clkb.deleteSprite();
    clkb.unloadFont();

    if (currentIndex >= 5)
      currentIndex = 0; //回第一个
    else
      currentIndex += 1; //准备切换到下一个
  }
}

// 用快速线方法绘制数字
void drawLineFont(uint32_t _x, uint32_t _y, uint32_t _num, uint32_t _size, uint32_t _color)
{
  uint32_t fontSize;
  const LineAtom *fontOne;
  // 小号(9*14)
  if (_size == 1)
  {
    fontOne = smallLineFont[_num];
    fontSize = smallLineFont_size[_num];
  }
  // 中号(18*30)
  else if (_size == 2)
  {
    fontOne = middleLineFont[_num];
    fontSize = middleLineFont_size[_num];
  }
  // 大号(36*90)
  else if (_size == 3)
  {
    fontOne = largeLineFont[_num];
    fontSize = largeLineFont_size[_num];
  }
  else
    return;

  for (uint32_t i = 0; i < fontSize; i++)
  {
    tft.drawFastHLine(fontOne[i].xValue + _x, fontOne[i].yValue + _y, fontOne[i].lValue, _color);
  }
}

int Hour_sign = 60;
int Minute_sign = 60;
int Second_sign = 60;
// 日期刷新
void digitalClockDisplay(bool forceRefresh)
{
  // 时钟刷新,输入1强制刷新
  int now_hour = hour();     //获取小时
  int now_minute = minute(); //获取分钟
  int now_second = second(); //获取秒针
  //小时刷新
  if (now_hour != Hour_sign || forceRefresh)
  {
    drawLineFont(20, timeY, now_hour / 10, 3, SD_FONT_WHITE);
    drawLineFont(60, timeY, now_hour % 10, 3, SD_FONT_WHITE);
    Hour_sign = now_hour;
  }
  //分钟刷新
  if (now_minute != Minute_sign || forceRefresh)
  {
    drawLineFont(101, timeY, now_minute / 10, 3, SD_FONT_YELLOW);
    drawLineFont(141, timeY, now_minute % 10, 3, SD_FONT_YELLOW);
    Minute_sign = now_minute;
  }
  //秒针刷新
  if (now_second != Second_sign || forceRefresh)
  {
    drawLineFont(182, timeY + 30, now_second / 10, 2, SD_FONT_WHITE);
    drawLineFont(202, timeY + 30, now_second % 10, 2, SD_FONT_WHITE);
    Second_sign = now_second;
  }

}

void esp_reset(Button2 &btn)
{
  ESP.reset();
}

void wifi_reset(Button2 &btn)
{
  wm.resetSettings();
  deletewificonfig();
  delay(10);
  Serial.println("重置WiFi成功");
  ESP.restart();
}

//更新时间
void refreshTime()
{
  digitalClockDisplay();
}

//切换天气 or 空气质量
void refreshBanner()
{
#if DHT_EN
  if (DHT_img_flag != 0)
    IndoorTem();
#endif
  scrollBanner();
}

void serviceNetwork()
{
  if (wifiRefreshRequested && !networkRefresh.isActive())
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      Serial.println("WIFI connected");
      networkRefresh.start(cityCode);
      wifiRefreshRequested = false;
    }
  }

  if (networkRefresh.service(cityCode, parseWeatherResponse))
    digitalClockDisplay(true);
}

// 打开WIFI
void wakeWifi()
{
  Serial.println("Requesting network refresh......");
  wifiRefreshRequested = true;
}

// 强制屏幕刷新
void refreshDisplay()
{
  refreshTime();
  refreshBanner();
  wakeWifi();
}

PeriodicTask clockTask(refreshTime, 300);
PeriodicTask bannerTask(refreshBanner, 2 * TMS);
PeriodicTask wifiTask(wakeWifi, 60 * TMS);
PeriodicTask animationTask(refresh_AnimatedImage, 80);

void setWeatherUpdateInterval()
{
  wifiTask.setInterval(weatherUpdateMinutes * 60UL * TMS);
}

void serviceScheduledTasks()
{
  const uint32_t now = millis();
#if Animate_Choice != 3
  clockTask.run(now);
  bannerTask.run(now);
#endif
  wifiTask.run(now);
  animationTask.run(now);
}

void setup()
{
  Button_sw1.setClickHandler(esp_reset);
  Button_sw1.setLongClickHandler(wifi_reset);
  Serial.begin(115200);
  EEPROM.begin(1024);
  // WiFi.forceSleepWake();
  // wm.resetSettings();    //在初始化中使wifi重置，需重新配置WiFi

#if DHT_EN
  dht.begin();
  //从eeprom读取DHT传感器使能标志
  DHT_img_flag = EEPROM.read(DHT_addr);
#endif
  //从eeprom读取背光亮度设置
  if (EEPROM.read(BL_addr) > 0 && EEPROM.read(BL_addr) < 100)
    LCD_BL_PWM = EEPROM.read(BL_addr);
  //从eeprom读取屏幕方向设置
  if (EEPROM.read(Ro_addr) >= 0 && EEPROM.read(Ro_addr) <= 3)
    LCD_Rotation = EEPROM.read(Ro_addr);

  pinMode(LCD_BL_PIN, OUTPUT);
  analogWrite(LCD_BL_PIN, 1023 - (LCD_BL_PWM * 10));

  tft.begin();          /* TFT init */
  tft.invertDisplay(1); //反转所有显示颜色：1反转，0正常
  tft.setRotation(LCD_Rotation);
  tft.fillScreen(0x0000);
  tft.setTextColor(TFT_BLACK, bgColor);

  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(tft_output);

  drawMainScreen();
  showWifiStatus("WiFi Connecting...");

  readwificonfig(); //读取存储的wifi信息
  Serial.print("正在连接WIFI ");
  Serial.println(wificonf.stassid);
  WiFi.begin(wificonf.stassid, wificonf.stapsw);

  const unsigned long wifiConnectStart = millis();
  while (WiFi.status() != WL_CONNECTED)
  {
    refreshConnectingScreen();
    delay(10);
    if (millis() - wifiConnectStart >= 6000)
    {
//使能web配网后自动将smartconfig配网失效
#if WM_EN
      Web_win();
      Webconfig();
#endif

#if !WM_EN
      SmartConfig();
#endif
      break;
    }
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.print("SSID:");
    Serial.println(WiFi.SSID().c_str());
    Serial.print("PSW:");
    Serial.println(WiFi.psk().c_str());
    strcpy(wificonf.stassid, WiFi.SSID().c_str()); //名称复制
    strcpy(wificonf.stapsw, WiFi.psk().c_str());   //密码复制
    savewificonfig();
    readwificonfig();

#if OTA_EN
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.begin();
    Serial.print("OTA ready: ");
    Serial.print(OTA_HOSTNAME);
    Serial.println(".local");
#endif
    showWifiStatus("WiFi Connected");
  }

  Serial.print("本地IP： ");
  Serial.println(WiFi.localIP());
  Serial.println("启动UDP");
  Udp.begin(localPort);

  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(tft_output);

  int CityCODE = 0;
  for (int cnum = 5; cnum > 0; cnum--)
  {
    CityCODE = CityCODE * 100;
    CityCODE += EEPROM.read(CC_addr + cnum - 1);
    delay(5);
  }
  if (CityCODE >= 101000000 && CityCODE <= 102000000)
    cityCode = CityCODE;
  else
    cityCode = "0";
#if DHT_EN
  if (DHT_img_flag != 0)
    IndoorTem();
#endif

  const uint32_t now = millis();
#if Animate_Choice != 3
  clockTask.start(now);
  bannerTask.start(now);
#endif
  setWeatherUpdateInterval();
  wifiTask.start(now);
  animationTask.start(now, true);
}

void refresh_AnimatedImage()
{
#if Animate_Choice
  if (DHT_img_flag == 0)
  {
    AnimationFrame frame;
    if (animationPlayer.nextFrame(frame))
    {
#if Animate_Choice == 3
      TJpgDec.drawJpg(0, 0, frame.data, frame.size);
      digitalClockDisplay(true);
#else
      TJpgDec.drawJpg(160, 160, frame.data, frame.size);
#endif
    }
  }
#endif
}

void loop()
{
#if OTA_EN
  ArduinoOTA.handle();
#endif
  serviceScheduledTasks();
  serviceNetwork();
  serviceSerialCommands();
  Button_sw1.loop();
}
