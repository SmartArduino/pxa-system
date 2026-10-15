"""Validate held-press feedback through real Host events; isolated 320x320/160dpi profile."""
import argparse, io, json, os, socket, subprocess, tempfile, time
from pathlib import Path
from PIL import Image, ImageChops
p=argparse.ArgumentParser(description=__doc__)
for arg in ('simulator','package','publisher-key','output'):p.add_argument('--'+arg,type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='pxa-gesture-') as tmp:
 state=Path(tmp)/'state';state.mkdir();control=Path(tmp)/'c.sock'
 with (a.output/'runtime.log').open('wb') as log:
  process=subprocess.Popen([str(a.simulator.resolve()),'--package',str(a.package.resolve()),
   '--publisher-key',str(a.publisher_key.resolve()),'--state-root',str(state),
   '--width','320','--height','320','--density-dpi','160','--pxadb-control-socket',str(control)],
   env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy'),stdout=log,stderr=subprocess.STDOUT)
  try:
   deadline=time.monotonic()+15
   while not control.exists():
    assert process.poll() is None and time.monotonic()<deadline
    time.sleep(.04)
   def command(text):
    with socket.socket(socket.AF_UNIX) as s:
     s.settimeout(10);s.connect(str(control));s.sendall((text+'\n').encode());data=b''
     while chunk:=s.recv(65536):data+=chunk
    assert not data.startswith(b'ERR'),data
    return data
   def shot(name):
    header,data=command('SCREENSHOT').split(b'\n',1);assert header==f'PNG {len(data)}'.encode()
    (a.output/(name+'.png')).write_bytes(data)
    return Image.open(io.BytesIO(data)).convert('RGB')
   tap_box,hold_box=(20,55,100,88),(20,90,100,122)
   def differs(first,second,box):return bool(ImageChops.difference(first.crop(box),second.crop(box)).getbbox())
   time.sleep(.4);initial=shot('initial')
   start=time.monotonic();command('TOUCH DOWN 0 70 148');time.sleep(.3);early=shot('early')
   assert not differs(initial,early,tap_box) and not differs(initial,early,hold_box)
   while True:
    held=shot('held')
    if differs(initial,held,hold_box):break
    assert time.monotonic()-start<2,'held callback did not refresh the UI'
    time.sleep(.02)
   elapsed=(time.monotonic()-start)*1000
   assert not differs(initial,held,tap_box)
   time.sleep(.65);assert not differs(held,shot('held-once'),hold_box)
   command('TOUCH UP 0 70 148');time.sleep(.25);released=shot('released')
   assert not differs(held,released,hold_box) and not differs(initial,released,tap_box)
   command('TOUCH DOWN 0 70 148');time.sleep(.08);command('TOUCH UP 0 70 148');time.sleep(.2)
   tapped=shot('tap');assert differs(released,tapped,tap_box) and not differs(released,tapped,hold_box)
   command('TOUCH DOWN 0 70 148');time.sleep(.1);command('TOUCH MOVE 0 100 148')
   time.sleep(.1);command('TOUCH MOVE 0 70 148');time.sleep(.55)
   command('TOUCH UP 0 70 148');time.sleep(.2);cancelled=shot('drag-cancelled')
   assert not differs(tapped,cancelled,tap_box) and not differs(tapped,cancelled,hold_box)
   command('TOUCH DOWN 0 70 148');time.sleep(.1);command('TOUCH CANCEL 0 70 148');time.sleep(.6)
   cancelled=shot('touch-cancelled')
   assert not differs(tapped,cancelled,tap_box) and not differs(tapped,cancelled,hold_box)
   result=dict(passed=True,held_before_release=True,held_once=True,no_release_clickthrough=True,
    tap=True,drag_out_and_back_cancels=True,cancel=True,held_visible_ms=round(elapsed,1),app_has_frame_callback=False)
   (a.output/'results.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
  finally:
   process.terminate();process.wait(timeout=8)
