#include "WebController.h"

#include <ESP8266WiFi.h>

#include "ImageViewer/ImageViewer.h"
#include "web_assets.generated.hpp"

namespace
{
constexpr uint16_t kHttpPort = 80;
constexpr size_t kMaximumHeaderSize = 3072;
constexpr uint32_t kClientTimeoutMs = 15000;

WiFiServer g_server(kHttpPort);
WiFiClient g_client;
String g_headers;
size_t g_body_remaining = 0;
uint32_t g_last_activity = 0;
bool g_reading_body = false;
bool g_started = false;
const char *g_firmware_version = "unknown";

String lowerAscii(String value)
{
  value.toLowerCase();
  return value;
}
const char *reasonPhrase(int status)
{
  switch (status)
  {
  case 200:
    return "OK";
  case 204:
    return "No Content";
  case 400:
    return "Bad Request";
  case 404:
    return "Not Found";
  case 409:
    return "Conflict";
  case 413:
    return "Payload Too Large";
  case 500:
    return "Internal Server Error";
  default:
    return "Error";
  }
}

void closeClient()
{
  if (g_client)
    g_client.stop();
  g_client = WiFiClient();
  g_headers = "";
  g_body_remaining = 0;
  g_reading_body = false;
}

void sendHeaders(int status, const char *contentType, size_t contentLength, bool gzip = false)
{
  g_client.print(F("HTTP/1.1 "));
  g_client.print(status);
  g_client.print(' ');
  g_client.print(reasonPhrase(status));
  g_client.print(F("\r\nContent-Type: "));
  g_client.print(contentType);
  g_client.print(F("\r\nContent-Length: "));
  g_client.print(contentLength);
  g_client.print(F("\r\nCache-Control: no-cache\r\nX-Content-Type-Options: nosniff\r\n"));
  g_client.print(F("Content-Security-Policy: default-src 'self'; img-src 'self' blob: data:; style-src 'self'; script-src 'self'; "
                   "connect-src 'self'; base-uri 'none'; frame-ancestors 'none'\r\n"));
  g_client.print(F("Referrer-Policy: no-referrer\r\nConnection: close\r\n"));
  if (gzip)
    g_client.print(F("Content-Encoding: gzip\r\n"));
  g_client.print(F("\r\n"));
}

void sendText(int status, const char *contentType, const String &body)
{
  sendHeaders(status, contentType, body.length());
  g_client.print(body);
  closeClient();
}

void sendJson(int status, bool ok, const String &result)
{
  const String body = String("{\"ok\":") + (ok ? "true" : "false") + ",\"status\":\"" + result + "\"}";
  sendText(status, "application/json; charset=utf-8", body);
}

void sendAsset(const uint8_t *data, size_t size, const char *contentType)
{
  sendHeaders(200, contentType, size, true);
  g_client.write_P(reinterpret_cast<PGM_P>(data), size);
  closeClient();
}

String stateJson()
{
  const ImageViewerState state = imageViewerState();
  return String("{\"firmware\":\"") + g_firmware_version + "\",\"protocol\":1,\"image_slot_format\":1,\"width\":240,\"height\":240," +
         "\"has_image\":" + (state.hasImage ? "true" : "false") + ",\"image_mode\":" + (state.imageMode ? "true" : "false") +
         ",\"uploading\":" + (state.uploading ? "true" : "false") + ",\"received_rows\":" + state.receivedRows +
         ",\"total_rows\":240,\"active_slot\":" + state.activeSlot + ",\"valid_slots\":" + state.validSlots + ",\"slot_count\":3}";
}

bool parseContentLength(const String &headers, size_t &contentLength, bool &hasLength, bool &chunked)
{
  contentLength = 0;
  hasLength = false;
  chunked = false;
  int cursor = headers.indexOf("\r\n") + 2;
  while (cursor > 1 && cursor < static_cast<int>(headers.length()))
  {
    const int end = headers.indexOf("\r\n", cursor);
    if (end < 0 || end == cursor)
      break;
    const String line = headers.substring(cursor, end);
    const int colon = line.indexOf(':');
    if (colon > 0)
    {
      const String name = lowerAscii(line.substring(0, colon));
      String value = line.substring(colon + 1);
      value.trim();
      if (name == "content-length")
      {
        for (size_t index = 0; index < value.length(); ++index)
          if (!isDigit(value[index]))
            return false;
        contentLength = static_cast<size_t>(strtoul(value.c_str(), nullptr, 10));
        hasLength = true;
      }
      else if (name == "transfer-encoding" && lowerAscii(value).indexOf("chunked") >= 0)
        chunked = true;
    }
    cursor = end + 2;
  }
  return true;
}

void handleGet(const String &path)
{
  if (path == "/")
    sendAsset(kWebIndexGzip, kWebIndexGzipSize, kWebIndexGzipContentType);
  else if (path == "/app.css")
    sendAsset(kWebCssGzip, kWebCssGzipSize, kWebCssGzipContentType);
  else if (path == "/protocol.js")
    sendAsset(kWebProtocolJsGzip, kWebProtocolJsGzipSize, kWebProtocolJsGzipContentType);
  else if (path == "/app.js")
    sendAsset(kWebJsGzip, kWebJsGzipSize, kWebJsGzipContentType);
  else if (path == "/api/state")
    sendText(200, "application/json; charset=utf-8", stateJson());
  else if (path == "/favicon.ico")
    sendText(204, "text/plain", "");
  else
    sendJson(404, false, "ERR NOT_FOUND");
}

void handleControlPost(const String &path)
{
  if (path == "/api/mode/toggle")
  {
    imageViewerToggleMode();
    sendJson(200, true, "OK");
    return;
  }
  if (path == "/api/slots/next")
  {
    imageViewerNextSlot();
    sendJson(200, true, "OK");
    return;
  }
  const String prefix = "/api/slots/";
  if (path.startsWith(prefix) && path.length() == prefix.length() + 1)
  {
    const char value = path[prefix.length()];
    if (value >= '0' && value <= '2' && imageViewerActivateSlot(value - '0'))
      sendJson(200, true, "OK");
    else
      sendJson(400, false, "ERR SLOT");
    return;
  }
  sendJson(404, false, "ERR NOT_FOUND");
}

void beginRequest()
{
  const int lineEnd = g_headers.indexOf("\r\n");
  if (lineEnd < 0)
  {
    sendJson(400, false, "ERR HTTP_REQUEST");
    return;
  }
  const String requestLine = g_headers.substring(0, lineEnd);
  const int firstSpace = requestLine.indexOf(' ');
  const int secondSpace = firstSpace < 0 ? -1 : requestLine.indexOf(' ', firstSpace + 1);
  if (firstSpace < 0 || secondSpace < 0)
  {
    sendJson(400, false, "ERR HTTP_REQUEST");
    return;
  }
  const String method = requestLine.substring(0, firstSpace);
  String path = requestLine.substring(firstSpace + 1, secondSpace);
  const int query = path.indexOf('?');
  if (query >= 0)
    path.remove(query);

  size_t contentLength;
  bool hasLength;
  bool chunked;
  if (!parseContentLength(g_headers, contentLength, hasLength, chunked))
  {
    sendJson(400, false, "ERR CONTENT_LENGTH");
    return;
  }
  if (method == "GET")
  {
    handleGet(path);
    return;
  }
  if (method != "POST")
  {
    sendJson(404, false, "ERR NOT_FOUND");
    return;
  }
  if (chunked || !hasLength)
  {
    sendJson(400, false, "ERR CONTENT_LENGTH");
    return;
  }
  if (path != "/api/images")
  {
    if (contentLength != 0)
      sendJson(400, false, "ERR LENGTH");
    else
      handleControlPost(path);
    return;
  }
  if (contentLength != IMAGE_VIEWER_BUNDLE_SIZE)
  {
    sendJson(contentLength > IMAGE_VIEWER_BUNDLE_SIZE ? 413 : 400, false, "ERR LENGTH");
    return;
  }
  String error;
  if (!imageViewerUploadOpen(error))
  {
    sendJson(409, false, error);
    return;
  }
  g_body_remaining = contentLength;
  g_reading_body = true;
  g_headers = "";
}

void consumeBody()
{
  uint8_t buffer[512];
  while (g_client.available() && g_body_remaining > 0)
  {
    const size_t available = g_client.available();
    const size_t wanted = min(sizeof(buffer), min(available, g_body_remaining));
    const int read = g_client.read(buffer, wanted);
    if (read <= 0)
      return;
    g_last_activity = millis();
    g_body_remaining -= read;
    const char *result = imageViewerUploadConsume(buffer, read);
    if (result)
    {
      if (strcmp(result, "STORED") == 0 || strcmp(result, "DISPLAYED") == 0)
        sendJson(200, true, result);
      else if (strcmp(result, "ERR STORAGE") == 0 || strcmp(result, "ERR DISPLAY") == 0)
        sendJson(500, false, result);
      else
        sendJson(400, false, result);
      return;
    }
    yield();
  }
}
} // namespace

void webControllerBegin(const char *firmwareVersion)
{
  if (g_started)
    return;
  g_firmware_version = firmwareVersion;
  g_server.begin();
  g_started = true;
}

void webControllerService()
{
  if (!g_started)
    return;
  if (!g_client)
  {
    g_client = g_server.available();
    if (!g_client)
      return;
    g_client.setNoDelay(true);
    g_headers.reserve(1024);
    g_last_activity = millis();
  }

  if (!g_client.connected() && !g_client.available())
  {
    if (g_reading_body)
      imageViewerUploadAbort();
    closeClient();
    return;
  }
  if (millis() - g_last_activity > kClientTimeoutMs)
  {
    if (g_reading_body)
      imageViewerUploadAbort();
    closeClient();
    return;
  }
  if (g_reading_body)
  {
    consumeBody();
    return;
  }

  while (g_client.available())
  {
    g_last_activity = millis();
    g_headers += static_cast<char>(g_client.read());
    if (g_headers.length() > kMaximumHeaderSize)
    {
      sendJson(400, false, "ERR HTTP_HEADER");
      return;
    }
    if (g_headers.endsWith("\r\n\r\n"))
    {
      beginRequest();
      if (g_client && g_reading_body)
        consumeBody();
      return;
    }
  }
}
