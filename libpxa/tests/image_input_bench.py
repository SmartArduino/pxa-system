"""Generate a copyright-free JPEG with APP metadata and run the input benchmark."""
import argparse
from io import BytesIO
from pathlib import Path
import struct
import subprocess
import tempfile
from PIL import Image

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('binary', type=Path)
p.add_argument('--save-input', type=Path)
a = p.parse_args()
image = Image.new('RGB', (320, 180))
image.putdata([(30+x*120//320, 95+y*100//180, 210-y*60//180)
               for y in range(180) for x in range(320)])
encoded = BytesIO()
image.save(encoded, format='JPEG', quality=88)
data = encoded.getvalue()
padding = (b'\xff\xe1'+struct.pack('>H', 50002)+b'\0'*50000)*5
data = data[:2]+padding+data[2:]
with tempfile.TemporaryDirectory(prefix='pxa-jpeg-benchmark-') as directory:
    path = a.save_input or Path(directory)/'input.jpg'
    path.write_bytes(data)
    subprocess.run([str(a.binary.resolve()), str(path)], check=True)
