"""Keep the verified installation and activation on one host-side connection."""
from contextlib import contextmanager
import json

from .common import ToolError
from .host_tools import reader


def fields(text):
    return dict(part.split('=', 1) for part in text.strip().split(';') if '=' in part)


@contextmanager
def install_connection(environment, port, baud, expected_info):
    if type(baud) is not int or not 1200 <= baud <= 4000000:
        raise ToolError('Baud must be 1200..4000000')
    tools = reader(environment=environment)
    try:
        with tools.open_client(port, 10, baudrate=baud) as client:
            current = fields('\n'.join(frame.payload for frame in client.request('INFO')
                                      if frame.kind == 'OK'))
            for name in ('target', 'board', 'serial'):
                expected = expected_info.get(name)
                if expected and expected != 'unavailable' and current.get(name) != expected:
                    raise ToolError('Device '+name+' changed while building; nothing was uploaded')

            def device(*arguments, timeout=60):
                # Arguments are constructed by the installation workflow. Do
                # not interpret arbitrary shell or protocol command strings.
                if not arguments:
                    raise ToolError('Unsupported installation session operation')
                if arguments[0] == 'compatibility' and len(arguments) == 3 and arguments[2] == '--report-only':
                    return json.dumps(tools.package_compatibility(client, arguments[1]))
                if arguments == ('package', 'list'):
                    return '\n'.join(frame.payload for frame in client.request('PACKAGES')
                                     if frame.kind == 'PKG')
                if len(arguments) == 3 and arguments[:2] in (('package', 'run'), ('package', 'stop')):
                    return '\n'.join(frame.payload for frame in client.request(
                        'PACKAGE '+arguments[1]+' '+arguments[2], timeout=min(timeout, 10)))
                if arguments[:2] == ('package', 'install'):
                    parsed = tools.build_parser().parse_args([
                        *map(str, arguments), '--port', port, '--baud', str(baud), '--timeout', '10'])
                    tools.command_package_install(parsed, connection=client, quiet=True)
                    return 'Installation committed on the existing connection\n'
                raise ToolError('Unsupported installation session operation')

            yield device
    except (tools.PxaDbError, OSError) as error:
        if str(error).split(';', 1)[0] == 'install_status=-4':
            raise ToolError(
                'Device refused installation (install_status=-4). Check trust policy '
                'and device-bound installation/publisher records on the selected volume. '
                'Preserve existing code and saves; obtain a verified diagnosis before '
                'changing signatures or storage records. Installation was not retried.'
            ) from error
        raise ToolError(str(error)) from error
