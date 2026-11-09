"""Run a freshly built legacy SDK package in owned isolated simulator state.

No compiler, signing or lock update runs here. The verifier supplies a frozen
container and hash after a normal SDK build; unchecked legacy reuse is refused.
"""
from pathlib import Path
import argparse
import hashlib
import os
import subprocess
import sys

if __package__ in (None,''):
    sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from pxa_agent.host_tools import bridge, reader
from pxa_agent.common import ToolError


def stop(process):
    if process is not None and process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill();process.wait(timeout=5)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk',type=Path,required=True)
    parser.add_argument('--package',type=Path,required=True)
    parser.add_argument('--sha256',required=True)
    parser.add_argument('--app-id',required=True)
    parser.add_argument('--profile',required=True)
    parser.add_argument('--state',type=Path,required=True)
    parser.add_argument('--duration-ms',type=int,required=True)
    parser.add_argument('--store-screenshots',action='store_true')
    args=parser.parse_args()
    process=debug=None
    try:
        with args.package.open('rb') as stream:
            if hashlib.file_digest(stream,'sha256').hexdigest() != args.sha256:
                raise ValueError('Prepared verification package changed; nothing was installed')
        # Native launch flags are the same public SDK recipe as ordinary run.
        # Its actual profile reader and binaries belong to the locked release.
        sys.path.insert(0,str(args.sdk/'tools'))
        from pxa_tools.run import simulator_bin,profile_options
        options=profile_options(args.profile,store_screenshots=args.store_screenshots)
        tools=reader(sdk=args.sdk)
        if tools.package_manifest_identity(args.package) != args.app_id:
            raise ValueError('Prepared package identity differs from the app')
        public=[value for tag,value in tools.manifest_records(tools.read_package_manifest(args.package)[12:]) if tag==11]
        if len(public) != 1 or not public[0] or len(public[0]) > 512:
            raise ValueError('Prepared package has no unique bounded publisher key')
        args.state.mkdir(parents=True,exist_ok=False)
        key=args.state/'publisher.der';key.write_bytes(public[0])
        installer=simulator_bin('pxsys_package_installer')
        subprocess.run([installer,'--storage-root',args.state,'--publisher-key',key,
                        '--source',args.package,'--expected-id',args.app_id],check=True,timeout=120)
        sockets=Path(os.environ['PXA_SIMULATOR_SOCKET_ROOT'])
        token=hashlib.sha256(str(args.state).encode()).hexdigest()[:8]
        selector=args.profile+'@v'+token
        control=sockets/'control.sock'
        endpoint=sockets/(selector+'.sock')
        if max(len(os.fsencode(str(control))),len(os.fsencode(str(endpoint)))) >= 108:
            raise ValueError('Socket path is too long; choose a shorter temporary directory')
        debug=subprocess.Popen([sys.executable,str(bridge(sdk=args.sdk)),
            '--state-root',str(args.state),'--socket',str(endpoint),'--control-socket',str(control),
            '--installer',str(installer),'--publisher-key',str(key)])
        print('PXADB: pxa device info --simulator '+selector,flush=True)
        command=[simulator_bin('pxsys_desktop_simulator'),
            '--installed-packages-root',args.state/'packages','--state-root',args.state,
            '--publisher-key',key,'--product-runner',simulator_bin('pxsys_product_simulator'),
            '--launch',args.app_id,*options,'--pxadb-control-socket',control,
            '--duration-ms',str(args.duration_ms)]
        variables=dict(os.environ)
        font=args.sdk/'share/pxa/fonts/noto_sans_cjk_common.ttf'
        if font.is_file():variables['PXA_SIMULATOR_FONT']=str(font)
        if args.store_screenshots:
            variables.pop('PXA_SIMULATOR_PERF',None)
            variables['PXA_SIM_NAV_MODE']='gesture-no-bar'
        process=subprocess.Popen(list(map(str,command)),env=variables)
        return process.wait()
    except KeyboardInterrupt:
        return 130
    except (OSError,ValueError,TypeError,ImportError,ToolError,subprocess.SubprocessError) as error:
        print('PXA prepared run: '+str(error),file=sys.stderr)
        return 1
    finally:
        stop(process);stop(debug)


if __name__ == '__main__':raise SystemExit(main())
