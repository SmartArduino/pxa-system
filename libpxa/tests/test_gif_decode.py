"""Compare immutable, scaled native GIF frames with Pillow's full-size compositor."""
from io import BytesIO
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from PIL import Image, ImageDraw


def literal_frame(width, height, indices, x=0, y=0, control=None, interlaced=False, local=None):
    """Small legal min-code-size=2 fixture; no production entropy encoder."""
    packed = bytearray()
    bits = available = 0
    codes = []
    if interlaced:
        rows = list(range(0, height, 8)) + list(range(4, height, 8))
        rows += list(range(2, height, 4)) + list(range(1, height, 2))
        indices = [indices[y*width+x] for y in rows for x in range(width)]
    for at in range(0, len(indices), 2):
        codes.extend([4, *indices[at:at+2]])
    codes.append(5)
    for code in codes:
        bits |= code << available
        available += 3
        while available >= 8:
            packed.append(bits & 255)
            bits >>= 8
            available -= 8
    if available:
        packed.append(bits)
    out = bytearray()
    if control:
        disposal, delay, transparent = control
        out.extend(b'\x21\xf9\x04')
        out.extend(struct.pack('<BHB', disposal << 2 | (transparent is not None), delay,
                               transparent or 0))
        out.append(0)
    flags = (0x40 if interlaced else 0) | (0x81 if local else 0)
    out.extend(b',' + struct.pack('<HHHHB', x, y, width, height, flags))
    if local:
        out.extend(local)
    out.append(2)
    for at in range(0, len(packed), 255):
        block = packed[at:at+255]
        out.append(len(block))
        out.extend(block)
    out.append(0)
    return out


def references(data, frames, eof):
    picture = Image.open(BytesIO(data))
    w, h = picture.size
    ow, oh = w, h
    if ow > 64:
        oh = max(1, oh * 64 // ow)
        ow = 64
    if oh > 64:
        ow = max(1, ow * 64 // oh)
        oh = 64
    decoded = []
    for index in range(picture.n_frames):
        picture.seek(index)
        rgba = picture.convert('RGBA')
        pixels = bytearray()
        for y in range(oh):
            for x in range(ow):
                r, g, b, a = rgba.getpixel((x * w // ow, y * h // oh))
                pixels.extend((b, g, r, a))
        decoded.append((picture.info.get('duration', 0), bytes(pixels)))
    count = len(decoded) * frames
    out = bytearray(struct.pack('<IIII', ow, oh, count, eof))
    for iteration in range(frames):
        for index, (delay, pixels) in enumerate(decoded):
            last = eof and iteration == frames - 1 and index == len(decoded) - 1
            out.extend(struct.pack('<III', delay, iteration, last))
            out.extend(pixels)
    return out


def main():
    with tempfile.TemporaryDirectory(prefix='pxa-gif-') as temporary:
        root = Path(temporary)
        cases = []
        palette = [0, 0, 0, 240, 40, 30, 20, 200, 80, 30, 90, 240, 240, 220, 30] + [0] * (768-15)
        for name, transparent, disposals, loop in [
            ('opaque', False, [1] * 5, None),
            ('transparent', True, [2] * 5, 1),
            ('restore', True, [1, 3, 3, 1, 1], None),
            ('mixed', True, [1, 2, 3, 1, 2], 2),
            ('forever', True, [1, 2, 3, 1, 2], 0),
        ]:
            frames = []
            for n in range(5):
                frame = Image.new('P', (137, 83), 0)
                frame.putpalette(palette)
                draw = ImageDraw.Draw(frame)
                draw.rectangle((n*21+4, n*9+3, n*21+30, n*9+29), fill=n % 4 + 1)
                draw.rectangle((5, 66, 45, 78), fill=3)
                frames.append(frame)
            options = dict(save_all=True, append_images=frames[1:], duration=[0, 20, 90, 800, 655350],
                           disposal=disposals, optimize=False)
            if transparent: options['transparency'] = 0
            if loop is not None: options['loop'] = loop
            encoded = BytesIO(); frames[0].save(encoded, format='GIF', **options)
            data = encoded.getvalue()
            iterations = 2 if loop == 0 else (loop + 1 if loop is not None else 1)
            path = root/(name+'.gif'); path.write_bytes(data)
            reference = root/(name+'.ref')
            reference.write_bytes(references(data, iterations, loop != 0))
            cases.append((path, reference))
        # Pillow's single-image encoder emits an interlaced image descriptor.
        image = Image.new('P', (97, 65)); image.putpalette(palette)
        image.putdata([(x//7 + y//5) % 5 for y in range(65) for x in range(97)])
        data = BytesIO(); image.save(data, format='GIF', interlace=True, optimize=False)
        path = root/'interlaced.gif'; path.write_bytes(data.getvalue())
        reference = root/'interlaced.ref'; reference.write_bytes(references(data.getvalue(), 1, True))
        cases.append((path, reference))
        # Explicitly omit the GCE after a transparent frame: transparency and
        # disposal apply to one graphic, not the next image descriptor.
        colors = bytes([205, 215, 225, 240, 30, 20, 20, 200, 70, 30, 80, 230])
        manual = bytearray(b'GIF89a' + struct.pack('<HHBBB', 17, 11, 0x81, 0, 0) + colors)
        manual.extend(literal_frame(17, 11, [(x+y) % 2 for y in range(11) for x in range(17)],
                                    control=(1, 2, 0), interlaced=True))
        manual.extend(literal_frame(7, 4, [0] * 28, x=2, y=2))
        manual.extend(literal_frame(13, 5, [1] * 65, x=1, y=1,
                                    control=(2, 10, 0), local=colors[::-1]))
        manual.extend(literal_frame(4, 3, [2] * 12, x=5, y=6, control=(3, 20, 0)))
        manual.extend(literal_frame(3, 2, [3] * 6, x=1, y=8, control=(1, 7, 0)))
        manual.append(0x3b)
        path = root/'min-code-controls.gif'; path.write_bytes(manual)
        reference = root/'min-code-controls.ref'; reference.write_bytes(references(manual, 1, True))
        cases.append((path, reference))
        # Large source, tiny output: no source-sized pixel canvas is needed.
        image = Image.new('P', (1024, 512)); image.putpalette(palette)
        image.putdata([(x//13+y//11) % 5 for y in range(512) for x in range(1024)])
        data = BytesIO(); image.save(data, format='GIF', optimize=False)
        path = root/'large-source.gif'; path.write_bytes(data.getvalue())
        reference = root/'large-source.ref'; reference.write_bytes(references(data.getvalue(), 1, True))
        cases.append((path, reference))
        for path, reference in cases:
            result = subprocess.run([sys.argv[1], str(path), str(reference)], timeout=40)
            if result.returncode == 77:
                sys.exit(77)  # The platform intentionally disables LV_USE_GIF.
            result.check_returncode()


if __name__ == '__main__':
    main()
