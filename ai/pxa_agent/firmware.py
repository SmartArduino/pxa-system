"""Download official signed firmware and preserve data on matching layouts."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time
import re

from .common import ToolError, artifact, atomic_json, data_root, evidence, digest, request, signed_document
from .workbench import info_fields

BOARDS = ('pai-touch', 'doit-gamekit', 'doit-gamekit-1', 'doit-gamekit-2', 'esp32s31-korvo-1', 'esp-mosaico', 'sensecap-watcher', 'doit-esp32s31-devkit', 'espp4-a')


def partition_table(data):
    """Decode the ROM's real table, including its integrity marker."""
    result, consumed, checksum_seen = {}, bytearray(), False
    for offset in range(0, len(data) - 31, 32):
        entry = data[offset:offset + 32]
        magic = struct.unpack_from('<H', entry)[0]
        if magic == 0xffff:
            break
        if magic == 0xebeb:
            if checksum_seen or entry[16:] != hashlib.md5(consumed, usedforsecurity=False).digest():
                raise ToolError('The installed partition table has an invalid MD5')
            checksum_seen = True
            continue
        if magic != 0x50aa or checksum_seen:
            raise ToolError('The installed flash has no valid PXA partition table')
        _, kind, subtype, start, size, raw_name, flags = struct.unpack('<HBBII16sI', entry)
        name = raw_name.split(b'\0')[0].decode('ascii')
        if not name or name in result or not size:
            raise ToolError('Invalid installed partition entry')
        result[name] = {'offset': start, 'size': size}
        consumed.extend(entry)
    if not result:
        raise ToolError('The device has no readable partition table')
    previous_end = 0
    for entry in sorted(result.values(), key=lambda item: item['offset']):
        if entry['offset'] < previous_end:
            raise ToolError('The installed partition table overlaps')
        previous_end = entry['offset'] + entry['size']
    return result


def release_descriptor(board, *, version='latest', manifest=None):
    if board not in BOARDS:
        raise ToolError('Choose a supported firmware board')
    if version == 'latest':
        with request('https://api.github.com/repos/SmartArduino/pxa-workspace/releases/latest') as response:
            version = json.load(response)['tag_name'].removeprefix('v')
    if not re.fullmatch(r'\d+\.\d+\.\d+', version):
        raise ToolError('Invalid official firmware release version')
    source = manifest or f'https://github.com/SmartArduino/pxa-workspace/releases/download/v{version}/firmware-manifest.json'
    rows, raw, signature = signed_document(source)
    if not isinstance(rows, list) or any(not isinstance(row,dict) for row in rows):
        raise ToolError('Invalid official firmware manifest')
    matches = [row for row in rows if row.get('board') == board and row.get('version') == version]
    if len(matches) != 1:
        raise ToolError('This release has no unique signed firmware for the selected board')
    row = matches[0]
    if row['chip'] != ('esp32p4' if board == 'espp4-a' else 'esp32s3' if board in ('pai-touch', 'sensecap-watcher') else 'esp32s31'):
        raise ToolError('Firmware board/CPU mismatch')
    return row, source


def prepare(board, *, version='latest', mode='app', manifest=None):
    if board not in BOARDS or mode not in ('app', 'full'):
        raise ToolError('Choose a supported board and app/full mode')
    row, source = release_descriptor(board,version=version,manifest=manifest)
    version = row['version']
    from .environment import locate
    item = row['application' if mode == 'app' else 'merged']
    image = artifact(locate(source, item['file']), item, maximum=64 * 1024**2)
    regions = [region for region in row['regions'] if region['sha256'] == row['application']['sha256']]
    if len(regions) != 1:
        raise ToolError('The application offset is ambiguous')
    expected = {part['name']: {'offset': part['offset'], 'size': part['size']} for part in row['partitions']}
    if mode == 'app':
        start = regions[0]['offset']
        if not any(part['offset'] == start and item['bytes'] <= part['size'] for part in row['partitions']):
            raise ToolError('Firmware exceeds its signed application partition')
    elif item['bytes'] != row['flash_bytes']:
        raise ToolError('Merged image does not match the board flash capacity')
    return {'board': board, 'version': version, 'chip': row['chip'], 'mode': mode,
            'image': str(image), 'sha256': item['sha256'], 'bytes': item['bytes'],
            'offset': regions[0]['offset'] if mode == 'app' else 0,
            'partitions': expected, 'signature_verified': True,
            'preserves_data': mode == 'app', 'requires_matching_partition_table': mode == 'app',
            'full_flash_erases_apps_saves_settings': mode == 'full',
            'bootloader_hint': 'Hold BOOT, press RESET, release BOOT; the port may change.' if row['chip'] == 'esp32s31' else None}


def status(environment, *, port, board=None, baud=2000000, version='latest', manifest=None):
    """Read INFO and signed release metadata; never enter ROM or download an image."""
    if not port:
        raise ToolError('Firmware status needs an explicit device port')
    from .device_profile import connection_help
    try:
        info = info_fields(environment.device(['info','--port',port,'--baud',str(baud)],timeout=12))
    except ToolError as error:
        return {'connected':False,'port':port,'error':str(error),'connection_steps':connection_help()}
    reported = info.get('board')
    if board is not None and reported and board != reported:
        raise ToolError('The device reports board '+reported+'; requested '+board+'. No firmware was written.')
    board = board or reported
    if board not in BOARDS:
        return {'connected':True,'port':port,'device':info,'recognized_board':False,
                'firmware_origin':'unverified','status':'unknown',
                'next_steps':['Confirm the exact board and CPU before choosing firmware.',
                              'Use https://pxa.doit.am/flash/ for supported hardware, or the board adaptation workflow for a custom board.']}
    row,source = release_descriptor(board,version=version,manifest=manifest)
    target = {'esp32-s3':'esp32s3','esp32-s31':'esp32s31','esp32-p4':'esp32p4'}.get(info.get('target'),info.get('target'))
    if target is not None and target != row['chip']:
        raise ToolError('The reported board and CPU disagree; no firmware was written')
    current, selected = info.get('pxa_version'), row['version']
    comparison = 'unknown'
    if current and re.fullmatch(r'\d+\.\d+\.\d+',current):
        left,right = tuple(map(int,current.split('.'))),tuple(map(int,selected.split('.')))
        comparison = 'same_release' if left == right else 'update_available' if left < right else 'device_newer'
    return {'connected':True,'port':port,'board':board,'recognized_board':True,'device':info,
            'firmware_origin':'unverified','status':comparison,
            'selected_release':selected,'selected_chip':row['chip'],'signature_verified':True,
            'manifest':str(source),'image_downloaded':False,'flash_completed':False,
            'app_only':{'partition_match':'unknown','preserves_data_when_layout_matches':True,
                        'note':'The real partition table is checked only during an explicitly requested upgrade. Release numbers do not prove a matching layout or official running image.'},
            'commands':{'prepare':['firmware','prepare','--board',board,'--release',selected],
                        'upgrade':['firmware','upgrade','--board',board,'--release',selected,'--port',port]},
            'note':'Firmware release, Core ABI, service versions and AOT ABI are separate. Compare the intended package before installation; status never upgrades automatically.'}


def flash(environment, board, *, port, baud=2000000, version='latest', mode='app', erase_data=False, manifest=None):
    if not port:
        raise ToolError('Firmware flashing needs an explicit board and device port')
    if mode == 'full' and not erase_data:
        raise ToolError('Full firmware erases apps, saves and settings. Back up first; explicit --erase-data authorization is required.')
    prepared = prepare(board, version=version, mode=mode, manifest=manifest)
    info = None
    try:
        info = info_fields(environment.device(['info', '--port', port, '--baud', str(baud)], timeout=12))
    except ToolError:
        pass  # A board explicitly selected by the user may already be in ROM.
    if info and info.get('board') and info['board'] != board:
        raise ToolError('The connected board identifies as ' + info['board'] + '; no firmware was written')
    if info and info.get('target') != prepared['chip']:
        raise ToolError('The connected CPU does not match the selected board')
    output = evidence(environment.workspace, 'pxa-firmware')
    chip = prepared['chip']
    if chip == 'esp32s31':
        from contextlib import redirect_stderr, redirect_stdout
        from .firmware_session import write
        with (output / 'flash.log').open('w') as log, redirect_stdout(log), redirect_stderr(log):
            port, serial_id = write(prepared, board, port, baud, output,
                                    info.get('serial') if info else None)
        boot = None
        for _ in range(8):
            time.sleep(1)
            if serial_id:
                from serial.tools.list_ports import comports
                matches = [p for p in comports() if p.product == 'ESP-Mosaico'
                           and (p.serial_number or '').lower() == serial_id]
                if len(matches) != 1:
                    continue
                port = matches[0].device
            try:
                boot = info_fields(environment.device(['info', '--port', port, '--baud', str(baud)], timeout=3))
                if boot.get('pxa_version') == prepared['version']:
                    break
            except ToolError:
                continue
        verified = bool(boot and boot.get('pxa_version') == prepared['version'])
        return {**prepared, 'flash_completed': True, 'boot_verified': verified,
                'port': port, 'evidence': str(output),
                'next_step': None if verified else 'Press RESET and run device-profile to verify the installed version.'}
    base = [sys.executable, '-m', 'esptool', '--chip', chip, '--port', port, '--baud', str(baud)]
    before = 'no-reset' if chip == 'esp32s31' else 'default-reset'
    stub = ['--no-stub'] if chip == 'esp32s31' else []
    if mode == 'app':
        table = output / 'installed-partitions.bin'
        command = [*base, '--before', before, '--after', 'no-reset' if chip == 'esp32s31' else 'hard-reset', *stub,
                   'read-flash', '0x8000', '0x1000', str(table)]
        with (output / 'read-partitions.log').open('w') as log:
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=90)
        if result.returncode or not table.is_file():
            raise ToolError('Could not read the installed partition table. No firmware was written; see ' + str(output))
        actual = partition_table(table.read_bytes())
        if actual != prepared['partitions']:
            atomic_json(output / 'partition-mismatch.json', {'installed': actual, 'release': prepared['partitions']})
            raise ToolError('Partition layouts differ. App-only flashing was blocked to preserve data. Back up before an explicitly authorized full migration. See ' + str(output))
        before = 'no-reset' if chip == 'esp32s31' else 'default-reset'
    if digest(prepared['image']) != prepared['sha256']:
        raise ToolError('Cached firmware changed after verification; no firmware was written')
    command = [*base, '--before', before, '--after', 'no-reset' if chip == 'esp32s31' else 'hard-reset',
               *stub, 'write-flash', hex(prepared['offset']), prepared['image']]
    with (output / 'flash.log').open('w') as log:
        result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=900)
    if result.returncode:
        raise ToolError('Firmware programming did not complete; inspect ' + str(output / 'flash.log'))
    boot = None
    if chip in ('esp32s3', 'esp32p4'):
        for _ in range(8):
            time.sleep(1)
            try:
                boot = info_fields(environment.device(['info', '--port', port, '--baud', str(baud)], timeout=3))
                if boot.get('pxa_version') == prepared['version']:
                    break
            except ToolError:
                continue
    verified = bool(boot and boot.get('pxa_version') == prepared['version'])
    return {**prepared, 'flash_completed': True, 'boot_verified': verified,
            'port': port, 'evidence': str(output),
            'next_step': None if verified else 'Boot normally, enable Settings / PXADB debugging where available, then run device-profile to verify the installed version.'}
