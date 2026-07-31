# SmallDesktopDisplay

ESP8266 + ST7789 240×240 桌面天气时钟，并支持局域网 RGB565 图片显示。

## 图片控制

设备联网后访问：

```text
http://SmallDesktopDisplay.local
```

网页支持选图、完整显示/铺满/拉伸、旋转、缩放、拖动裁切、实时预览、仅显示、渐进或校验后刷新，以及 A/B/C 三槽保存。图片在浏览器或 CLI 中转换为 240×240 RGB565，设备只保留 480 字节行缓冲。

命令行上传：

```bash
python3 -m pip install -r tools/requirements-image.txt
python3 tools/image_push.py photo.jpg
python3 tools/image_push.py photo.jpg --slot 2 --fit cover --rotate 90
python3 tools/image_push.py photo.jpg --display-only
python3 tools/image_push.py photo.jpg --render-mode frame
```

按键：

- 单击：切换时钟和图片模式。
- 双击：切换到下一张有效槽图片。
- 三击：重启设备。
- 长按：执行原有 WiFi 重置/重启流程。

协议和持久化说明见 [docs/image-viewer.md](docs/image-viewer.md)。

## 构建与测试

```bash
pio run -e esp12e
pio run -t upload -e esp12e
pio run -t upload -e esp12e_serial
python3 -m unittest tools/test_image_push.py
```

`esp12e` 通过 `SmallDesktopDisplay.local` 执行 WiFi OTA；`esp12e_serial` 通过自动识别的 USB 串口上传。串口较多时显式指定端口，例如：

```bash
pio run -t upload -e esp12e_serial --upload-port /dev/cu.usbserial-XXXX
```
