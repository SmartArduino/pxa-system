"""Alternate identical Guest FS work; the import hashes bytes instead of accessing a disk."""
import argparse,json,os,statistics,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--before',type=Path,required=True);p.add_argument('--after',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
if hasattr(os,'sched_getaffinity'):os.sched_setaffinity(0,{max(os.sched_getaffinity(0))})
source=Path(__file__).with_name('fs_guest_benchmark.cpp')
with tempfile.TemporaryDirectory()as directory:
 directory=Path(directory)
 for side in ('before','after'):
  sdk=getattr(a,side).resolve()
  subprocess.run([os.environ.get('CXX','clang++'),'-std=c++2c','-O3','-fno-builtin','-fno-exceptions','-fno-rtti','-Wno-attributes','-I',str(sdk/'include'),str(source),str(sdk/'src/runtime.cpp'),'-o',str(directory/side)],check=True)
 data={str(size):{side:[]for side in ('before','after')}for size in (8,64,255)}
 for repeat in range(8):
  for size in data:
   for side in (('before','after')if repeat%2==0 else ('after','before')):
    data[size][side].append(json.loads(subprocess.check_output([str(directory/side),size])))
for size,sides in data.items():
 for key in ('imports','digest','pool','peak_slots'):
  expected=sides['before'][0][key];assert all(v[key]==expected for values in sides.values()for v in values),key
 print(size,{side:statistics.median(v['ns']for v in values)for side,values in sides.items()})
a.output.write_text(json.dumps(data,indent=2)+'\n')
