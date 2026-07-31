#include "ImageViewer.h"

#include <EEPROM.h>
#include <LittleFS.h>
#include <TFT_eSPI.h>

namespace
{
constexpr uint8_t kProtocolVersion = 1;
constexpr uint8_t kSlotFormatVersion = 1;
constexpr uint8_t kCommandStore = 1;
constexpr uint8_t kCommandDisplayOnly = 2;
constexpr uint8_t kPixelFormatRgb565Be = 1;
constexpr uint8_t kCurrentSlot = 0xff;
constexpr uint8_t kFeatureFrameRender = 1;
constexpr uint32_t kConfigMagic = 0x53444449;
constexpr uint8_t kConfigVersion = 1;
constexpr int kConfigAddress = 160;
constexpr char kTemporaryPath[] = "/image-upload.tmp";

struct ViewerConfig
{
  uint32_t magic;
  uint8_t version;
  uint8_t activeSlot;
  uint8_t validSlots;
  uint8_t imageMode;
};

TFT_eSPI *g_display = nullptr;
DashboardDrawCallback g_draw_dashboard = nullptr;
ViewerConfig g_config = {kConfigMagic, kConfigVersion, 0, 0, 0};
String g_network_address;

bool g_uploading = false;
bool g_upload_started = false;
bool g_upload_completed = false;
bool g_upload_persist = false;
bool g_upload_frame_render = false;
bool g_upload_displayed = false;
bool g_previous_image_mode = false;
uint8_t g_upload_target_slot = 0;
uint8_t g_previous_active_slot = 0;
uint8_t g_header[IMAGE_VIEWER_HEADER_SIZE];
size_t g_header_received = 0;
size_t g_payload_received = 0;
size_t g_row_received = 0;
uint16_t g_received_rows = 0;
uint32_t g_crc = 0xffffffff;
uint32_t g_expected_crc = 0;
File g_upload_file;
uint8_t g_row_bytes[IMAGE_VIEWER_WIDTH * 2];
uint16_t g_row_pixels[IMAGE_VIEWER_WIDTH];
String g_last_error;
bool g_filesystem_ready = false;

uint16_t readU16Le(const uint8_t *data)
{
  return static_cast<uint16_t>(data[0]) | static_cast<uint16_t>(data[1]) << 8;
}

uint32_t readU32Le(const uint8_t *data)
{
  return static_cast<uint32_t>(data[0]) | static_cast<uint32_t>(data[1]) << 8 | static_cast<uint32_t>(data[2]) << 16 |
         static_cast<uint32_t>(data[3]) << 24;
}

void crc32Update(uint32_t &crc, const uint8_t *data, size_t size)
{
  while (size-- > 0)
  {
    crc ^= *data++;
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xedb88320UL & (0UL - (crc & 1UL)));
  }
}

String slotPath(uint8_t slot)
{
  return String("/image-") + slot + ".rgb";
}

String backupPath(uint8_t slot)
{
  return String("/image-") + slot + ".bak";
}

void saveConfig()
{
  g_config.magic = kConfigMagic;
  g_config.version = kConfigVersion;
  EEPROM.put(kConfigAddress, g_config);
  EEPROM.commit();
}

bool requestHeaderValid(const uint8_t *header)
{
  const bool slotValid = header[8] == kCurrentSlot || header[8] < IMAGE_VIEWER_SLOT_COUNT;
  return memcmp(header, "SDDI", 4) == 0 && header[4] == kProtocolVersion &&
         (header[5] == kCommandStore || header[5] == kCommandDisplayOnly) && header[6] == kPixelFormatRgb565Be &&
         header[7] == IMAGE_VIEWER_HEADER_SIZE && slotValid && header[9] == 0 && (header[10] & ~kFeatureFrameRender) == 0 &&
         header[11] == 0 && readU16Le(header + 12) == IMAGE_VIEWER_WIDTH && readU16Le(header + 14) == IMAGE_VIEWER_HEIGHT &&
         readU32Le(header + 16) == IMAGE_VIEWER_PAYLOAD_SIZE;
}

bool storedHeaderValid(const uint8_t *header)
{
  return memcmp(header, "SDDI", 4) == 0 && header[6] == kPixelFormatRgb565Be && header[7] == IMAGE_VIEWER_HEADER_SIZE &&
         header[8] < IMAGE_VIEWER_SLOT_COUNT && header[9] == kSlotFormatVersion && readU16Le(header + 12) == IMAGE_VIEWER_WIDTH &&
         readU16Le(header + 14) == IMAGE_VIEWER_HEIGHT && readU32Le(header + 16) == IMAGE_VIEWER_PAYLOAD_SIZE;
}

void pushRow(uint16_t row, const uint8_t *bytes)
{
  if (!g_display || row >= IMAGE_VIEWER_HEIGHT)
    return;
  for (uint16_t column = 0; column < IMAGE_VIEWER_WIDTH; ++column)
    g_row_pixels[column] = static_cast<uint16_t>(bytes[column * 2]) << 8 | bytes[column * 2 + 1];
  const bool previousSwapBytes = g_display->getSwapBytes();
  g_display->setSwapBytes(true);
  g_display->pushImage(0, row, IMAGE_VIEWER_WIDTH, 1, g_row_pixels);
  g_display->setSwapBytes(previousSwapBytes);
}

bool validateSlot(uint8_t slot)
{
  File file = LittleFS.open(slotPath(slot), "r");
  if (!file || file.size() != IMAGE_VIEWER_BUNDLE_SIZE)
    return false;
  uint8_t header[IMAGE_VIEWER_HEADER_SIZE];
  if (file.read(header, sizeof(header)) != sizeof(header) || !storedHeaderValid(header) || header[8] != slot)
  {
    file.close();
    return false;
  }
  uint32_t crc = 0xffffffff;
  uint8_t buffer[256];
  size_t remaining = IMAGE_VIEWER_PAYLOAD_SIZE;
  while (remaining > 0)
  {
    const size_t wanted = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
    const size_t read = file.read(buffer, wanted);
    if (read != wanted)
    {
      file.close();
      return false;
    }
    crc32Update(crc, buffer, read);
    remaining -= read;
    yield();
  }
  file.close();
  return (crc ^ 0xffffffff) == readU32Le(header + 20);
}

bool renderFile(const String &path)
{
  File file = LittleFS.open(path, "r");
  if (!file)
    return false;
  uint8_t header[IMAGE_VIEWER_HEADER_SIZE];
  if (file.read(header, sizeof(header)) != sizeof(header) || !storedHeaderValid(header))
  {
    file.close();
    return false;
  }
  g_display->startWrite();
  for (uint16_t row = 0; row < IMAGE_VIEWER_HEIGHT; ++row)
  {
    if (file.read(g_row_bytes, sizeof(g_row_bytes)) != sizeof(g_row_bytes))
    {
      g_display->endWrite();
      file.close();
      return false;
    }
    pushRow(row, g_row_bytes);
    if ((row & 15) == 15)
      yield();
  }
  g_display->endWrite();
  file.close();
  return true;
}

bool renderSlot(uint8_t slot)
{
  if (slot >= IMAGE_VIEWER_SLOT_COUNT || (g_config.validSlots & (1 << slot)) == 0)
    return false;
  return renderFile(slotPath(slot));
}

void showEmptySlot()
{
  if (!g_display)
    return;
  g_display->fillScreen(TFT_BLACK);
  g_display->setTextDatum(MC_DATUM);
  g_display->setTextColor(TFT_WHITE, TFT_BLACK);
  g_display->drawString(String("IMAGE ") + static_cast<char>('A' + g_config.activeSlot), 120, 92, 4);
  g_display->setTextColor(TFT_DARKGREY, TFT_BLACK);
  g_display->drawString("No saved image", 120, 126, 2);
  if (g_network_address.length() > 0)
    g_display->drawString(g_network_address, 120, 151, 2);
}

void showActiveSlot()
{
  if (!renderSlot(g_config.activeSlot))
    showEmptySlot();
}

void restorePreviousDisplay()
{
  g_config.activeSlot = g_previous_active_slot;
  g_config.imageMode = g_previous_image_mode ? 1 : 0;
  if (g_previous_image_mode)
    showActiveSlot();
  else if (g_draw_dashboard)
    g_draw_dashboard();
}

void resetUploadState()
{
  if (g_upload_file)
    g_upload_file.close();
  g_uploading = false;
  g_upload_started = false;
  g_upload_completed = false;
  g_header_received = 0;
  g_payload_received = 0;
  g_row_received = 0;
  g_received_rows = 0;
  g_crc = 0xffffffff;
}

const char *failUpload(const char *code)
{
  g_last_error = String("ERR ") + code;
  if (g_upload_file)
    g_upload_file.close();
  LittleFS.remove(kTemporaryPath);
  g_upload_completed = true;
  g_uploading = false;
  g_received_rows = 0;
  if (g_upload_started || g_upload_displayed)
    restorePreviousDisplay();
  return g_last_error.c_str();
}

bool beginPayload()
{
  if (!requestHeaderValid(g_header))
    return false;
  g_upload_target_slot = g_header[8] == kCurrentSlot ? g_config.activeSlot : g_header[8];
  g_upload_persist = g_header[5] == kCommandStore;
  g_upload_frame_render = (g_header[10] & kFeatureFrameRender) != 0;
  g_expected_crc = readU32Le(g_header + 20);
  g_upload_started = true;
  g_config.imageMode = 1;

  if (g_upload_persist || g_upload_frame_render)
  {
    LittleFS.remove(kTemporaryPath);
    g_upload_file = LittleFS.open(kTemporaryPath, "w");
    if (!g_upload_file)
      return false;
    uint8_t storedHeader[IMAGE_VIEWER_HEADER_SIZE];
    memcpy(storedHeader, g_header, sizeof(storedHeader));
    storedHeader[5] = kCommandStore;
    storedHeader[8] = g_upload_target_slot;
    storedHeader[9] = kSlotFormatVersion;
    storedHeader[10] = 0;
    if (g_upload_file.write(storedHeader, sizeof(storedHeader)) != sizeof(storedHeader))
      return false;
  }
  return true;
}

bool commitTemporaryFile(uint8_t slot)
{
  const String target = slotPath(slot);
  const String backup = backupPath(slot);
  LittleFS.remove(backup);
  const bool hadTarget = LittleFS.exists(target);
  if (hadTarget && !LittleFS.rename(target, backup))
    return false;
  if (!LittleFS.rename(kTemporaryPath, target))
  {
    if (hadTarget)
      LittleFS.rename(backup, target);
    return false;
  }
  LittleFS.remove(backup);
  return true;
}

const char *finishUpload()
{
  if (g_payload_received != IMAGE_VIEWER_PAYLOAD_SIZE || (g_crc ^ 0xffffffff) != g_expected_crc)
    return failUpload("CRC");
  if (g_upload_file)
    g_upload_file.close();

  if (g_upload_frame_render && !renderFile(kTemporaryPath))
    return failUpload("DISPLAY");

  if (!g_upload_persist)
  {
    LittleFS.remove(kTemporaryPath);
    g_upload_completed = true;
    g_uploading = false;
    g_received_rows = IMAGE_VIEWER_HEIGHT;
    return "DISPLAYED";
  }

  if (!commitTemporaryFile(g_upload_target_slot))
    return failUpload("STORAGE");
  g_config.activeSlot = g_upload_target_slot;
  g_config.validSlots |= 1 << g_upload_target_slot;
  g_config.imageMode = 1;
  saveConfig();
  g_upload_completed = true;
  g_uploading = false;
  g_received_rows = IMAGE_VIEWER_HEIGHT;
  return "STORED";
}

const char *consumePayload(const uint8_t *data, size_t size)
{
  if (size > IMAGE_VIEWER_PAYLOAD_SIZE - g_payload_received)
    return failUpload("TRAILING_DATA");
  while (size > 0)
  {
    const size_t rowSpace = sizeof(g_row_bytes) - g_row_received;
    const size_t take = size < rowSpace ? size : rowSpace;
    memcpy(g_row_bytes + g_row_received, data, take);
    if (g_upload_file && g_upload_file.write(data, take) != take)
      return failUpload("STORAGE");
    crc32Update(g_crc, data, take);
    g_row_received += take;
    g_payload_received += take;
    data += take;
    size -= take;
    if (g_row_received == sizeof(g_row_bytes))
    {
      if (!g_upload_frame_render)
      {
        pushRow(g_received_rows, g_row_bytes);
        g_upload_displayed = true;
      }
      ++g_received_rows;
      g_row_received = 0;
      yield();
    }
  }
  if (g_payload_received == IMAGE_VIEWER_PAYLOAD_SIZE)
    return finishUpload();
  return nullptr;
}
} // namespace

void imageViewerInit(TFT_eSPI &display, DashboardDrawCallback drawDashboard)
{
  g_display = &display;
  g_draw_dashboard = drawDashboard;
  g_filesystem_ready = LittleFS.begin();
  if (!g_filesystem_ready)
  {
    LittleFS.format();
    g_filesystem_ready = LittleFS.begin();
  }
  EEPROM.get(kConfigAddress, g_config);
  if (g_config.magic != kConfigMagic || g_config.version != kConfigVersion || g_config.activeSlot >= IMAGE_VIEWER_SLOT_COUNT)
    g_config = {kConfigMagic, kConfigVersion, 0, 0, 0};

  g_config.validSlots = 0;
  for (uint8_t slot = 0; slot < IMAGE_VIEWER_SLOT_COUNT; ++slot)
  {
    if (g_filesystem_ready)
      LittleFS.remove(backupPath(slot));
    if (g_filesystem_ready && validateSlot(slot))
      g_config.validSlots |= 1 << slot;
  }
  if (g_filesystem_ready)
    LittleFS.remove(kTemporaryPath);
  if ((g_config.validSlots & (1 << g_config.activeSlot)) == 0)
  {
    for (uint8_t slot = 0; slot < IMAGE_VIEWER_SLOT_COUNT; ++slot)
      if (g_config.validSlots & (1 << slot))
      {
        g_config.activeSlot = slot;
        break;
      }
  }
  saveConfig();
  if (g_config.imageMode)
    showActiveSlot();
}

void imageViewerSetNetworkAddress(const String &address)
{
  g_network_address = address;
  if (g_config.imageMode && (g_config.validSlots & (1 << g_config.activeSlot)) == 0)
    showEmptySlot();
}

void imageViewerToggleMode()
{
  if (g_uploading)
    return;
  g_config.imageMode = !g_config.imageMode;
  saveConfig();
  if (g_config.imageMode)
    showActiveSlot();
  else if (g_draw_dashboard)
    g_draw_dashboard();
}

void imageViewerNextSlot()
{
  if (g_uploading || g_config.validSlots == 0)
    return;
  for (uint8_t offset = 1; offset <= IMAGE_VIEWER_SLOT_COUNT; ++offset)
  {
    const uint8_t slot = (g_config.activeSlot + offset) % IMAGE_VIEWER_SLOT_COUNT;
    if (g_config.validSlots & (1 << slot))
    {
      imageViewerActivateSlot(slot);
      return;
    }
  }
}

bool imageViewerActivateSlot(uint8_t slot)
{
  if (g_uploading || slot >= IMAGE_VIEWER_SLOT_COUNT || (g_config.validSlots & (1 << slot)) == 0)
    return false;
  g_config.activeSlot = slot;
  g_config.imageMode = 1;
  saveConfig();
  showActiveSlot();
  return true;
}

bool imageViewerIsImageMode()
{
  return g_config.imageMode != 0;
}

ImageViewerState imageViewerState()
{
  ImageViewerState state;
  state.hasImage = (g_config.validSlots & (1 << g_config.activeSlot)) != 0;
  state.imageMode = g_config.imageMode != 0;
  state.uploading = g_uploading;
  state.receivedRows = g_received_rows;
  state.activeSlot = g_config.activeSlot;
  state.validSlots = g_config.validSlots;
  return state;
}

bool imageViewerUploadOpen(String &error)
{
  if (g_uploading)
  {
    error = "ERR BUSY";
    return false;
  }
  resetUploadState();
  g_uploading = true;
  g_previous_image_mode = g_config.imageMode != 0;
  g_previous_active_slot = g_config.activeSlot;
  g_upload_displayed = false;
  return true;
}

const char *imageViewerUploadConsume(const uint8_t *data, size_t size)
{
  if (!g_uploading || g_upload_completed || data == nullptr || size == 0)
    return nullptr;
  while (size > 0 && !g_upload_completed)
  {
    if (!g_upload_started)
    {
      const size_t needed = IMAGE_VIEWER_HEADER_SIZE - g_header_received;
      const size_t take = size < needed ? size : needed;
      memcpy(g_header + g_header_received, data, take);
      g_header_received += take;
      data += take;
      size -= take;
      if (g_header_received == IMAGE_VIEWER_HEADER_SIZE && !beginPayload())
        return failUpload(requestHeaderValid(g_header) ? "STORAGE" : "HEADER");
      continue;
    }
    return consumePayload(data, size);
  }
  return nullptr;
}

void imageViewerUploadAbort()
{
  if (!g_uploading)
    return;
  failUpload("INCOMPLETE");
}
