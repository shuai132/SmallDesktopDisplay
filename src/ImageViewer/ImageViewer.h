#pragma once

#include <Arduino.h>

class TFT_eSPI;

constexpr uint16_t IMAGE_VIEWER_WIDTH = 240;
constexpr uint16_t IMAGE_VIEWER_HEIGHT = 240;
constexpr size_t IMAGE_VIEWER_HEADER_SIZE = 24;
constexpr size_t IMAGE_VIEWER_PAYLOAD_SIZE = IMAGE_VIEWER_WIDTH * IMAGE_VIEWER_HEIGHT * 2;
constexpr size_t IMAGE_VIEWER_BUNDLE_SIZE = IMAGE_VIEWER_HEADER_SIZE + IMAGE_VIEWER_PAYLOAD_SIZE;
constexpr uint8_t IMAGE_VIEWER_SLOT_COUNT = 3;

struct ImageViewerState
{
  bool hasImage;
  bool imageMode;
  bool uploading;
  uint16_t receivedRows;
  uint8_t activeSlot;
  uint8_t validSlots;
};

using DashboardDrawCallback = void (*)();

void imageViewerInit(TFT_eSPI &display, DashboardDrawCallback drawDashboard);
void imageViewerSetNetworkAddress(const String &address);
void imageViewerToggleMode();
void imageViewerNextSlot();
bool imageViewerActivateSlot(uint8_t slot);
bool imageViewerIsImageMode();
ImageViewerState imageViewerState();

bool imageViewerUploadOpen(String &error);
const char *imageViewerUploadConsume(const uint8_t *data, size_t size);
void imageViewerUploadAbort();
