import base64
import json
import shutil
import struct
import subprocess
import sys
import unittest
import zlib
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import image_push


ROOT = Path(__file__).resolve().parents[1]


class ImagePushTests(unittest.TestCase):
    def test_bundle_header_and_crc(self):
        bundle = image_push.build_bundle(Image.new("RGB", (240, 240), (12, 34, 56)), slot=2)
        header = struct.unpack(image_push.HEADER_FORMAT, bundle[: image_push.HEADER_SIZE])
        payload = bundle[image_push.HEADER_SIZE :]
        self.assertEqual(image_push.MAGIC, header[0])
        self.assertEqual(image_push.PROTOCOL_VERSION, header[1])
        self.assertEqual(image_push.COMMAND_STORE, header[2])
        self.assertEqual(2, header[5])
        self.assertEqual((240, 240, 115200), header[6:9])
        self.assertEqual(zlib.crc32(payload), header[9])
        self.assertEqual(115224, len(bundle))

    def test_display_only_uses_current_slot(self):
        bundle = image_push.build_bundle(Image.new("RGB", (240, 240)), display_only=True)
        header = struct.unpack(image_push.HEADER_FORMAT, bundle[: image_push.HEADER_SIZE])
        self.assertEqual(image_push.COMMAND_DISPLAY_ONLY, header[2])
        self.assertEqual(image_push.CURRENT_SLOT, header[5])

    def test_rgb565_big_endian(self):
        image = Image.new("RGB", (240, 240), (0, 0, 0))
        image.putpixel((0, 0), (255, 0, 0))
        image.putpixel((1, 0), (0, 255, 0))
        image.putpixel((2, 0), (0, 0, 255))
        self.assertEqual(b"\xf8\x00\x07\xe0\x00\x1f", image_push.rgb565_be_payload(image)[:6])

    def test_contain_adds_black_bars(self):
        output = image_push.fit_image(Image.new("RGB", (200, 100), (255, 0, 0)))
        self.assertEqual((0, 0, 0), output.getpixel((120, 0)))
        self.assertEqual((255, 0, 0), output.getpixel((120, 120)))

    @unittest.skipUnless(shutil.which("node"), "node is required for browser protocol parity")
    def test_browser_protocol_matches_cli(self):
        source = """
          const p = require('./web/protocol.js');
          const rgba = new Uint8ClampedArray(p.WIDTH * p.HEIGHT * 4);
          for (let y=0;y<p.HEIGHT;y++) for (let x=0;x<p.WIDTH;x++) {
            const i=(y*p.WIDTH+x)*4; rgba[i]=(x*7+y*3)&255; rgba[i+1]=(x*5+y*11)&255; rgba[i+2]=(x*13+y)&255; rgba[i+3]=255;
          }
          const b=p.buildBundle({data:rgba},{displayOnly:true,progressive:false});
          console.log(JSON.stringify({header:Array.from(b.slice(0,24)),payload:Buffer.from(b.slice(24)).toString('base64')}));
        """
        completed = subprocess.run(["node", "-e", source], cwd=ROOT, check=True, capture_output=True, text=True)
        result = json.loads(completed.stdout)
        image = Image.new("RGB", (240, 240))
        image.putdata([((x * 7 + y * 3) & 255, (x * 5 + y * 11) & 255, (x * 13 + y) & 255) for y in range(240) for x in range(240)])
        payload = image_push.rgb565_be_payload(image)
        self.assertEqual(payload, base64.b64decode(result["payload"]))
        self.assertEqual(image_push.FEATURE_FRAME_RENDER, result["header"][10])


if __name__ == "__main__":
    unittest.main()
