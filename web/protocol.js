"use strict";

(function initializeProtocol(globalScope) {
  const WIDTH = 240;
  const HEIGHT = 240;
  const HEADER_SIZE = 24;
  const PAYLOAD_SIZE = WIDTH * HEIGHT * 2;
  const CURRENT_SLOT = 255;
  const PROTOCOL_VERSION = 1;
  const FEATURE_FRAME_RENDER = 1;
  const BAYER_8X8 = [
    [0, 48, 12, 60, 3, 51, 15, 63],
    [32, 16, 44, 28, 35, 19, 47, 31],
    [8, 56, 4, 52, 11, 59, 7, 55],
    [40, 24, 36, 20, 43, 27, 39, 23],
    [2, 50, 14, 62, 1, 49, 13, 61],
    [34, 18, 46, 30, 33, 17, 45, 29],
    [10, 58, 6, 54, 9, 57, 5, 53],
    [42, 26, 38, 22, 41, 25, 37, 21],
  ];

  function orderedQuantize(value, maxCode, threshold) {
    const scaled = value * maxCode;
    let code = Math.floor(scaled / 255);
    const remainder = scaled % 255;
    if (code < maxCode && remainder * 128 > (threshold * 2 + 1) * 255) code += 1;
    return code;
  }

  function rgb565Payload(imageData) {
    if (!imageData || !imageData.data || imageData.data.length !== WIDTH * HEIGHT * 4) {
      throw new Error("image data must be RGBA 240x240");
    }
    const payload = new Uint8Array(PAYLOAD_SIZE);
    let output = 0;
    for (let y = 0; y < HEIGHT; y += 1) {
      for (let x = 0; x < WIDTH; x += 1) {
        const input = (y * WIDTH + x) * 4;
        const threshold = BAYER_8X8[y & 7][x & 7];
        const pixel =
          (orderedQuantize(imageData.data[input], 31, threshold) << 11) |
          (orderedQuantize(imageData.data[input + 1], 63, threshold) << 5) |
          orderedQuantize(imageData.data[input + 2], 31, threshold);
        payload[output++] = pixel >>> 8;
        payload[output++] = pixel & 0xff;
      }
    }
    return payload;
  }

  let crcTable;
  function crc32(bytes) {
    if (!crcTable) {
      crcTable = new Uint32Array(256);
      for (let entry = 0; entry < 256; entry += 1) {
        let value = entry;
        for (let bit = 0; bit < 8; bit += 1) value = value & 1 ? 0xedb88320 ^ (value >>> 1) : value >>> 1;
        crcTable[entry] = value >>> 0;
      }
    }
    let crc = 0xffffffff;
    for (const value of bytes) crc = crcTable[(crc ^ value) & 0xff] ^ (crc >>> 8);
    return (crc ^ 0xffffffff) >>> 0;
  }

  function buildBundle(imageData, options = {}) {
    const displayOnly = Boolean(options.displayOnly);
    const progressive = Boolean(options.progressive);
    const slot = displayOnly ? CURRENT_SLOT : options.slot ?? CURRENT_SLOT;
    if (slot !== CURRENT_SLOT && (!Number.isInteger(slot) || slot < 0 || slot > 2)) throw new Error("invalid slot");
    const payload = rgb565Payload(imageData);
    const bundle = new Uint8Array(HEADER_SIZE + payload.length);
    const header = new DataView(bundle.buffer, 0, HEADER_SIZE);
    bundle.set([0x53, 0x44, 0x44, 0x49], 0);
    header.setUint8(4, PROTOCOL_VERSION);
    header.setUint8(5, displayOnly ? 2 : 1);
    header.setUint8(6, 1);
    header.setUint8(7, HEADER_SIZE);
    header.setUint8(8, slot);
    header.setUint8(9, 0);
    header.setUint8(10, progressive ? 0 : FEATURE_FRAME_RENDER);
    header.setUint8(11, 0);
    header.setUint16(12, WIDTH, true);
    header.setUint16(14, HEIGHT, true);
    header.setUint32(16, PAYLOAD_SIZE, true);
    header.setUint32(20, crc32(payload), true);
    bundle.set(payload, HEADER_SIZE);
    return bundle;
  }

  const protocol = { WIDTH, HEIGHT, HEADER_SIZE, PAYLOAD_SIZE, CURRENT_SLOT, orderedQuantize, rgb565Payload, crc32, buildBundle };
  globalScope.SmallDisplayProtocol = protocol;
  if (typeof module !== "undefined" && module.exports) module.exports = protocol;
})(typeof globalThis !== "undefined" ? globalThis : this);
