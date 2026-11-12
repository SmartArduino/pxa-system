"""Run a verified SDK package in owned isolated simulator state.

No compiler, signing or lock update runs here. The verifier supplies a frozen
container and hash after a normal SDK build; unchecked reuse is refused.
An optional, exactly bound resource generation is installed before the code.
"""
from pathlib import Path
import argparse
import hashlib
import os
import shutil
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


def verify_resource(tools, application, resource, expected_hash):
    """Check the immutable pair before invoking the signature-checking installer."""
    with resource.open('rb') as stream:
        if hashlib.file_digest(stream, 'sha256').hexdigest() != expected_hash:
            raise ValueError('Prepared resource package changed; nothing was installed')
    resource_manifest = tools.read_package_manifest(resource)
    app_manifest = tools.read_package_manifest(application)
    def field(manifest, tag):
        values = [value for kind, value in tools.manifest_records(manifest[12:]) if kind == tag]
        if len(values) != 1:
            raise ValueError('Resource verification requires unique manifest identity fields')
        return values[0]
    if field(resource_manifest, 12) != b'\x02':
        raise ValueError('The resource package must contain resources, not application code')
    if field(app_manifest, 13) != hashlib.sha256(resource_manifest).digest():
        raise ValueError('Resource manifest differs from the app binding')
    for tag in (1, 2):
        if field(app_manifest, tag) != field(resource_manifest, tag):
            raise ValueError('Resource App ID or publisher lineage differs from the app')
    # The isolated verifier trusts only the selected application's key. Key
    # rotation across an older resource generation needs an explicit trust flow.
    if field(app_manifest, 11) != field(resource_manifest, 11):
        raise ValueError('Verification currently requires the same signing key for app and resources')
    if tools.package_publisher_root(application) != tools.package_publisher_root(resource):
        raise ValueError('Resource publisher lineage differs from the app')


def stage_resource_media(tools, application, installed, state):
    """Expose verified resources through the desktop Host's read-only app root.

    The immutable SDK installer treats both kinds as packages. Use a separate
    installation tree so installing code cannot replace its bound resources.
    This hash is the public desktop_media_root.h application-key recipe.
    """
    publisher = tools.package_publisher_root(application)
    app_id = tools.package_manifest_identity(application).encode('ascii')
    if publisher is None or len(publisher) != 32 or not 1 <= len(app_id) <= 64:
        raise ValueError('Invalid application media identity')
    digest = hashlib.sha256(b'PXA-APPKEY-1' + publisher +
                            len(app_id).to_bytes(2, 'little') + app_id).hexdigest()
    media = state / 'media' / digest
    if not installed.is_dir() or installed.is_symlink():
        raise ValueError('Verified resource installation is missing')
    # Never follow links or copy special files into a scoped media grant.
    for entry in installed.rglob('*'):
        if entry.is_symlink() or not (entry.is_file() or entry.is_dir()):
            raise ValueError('Resource installation contains an unsafe file')
    media.parent.mkdir(exist_ok=True)
    shutil.copytree(installed, media)
    return media


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
    parser.add_argument('--resource-package',type=Path)
    parser.add_argument('--resource-sha256')
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
        if bool(args.resource_package) != bool(args.resource_sha256):
            raise ValueError('A resource package and its expected SHA-256 are required together')
        if args.resource_package:
            verify_resource(tools, args.package, args.resource_package, args.resource_sha256)
        args.state.mkdir(parents=True,exist_ok=False)
        key=args.state/'publisher.der';key.write_bytes(public[0])
        installer=simulator_bin('pxsys_package_installer')
        if args.resource_package:
            resource_state=args.state/'resource-install'
            resource_state.mkdir()
            subprocess.run([installer,'--storage-root',resource_state,'--publisher-key',key,
                            '--source',args.resource_package,'--expected-id',args.app_id],check=True,timeout=120)
            verify_resource(tools, args.package, args.resource_package, args.resource_sha256)
            installed=resource_state/'packages'/args.app_id
            if (installed/'current'/'manifest.pxm').is_file():
                installed=installed/'current'
            media=stage_resource_media(tools,args.package,installed,args.state)
            print('PXA verified resource media: '+str(media),flush=True)
        subprocess.run([installer,'--storage-root',args.state,'--publisher-key',key,
                        '--source',args.package,'--expected-id',args.app_id],check=True,timeout=120)
        sockets=Path(os.environ['PXA_SIMULATOR_SOCKET_ROOT'])
        token=hashlib.sha256(str(args.state).encode()).hexdigest()[:8]
        selector=args.profile+'@v'+token
        control=sockets/(selector+'.control.sock')
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
