"""Current debug protocol tools, independent of an app's immutable compiler SDK."""
from pathlib import Path
import os
import importlib.util
import sys

from .common import KIT, ToolError


def directory(*, environment=None, sdk=None):
    candidates = []
    if os.environ.get('PXA_AGENT_HOST_TOOLS'):
        candidates.append(Path(os.environ['PXA_AGENT_HOST_TOOLS']))
    candidates.append(KIT/'host_tools')
    # A source checkout keeps the canonical clients in the parent workspace.
    if (KIT.parent/'sdk/guest-c').is_dir() and len(KIT.parents) > 2:
        candidates.append(KIT.parents[2]/'tools/pxadb')
    if environment is not None:
        if environment.native:
            sdk = environment.config.get('sdk')
        elif environment.config.get('host_tools'):
            candidates.append(Path(environment.config['host_tools']))
    if sdk is not None:
        candidates.append(Path(sdk)/'tools/pxadb')
    for candidate in candidates:
        if (candidate/'pxadb.py').is_file():
            return candidate.resolve()
    raise ToolError('PXA host tools are missing; repair the AI Kit or install its matching host-tools bundle')


def client(*, environment=None, sdk=None):
    return directory(environment=environment,sdk=sdk)/'pxadb.py'


def reader(*, environment=None, sdk=None):
    spec = importlib.util.spec_from_file_location('pxa_agent_host_reader',client(environment=environment,sdk=sdk))
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def bridge(*, sdk):
    tools = directory(sdk=sdk)
    candidates = [tools/'simulator_pxadb.py',tools.parent/'simulator_pxadb.py',
                  Path(sdk)/'tools/simulator_pxadb.py']
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise ToolError('Simulator debug bridge is missing; repair the AI Kit/DevKit')
