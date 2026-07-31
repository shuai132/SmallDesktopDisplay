# RGB565 图片显示

## 架构

ESP8266 没有可容纳 240×240 RGB565 整帧的 RAM。图片上传、显示和存储均按网络分片处理，只使用 480 字节源行和 480 字节 TFT 像素缓冲。

```text
浏览器 / tools/image_push.py
  -> 240×240 RGB565 big-endian
  -> HTTP POST /api/images
  -> 逐分片 CRC32、逐行显示、LittleFS 临时文件
  -> CRC 成功后提交 A/B/C 槽
```

默认 4m1m linker layout 提供约 1 MB LittleFS。三个槽各占 115,224 字节；持久化上传先写 `/image-upload.tmp`，旧目标槽先改名为备份，临时文件成功改名后再删除备份。上传中断、长度或 CRC 错误不会提交目标槽。

## SDDI wire protocol v1

每个 HTTP body 固定为 115,224 字节：24 字节 header 加 115,200 字节 payload。

| 偏移 | 类型 | 值 |
|------|------|----|
| 0 | 4 bytes | ASCII `SDDI` |
| 4 | u8 | protocol version `1` |
| 5 | u8 | 保存 `1`；仅显示 `2` |
| 6 | u8 | RGB565 big-endian `1` |
| 7 | u8 | header size `24` |
| 8 | u8 | 槽 `0..2`；`255` 表示当前槽 |
| 9 | u8 | wire 请求为 `0`；槽文件格式版本 `1` |
| 10 | u8 | bit 0：校验后刷新 |
| 11 | u8 | reserved `0` |
| 12 | u16 LE | width `240` |
| 14 | u16 LE | height `240` |
| 16 | u32 LE | payload size `115200` |
| 20 | u32 LE | payload CRC32 |

payload 按横向 row-major 排列，每个 RGB565 像素高字节在前。渐进模式收到完整行后立即显示；校验后刷新模式先写临时文件，CRC 成功后再从文件逐行刷新，因此不申请整帧 RAM。

仅显示的渐进模式不写 LittleFS，适合网页实时预览。仅显示与校验后刷新组合需要临时文件，会产生 Flash 写入，不建议用于高频实时调整；网页实时模式因此固定为渐进刷新。

## HTTP API

| 方法 | 路径 | 作用 |
|------|------|------|
| GET | `/` | 内置图片控制网页 |
| GET | `/api/state` | 固件、模式、上传进度和槽位状态 |
| POST | `/api/images` | 上传固定长度 SDDI bundle |
| POST | `/api/mode/toggle` | 切换时钟/图片模式 |
| POST | `/api/slots/next` | 切换下一有效槽 |
| POST | `/api/slots/0..2` | 激活指定有效槽 |

HTTP 与图片上传当前没有认证，只应在可信局域网使用。服务不接受 chunked request body，同一时间只允许一个上传。

## 持久化与启动

- LittleFS 保存三张 RGB565 图片；EEPROM offset 160 保存配置版本、活动槽和显示模式。
- 启动时逐槽校验 header、长度和 CRC，重建有效槽位图。
- 保存上传成功后进入图片模式；仅显示不会保存活动槽或显示模式。
- 重启恢复上次持久化模式；临时预览不会成为持久图片。
