"""Compile a probe against an existing product simulator and run a signed AOT."""
import argparse, subprocess
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('--simulator-build',type=Path,required=True)
p.add_argument('--package',type=Path,required=True)
p.add_argument('--publisher-key',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();b=a.simulator_build.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
binary=b/'pxsys_cpp_service_safety_test'
if not binary.is_file():
 raise SystemExit('Build pxsys_cpp_service_safety_test with PXSYS_BUILD_TESTS=ON first')
state=out/'state';state.mkdir(exist_ok=True)
r=subprocess.run([str(binary),str(a.package.resolve()),str(a.publisher_key.resolve()),str(state),str(out)],text=True,capture_output=True)
(out/'host.log').write_text(r.stdout+r.stderr);print(r.stdout);print(r.stderr);r.check_returncode()
