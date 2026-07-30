#define Animate_Choice 3      //动图选择：1,太空人 2,胡桃 3,龙猫星夜特效
#define TMS 1000              //一千毫秒
#define WM_EN 0               //关闭 WEB/AP 配网，固件直接连接预设 WiFi
#define DHT_EN 0              //设定DHT11温湿度传感器使能标志
#define OTA_EN 1              //启用 WiFi OTA 上传；首次仍需通过串口刷入
#define OTA_HOSTNAME "SmallDesktopDisplay"
#define SD_FONT_YELLOW 0xD404 // 黄色字体颜色
#define SD_FONT_WHITE 0xFFFF  // 黄色字体颜色

#if Animate_Choice == 3
#define timeY 75 // 星夜模式将时钟垂直居中
#else
#define timeY 82
#endif
