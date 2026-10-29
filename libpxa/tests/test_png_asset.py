"""Generate an indexed alpha fixture and verify the native cached PNG loader."""
from pathlib import Path
import subprocess
import sys
import tempfile
import struct
from io import BytesIO
from PIL import Image


def verify(binary, image, directory, indexed=True):
    if indexed:
        image = image.quantize(colors=256, method=Image.Quantize.FASTOCTREE,
                               dither=Image.Dither.NONE)
    png = directory / 'pose.png'
    image.save(png, optimize=True)
    rgba = image.convert('RGBA').tobytes()
    bgra = bytearray()
    for r, g, b, a in zip(rgba[::4], rgba[1::4], rgba[2::4], rgba[3::4]):
        bgra.extend(((b*a+127)//255, (g*a+127)//255, (r*a+127)//255, a))
    reference = directory / 'expected.bgra'
    reference.write_bytes(bgra)
    subprocess.run([binary, str(png), str(reference), str(image.width), str(image.height)], check=True)


def verify_rgb565(binary, directory):
    # Exercise the full opaque area (above ordinary PNG's 65536-pixel cap)
    # and every possible RGB565 value, including channel rounding edges.
    width, height = 320, 240
    words = [i % 65536 for i in range(width * height)]
    rgb = bytes(c for word in words for c in (((word >> 11)*255+15)//31,
                 (((word >> 5)&63)*255+31)//63, ((word&31)*255+15)//31))
    image = Image.frombytes('RGB', (width, height), rgb)
    path, reference = directory/'room.pxr', directory/'expected.rgb565'
    reference.write_bytes(struct.pack('<'+'H'*len(words), *words))
    def write(image):
        stream = BytesIO()
        image.save(stream, format='PNG', optimize=True)
        path.write_bytes(struct.pack('<4sHHHHHHIIQ', b'PXR1',1,0,4,10,width,height,
                                     width*height*2,32,0)+stream.getvalue())
    write(image)
    command = [binary, str(path), str(reference), str(width), str(height), 'rgb565']
    subprocess.run(command, check=True)
    image = image.convert('RGBA')
    image.putpixel((width-1,height-1), (0,0,0,254))
    write(image)
    subprocess.run(command+['reject-alpha'], check=True)


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='pxa-png-asset-') as work:
        image = Image.new('RGBA', (16, 16))
        image.putdata([(x*13, y*15, (x+y)*7, (0, 1, 80, 127, 128, 254, 255)[(x+y)%7])
                       for y in range(16) for x in range(16)])
        verify(sys.argv[1], image, Path(work))
        verify(sys.argv[1], image, Path(work), indexed=False)
        verify_rgb565(sys.argv[1], Path(work))
