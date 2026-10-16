"""Generate deterministic PNG/JPEG references and run the native decoder test.

Requires Pillow for fixture generation only; the runtime has no Python dependency.
"""
from io import BytesIO
from pathlib import Path
import subprocess
import sys
import tempfile
import struct
import zlib
from PIL import Image, ImageDraw


def main():
    with tempfile.TemporaryDirectory(prefix='pxa-image-decode-') as work:
        directory = Path(work)
        image = Image.new('RGBA', (320, 180))
        image.putdata([(30+x*120//320, 95+y*100//180, 210-y*60//180, 255)
                       for y in range(180) for x in range(320)])
        ImageDraw.Draw(image).rectangle((110, 60, 190, 120), fill=(255, 255, 255, 0))
        # Write Adam7 explicitly: Pillow's PNG encoder does not emit interlaced PNG.
        def chunk(kind, payload):
            return struct.pack('>I', len(payload))+kind+payload+struct.pack('>I', zlib.crc32(kind+payload))
        scanlines = bytearray()
        for left, top, dx, dy in [(0,0,8,8),(4,0,8,8),(0,4,4,8),(2,0,4,4),
                                  (0,2,2,4),(1,0,2,2),(0,1,1,2)]:
            for y in range(top, 180, dy):
                scanlines.append(0)
                for x in range(left, 320, dx): scanlines.extend(image.getpixel((x,y)))
        interlaced = b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR', struct.pack('>IIBBBBB',320,180,8,6,0,0,1))
        interlaced += chunk(b'IDAT', zlib.compress(scanlines))+chunk(b'IEND', b'')
        (directory/'interlaced.png').write_bytes(interlaced)
        (directory/'interlaced.png.rgba').write_bytes(image.tobytes())
        # Suggested RGB background previously overran pngle's local channel array.
        (directory/'background.png').write_bytes(interlaced[:33]+chunk(b'bKGD', b'\0\xff\0\xff\0\xff')+interlaced[33:])
        (directory/'background.png.rgba').write_bytes(image.tobytes())
        ihdr = chunk(b'IHDR', struct.pack('>IIBBBBB',320,180,8,6,0,0,0))
        end = chunk(b'IEND', b'')
        signature = b'\x89PNG\r\n\x1a\n'
        (directory/'extra-pixels.png').write_bytes(signature+ihdr+chunk(b'IDAT', zlib.compress(b'\0'*(180*(320*4+1)+32768)))+end)
        (directory/'metadata-before-header.png').write_bytes(signature+chunk(b'bKGD', b'\0'*6)+ihdr+end)
        for name, picture, fmt, options in [
            ('alpha.png', image, 'PNG', {}),
            ('palette.png', image.convert('P', palette=Image.Palette.ADAPTIVE, colors=32), 'PNG', {}),
            ('gray.png', image.convert('L'), 'PNG', {}),
            ('photo.jpg', image.convert('RGB'), 'JPEG', {'quality': 88}),
            ('gray.jpg', image.convert('L'), 'JPEG', {'quality': 88}),
            ('progressive.jpg', image.convert('RGB'), 'JPEG', {'progressive': True}),
        ]:
            data = BytesIO(); picture.save(data, format=fmt, **options)
            (directory/name).write_bytes(data.getvalue())
            if fmt == 'PNG':
                (directory/(name+'.rgba')).write_bytes(picture.convert('RGBA').tobytes())
            else:
                reference = Image.open(BytesIO(data.getvalue())).convert('RGB').resize(
                    (64, 36), Image.Resampling.BOX)
                (directory/(name+'.rgb')).write_bytes(reference.tobytes())
        (directory/'broken.png').write_bytes((directory/'alpha.png').read_bytes()[:-16])
        (directory/'unsupported.svg').write_text('<svg/>')
        # Non-MCU-aligned portrait, plus headers crossing many input blocks.
        # The padding is legitimate APP metadata, not copyright web content.
        portrait = Image.new('RGB', (1199, 2533))
        portrait.putdata([(25+x*190//1199, 20+y*210//2533, 205-y*130//2533)
                          for y in range(2533) for x in range(1199)])
        for name, picture in [('portrait.jpg', portrait), ('padded.jpg', image.convert('RGB'))]:
            encoded = BytesIO();picture.save(encoded, format='JPEG', quality=88)
            data = encoded.getvalue()
            padding = b''.join(b'\xff\xe1'+struct.pack('>H',50002)+b'\0'*50000 for _ in range(5))
            (directory/name).write_bytes(data[:2]+padding+data[2:])
            w,h = picture.size
            ow,oh = w,h
            if ow>256: oh=oh*256//ow;ow=256
            if oh>1024: ow=ow*1024//oh;oh=1024
            while ow*oh>32768:
                if ow>=oh and ow>1: ow-=1;oh=max(1,h*ow//w)
                else: oh-=1;ow=max(1,w*oh//h)
            reference = Image.open(BytesIO(data)).convert('RGB').resize((ow,oh), Image.Resampling.BOX)
            (directory/(name+'.rgb')).write_bytes(reference.tobytes())
        subprocess.run([sys.argv[1], str(directory)], check=True)


if __name__ == '__main__':
    main()
