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


def chunk(kind, payload):
    return struct.pack('>I', len(payload))+kind+payload+struct.pack('>I', zlib.crc32(kind+payload))


def adam7(image):
    width, height = image.size
    scanlines = bytearray()
    for left, top, dx, dy in [(0,0,8,8),(4,0,8,8),(0,4,4,8),(2,0,4,4),
                              (0,2,2,4),(1,0,2,2),(0,1,1,2)]:
        if left >= width or top >= height: continue
        for y in range(top, height, dy):
            scanlines.append(0)
            for x in range(left, width, dx): scanlines.extend(image.getpixel((x,y)))
    return (b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR', struct.pack('>IIBBBBB',width,height,8,6,0,0,1))+
            chunk(b'IDAT', zlib.compress(scanlines))+chunk(b'IEND', b''))


def main():
    with tempfile.TemporaryDirectory(prefix='pxa-image-decode-') as work:
        directory = Path(work)
        image = Image.new('RGBA', (320, 180))
        image.putdata([(30+x*120//320, 95+y*100//180, 210-y*60//180, 255)
                       for y in range(180) for x in range(320)])
        ImageDraw.Draw(image).rectangle((110, 60, 190, 120), fill=(255, 255, 255, 0))
        # Write Adam7 explicitly: Pillow's PNG encoder does not emit interlaced PNG.
        interlaced = adam7(image)
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
        tiny=Image.new('RGBA',(9,9))
        tiny.putdata([(x*29,y*31,(x*17+y*11)%256,(x*13+y*19)%256) for y in range(9) for x in range(9)])
        for width in range(1,10):
            for height in range(1,10):
                picture=tiny.crop((0,0,width,height));name=f'tiny-{width}-{height}.png'
                (directory/name).write_bytes(adam7(picture))
                (directory/(name+'.rgba')).write_bytes(picture.tobytes())
        # The same source width requires the same row buffers at every height.
        # Exact RGBA references also cover non-integral output sampling/Adam7.
        large = Image.new('RGBA', (1280, 1778))
        large.putdata([((x//32%8)*31,(y//32%8)*31,((x//16+y//32)%2)*200,(x//16%2)*127+128)
                       for y in range(1778) for x in range(1280)])
        for name, picture, interlace in [('large.png',large,False),('large-interlaced.png',large,True),
                ('odd-interlaced.png',large.crop((0,0,97,65)),True),
                ('tall.png',large.crop((0,0,1,1778)),True),('single.png',large.crop((0,0,1,1)),True),
                ('area-boundary.png',Image.new('RGBA',(2048,2048),(75,130,190,85)),False)]:
            if interlace: encoded=adam7(picture)
            else:
                encoded=BytesIO();picture.save(encoded,'PNG');encoded=encoded.getvalue()
            (directory/name).write_bytes(encoded)
            assert len(encoded)<=524288,(name,len(encoded))
            (directory/(name+'.rgba')).write_bytes(picture.tobytes())
        for name,width,height in [('over-area.png',2049,2048),('over-width.png',4097,1),
                                  ('over-height.png',1,4097),('zero-width.png',0,1)]:
            (directory/name).write_bytes(signature+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,6,0,0,0))+end)
        # Packed grayscale and 16-bit channels exercise wrap/scaling paths
        # outside the common 8-bit RGBA fast path, including odd row widths.
        for depth in (1,2,4,16):
            rows=bytearray();reference=bytearray();maximum=(1<<depth)-1
            for y in range(13):
                rows.append(0);packed=bytearray();bits=0;value=0
                for x in range(17):
                    sample=(x*771+y*129)% (maximum+1)
                    gray=(sample*255+maximum//2)//maximum;reference.extend((gray,gray,gray,255))
                    if depth==16:packed.extend(struct.pack('>H',sample))
                    else:
                        value=(value<<depth)|sample;bits+=depth
                        if bits==8:packed.append(value);value=bits=0
                if bits:packed.append(value<<(8-bits))
                rows.extend(packed)
            name=f'depth-{depth}.png'
            (directory/name).write_bytes(signature+chunk(b'IHDR',struct.pack('>IIBBBBB',17,13,depth,0,0,0,0))+
                chunk(b'IDAT',zlib.compress(rows))+end)
            (directory/(name+'.rgba')).write_bytes(reference)
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
