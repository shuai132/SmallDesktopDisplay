#pragma once

// Copied from the User_Setup.h that was tracked in the old .pio directory.
#define USER_SETUP_LOADED

#define ST7789_2_DRIVER
#define TFT_RGB_ORDER TFT_RGB

#define TFT_WIDTH 240
#define TFT_HEIGHT 240

// NodeMCU: D8=GPIO15, D3=GPIO0, D4=GPIO2, D1=GPIO5.
#define TFT_CS 15
#define TFT_DC 0
#define TFT_RST 2
#define TFT_BL 5

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT

#define SPI_FREQUENCY 27000000
#define SPI_READ_FREQUENCY 20000000
#define SPI_TOUCH_FREQUENCY 2500000
