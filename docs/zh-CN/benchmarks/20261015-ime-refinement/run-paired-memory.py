#!/usr/bin/env python3
"""Compare this iteration's frozen frontend with the current frontend.

Use the adjacent native/ CMake project for deterministic synchronous rendering.
Normal desktop rendering is threaded: record ranges instead of asserting equal
instantaneous peaks. All values are requested heap bytes, excluding libc metadata.
"""
from pathlib import Path
import argparse, hashlib, json, shlex, shutil, subprocess
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--runs',type=int,default=5)
parser.add_argument('--threaded',action='store_true',help='Record nondeterministic peaks without requiring equality')
options=parser.parse_args()
if options.runs<1:parser.error('--runs must be positive')
sdk=Path(__file__).resolve().parents[4];assets=Path(__file__).resolve().parent
build=options.build.resolve();out=options.output.resolve();out.mkdir(parents=True,exist_ok=True)
frontend=sdk/'libpxa/adapters/lvgl/pxa_lvgl_ime.c'
expected=json.loads((assets/'frontend-sources.json').read_text())
if hashlib.sha256(frontend.read_bytes()).hexdigest()!=expected['after']:
    raise SystemExit('Frontend has changed since this report; explicitly re-establish the paired baseline.')
before_source=out/'before-frontend.c';shutil.copy2(frontend,before_source)
subprocess.run(['patch','--batch','--fuzz=0','--reverse',str(before_source),str(assets/'frontend-changes.patch')],check=True)
assert hashlib.sha256(before_source.read_bytes()).hexdigest()==expected['before']
commands=subprocess.check_output(['ninja','-C',str(build),'-t','commands','pxsys_reference_ime_test'],text=True).splitlines()
frontend=sdk/'libpxa/adapters/lvgl/pxa_lvgl_ime.c'
recipe=next(c for c in commands if ' -c '+str(frontend) in c);args=shlex.split(recipe);args=args[:args.index('-MD')];args+=['-I'+str(sdk/'libpxa/adapters/lvgl')]
test_recipe=next(c for c in commands if ' -c '+str(sdk/'ui/reference/tests/reference_ime_test.c') in c);test_args=shlex.split(test_recipe);test_args=test_args[:test_args.index('-MD')]
harness=(assets/'native-memory.c').read_text()
source=out/'paired-memory.c';source.write_text(harness)
obj=out/'paired-memory.o';subprocess.run(test_args+['-I'+str(sdk/'tools/benchmarks'),'-UNDEBUG','-o',str(obj),'-c',str(source)],cwd=build,check=True)
link=shlex.split(commands[-1]);start=link.index('&&')+1;end=link.index('&&',start);link=[p for p in link[start:end] if not p.endswith('.o')]
values=[]
for name,path in [('before',before_source),('after',frontend)]:
 fobj=out/(name+'-frontend.o');exe=out/(name+'-paired-memory')
 subprocess.run(args+['-o',str(fobj),'-c',str(path)],cwd=build,check=True)
 command=link.copy();command[command.index('-o')+1]=str(exe);command[1:1]=[str(obj),str(fobj)];command+=['-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free'];subprocess.run(command,cwd=build,check=True)
 for shape in (0,1):
  for run in range(options.runs):
   value=json.loads(subprocess.check_output([str(exe),str(shape)],text=True).splitlines()[-1]);value.update(variant=name,run=run);values.append(value)
(out/'paired-native-memory.json').write_text(json.dumps(values,indent=2)+'\n')
for shape in (0,1):
 before=[v for v in values if v['variant']=='before' and v['shape']==shape]
 after=[v for v in values if v['variant']=='after' and v['shape']==shape]
 for key in ('loaded_delta','closed_delta')+(() if options.threaded else ('reopen_peak_delta','initialization_absolute_peak')):
  actual={v[key] for v in before+after}
  assert len(actual)==1,(shape,key,actual)
 assert all(v['closed_delta']==0 for v in before+after)
print('Paired frontend heap passed; raw per-run values saved.')
