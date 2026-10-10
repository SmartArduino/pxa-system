"""Compile a probe against an existing product simulator and run a signed AOT."""
import argparse, shlex, subprocess
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('--simulator-build',type=Path,required=True)
p.add_argument('--package',type=Path,required=True)
p.add_argument('--publisher-key',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();b=a.simulator_build.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
source=Path(__file__).resolve().parent;sdk=source.parents[4]
target=b/'CMakeFiles/pxsys_declarative_ui_test.dir';flags={}
for line in (target/'flags.make').read_text().splitlines():
 if ' = ' in line:
  k,v=line.split(' = ',1);flags[k]=shlex.split(v)
link=shlex.split((target/'link.txt').read_text());obj=out/'probe.o';binary=out/'probe'
subprocess.run([link[0],*flags['C_DEFINES'],*flags['C_INCLUDES'],*flags['C_FLAGS'],
 '-DPXA_PRODUCT_RUNNER="'+str(sdk/'simulator/desktop/product_runner.c')+'"',
 '-c',str(source/'host_probe.c'),'-o',str(obj)],check=True)
link=[str(obj) if s.endswith('declarative_ui_test.c.o') else s for s in link if not s.startswith('-Wl,--dependency-file=')]
link[link.index('-o')+1]=str(binary);subprocess.run(link,cwd=b,check=True)
state=out/'state';state.mkdir(exist_ok=True)
r=subprocess.run([str(binary),str(a.package.resolve()),str(a.publisher_key.resolve()),str(state),str(out)],text=True,capture_output=True)
(out/'host.log').write_text(r.stdout+r.stderr);print(r.stdout);print(r.stderr);r.check_returncode()
