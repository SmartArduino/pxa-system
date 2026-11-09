"""Use the DevKit's shared package/Host admission check without device writes."""
import json

from .common import ToolError


def inspect(environment, package, *, port, baud=2000000, device=None):
    package = environment.project(package)
    if not package.exists():
        raise ToolError('Build the intended device package before checking compatibility')
    if not port:
        raise ToolError('Select an explicit device port after discovery')
    arguments = ['compatibility', package, '--report-only']
    result = json.loads(device(*arguments) if device is not None else environment.device([
        *arguments, '--port', port, '--baud', str(baud)]))
    if result.get('schema') != 'pxa-package-compatibility-1' or result.get('status') not in (
            'compatible','incompatible','unknown'):
        raise ToolError('The selected SDK did not return a supported compatibility report')
    return result


def admit(report, *, strict=False):
    if report['status'] == 'incompatible' or strict and report['status'] == 'unknown':
        issues = [item for item in report['checks'] if item['status'] != 'compatible']
        raise ToolError('Package compatibility '+report['status']+'; nothing was uploaded. '+
            json.dumps(issues,ensure_ascii=False)+
            '. Keep pxa.lock. Use compatible app APIs/SDK or explicitly update the identified board firmware.')
