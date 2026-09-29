#!/usr/bin/env python3
"""Compile both generated codecs and check every Core golden vector."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path


SPEC = Path(__file__).resolve().parent.parent
SYSTEM = SPEC.parent.parent


def c_array(data: bytes) -> str:
    return ", ".join(f"0x{value:02x}" for value in data) or "0"


def source(header: str, service_header: str, window_header: str,
           guest: bool) -> str:
    core = json.loads((SPEC / "golden/core-vectors.json").read_text())
    v1 = json.loads((SPEC / "abi-1.0-envelope.json").read_text())
    device = json.loads((SPEC / "golden/device-runtime-info.json").read_text())
    messages = [item for item in core["vectors"] if item["kind"] == "message"]
    records = [item for item in core["vectors"] if item["kind"] == "records"]
    window_records = next(item for item in records
                          if item["name"] == "window.snapshot.records")
    window_result = next(item for item in messages
                         if item["name"] == "window.get-snapshot.result")
    wire = "pxa_wire" if guest else "pxa_wire_v1"
    macro = "PXA_WIRE" if guest else "PXA_WIRE_V1"
    lines = [
        "#include <stddef.h>",
        "#include <stdint.h>",
        "#include <stdio.h>",
        "#include <string.h>",
        f'#include "{header}"',
        f'#include "{service_header}"',
        f'#include "{window_header}"',
    ]
    if not guest:
        lines += [
            "static int check_v0(const uint8_t *wire, size_t size,",
            "                    uint16_t service, uint16_t opcode, uint32_t id,",
            "                    const uint8_t *payload, size_t payload_size) {",
            "    uint8_t encoded[4096];",
            "    size_t written = 0;",
            "    pxa_wire_v0_view_t view;",
            "    return !pxa_wire_v0_decode(wire, size, 4096, &view) ||",
            "           view.service != service || view.opcode != opcode ||",
            "           view.request_id != id || view.payload_size != payload_size ||",
            "           memcmp(view.payload, payload, payload_size) != 0 ||",
            "           !pxa_wire_v0_encode(encoded, sizeof(encoded), service, opcode,",
            "                               id, payload, payload_size, &written) ||",
            "           written != size || memcmp(encoded, wire, size) != 0;",
            "}",
        ]
    lines += [
        "static int check_v1(const uint8_t *wire, size_t size,",
        "                    uint16_t service, uint16_t opcode, uint64_t token,",
        "                    const uint8_t *payload, size_t payload_size) {",
        "    uint8_t encoded[4096];",
        "    size_t written = 0;",
        f"    {wire}_view_t view;",
        f"    return !{wire}_decode(wire, size, 4096, &view) ||",
        "           view.service != service || view.opcode != opcode ||",
        "           view.request_token != token || view.payload_size != payload_size ||",
        "           memcmp(view.payload, payload, payload_size) != 0 ||",
        f"           !{wire}_encode(encoded, sizeof(encoded), service, opcode,",
        "                               token, payload, payload_size, &written) ||",
        "           written != size || memcmp(encoded, wire, size) != 0;",
        "}",
        "static int check_record(const uint8_t *wire, size_t remaining,",
        "                        uint16_t raw_tag, const uint8_t *payload,",
        "                        size_t payload_size, size_t *consumed) {",
        "    uint8_t encoded[65539];",
        "    size_t written = 0;",
        "    pxa_wire_record_view_t view;",
        "    if (!pxa_wire_record_decode(wire, remaining, &view, consumed) ||",
        "        view.raw_tag != raw_tag ||",
        "        view.tag != (raw_tag & PXA_WIRE_RECORD_TAG_MASK) ||",
        "        view.optional !=",
        "          ((raw_tag & PXA_WIRE_RECORD_OPTIONAL_MASK) != 0) ||",
        "        view.payload_size != payload_size ||",
        "        memcmp(view.payload, payload, payload_size) != 0 ||",
        "        !pxa_wire_record_encode(encoded, sizeof(encoded), raw_tag,",
        "                                payload, payload_size, &written) ||",
        "        written != *consumed ||",
        "        memcmp(encoded, wire, written) != 0) return 1;",
        "    return 0;",
        "}",
        "int main(void) {",
        f"    {wire}_view_t view;",
    ]
    if not guest:
        for index, item in enumerate(messages):
            message = item["message"]
            wire_bytes = bytes.fromhex(item["wire_hex"])
            payload = bytes.fromhex(message["payload_hex"])
            lines += [
                f"    static const uint8_t wire_{index}[] = "
                f"{{{c_array(wire_bytes)}}};",
                f"    static const uint8_t payload_{index}[] = "
                f"{{{c_array(payload)}}};",
                f"    if (check_v0(wire_{index}, sizeof(wire_{index}), "
                f"{message['service']}, {message['opcode']}, "
                f"{message['request_id']}, payload_{index}, {len(payload)})) {{",
                f'        fprintf(stderr, "v0 golden failed: {item["name"]}\\n");',
                "        return 1;",
                "    }",
            ]
    for index, item in enumerate(records):
        record_bytes = bytes.fromhex(item["wire_hex"])
        lines += [
            f"    static const uint8_t records_{index}[] = "
            f"{{{c_array(record_bytes)}}};",
            f"    size_t offset_{index} = 0;",
        ]
        for ordinal, record in enumerate(item["records"]):
            payload = bytes.fromhex(record["payload_hex"])
            lines += [
                f"    static const uint8_t record_payload_{index}_{ordinal}[] = "
                f"{{{c_array(payload)}}};",
                "    {",
                "        size_t consumed = 0;",
                f"        if (check_record(records_{index} + offset_{index}, "
                f"sizeof(records_{index}) - offset_{index}, "
                f"{record['tag']}, record_payload_{index}_{ordinal}, "
                f"{len(payload)}, &consumed)) {{",
                f'            fprintf(stderr, "record golden failed: '
                f'{item["name"]} / {ordinal}\\n");',
                "            return 7;",
                "        }",
                f"        offset_{index} += consumed;",
                "    }",
            ]
        lines.append(
            f"    if (offset_{index} != sizeof(records_{index})) return 8;")
    golden = v1["golden"]
    v1_wire = bytes.fromhex(golden["wire_hex"])
    v1_payload = bytes.fromhex(golden["payload_hex"])
    lines += [
        f"    static const uint8_t wire_v1[] = {{{c_array(v1_wire)}}};",
        f"    static const uint8_t payload_v1[] = {{{c_array(v1_payload)}}};",
        "    if (check_v1(wire_v1, sizeof(wire_v1), "
        f"{golden['service']}, {golden['opcode']}, "
        f"UINT64_C({golden['request_token']}), payload_v1, "
        "sizeof(payload_v1))) return 2;",
        "    for (size_t length = 0; length < sizeof(wire_v1); ++length)",
        f"        if ({wire}_decode(wire_v1, length, 4096, &view)) return 3;",
        "    uint8_t malformed[sizeof(wire_v1)];",
        "    memcpy(malformed, wire_v1, sizeof(malformed));",
        f"    malformed[{macro}_FLAGS_OFFSET] = 1;",
        f"    if ({wire}_decode(malformed, sizeof(malformed), 4096, &view)) return 4;",
        "    memcpy(malformed, wire_v1, sizeof(malformed));",
        f"    malformed[{macro}_PAYLOAD_LEN_OFFSET] = 4;",
        f"    if ({wire}_decode(malformed, sizeof(malformed), 4096, &view)) return 5;",
        "    memcpy(malformed, wire_v1, sizeof(malformed));",
        f"    malformed[{macro}_SERVICE_OFFSET] = 0;",
        f"    if ({wire}_decode(malformed, sizeof(malformed), 4096, &view)) return 6;",
        "    {",
        "        uint8_t invalid_record[4] = {0, 0x80, 0, 0};",
        "        pxa_wire_record_view_t record_view;",
        "        size_t consumed = 0;",
        "        if (pxa_wire_record_decode(invalid_record, 4, &record_view,",
        "                                   &consumed)) return 9;",
        "        if (pxa_wire_record_encode(invalid_record, 4, 0x8000, NULL,",
        "                                   0, &consumed)) return 10;",
        "        invalid_record[0] = 1;",
        "        invalid_record[2] = 1;",
        "        if (pxa_wire_record_decode(invalid_record, 4, &record_view,",
        "                                   &consumed)) return 11;",
        "    }",
        f"    static const uint8_t device_result[] = "
        f"{{{c_array(bytes.fromhex(device['result_hex']))}}};",
        "    uint8_t encoded_device[256] = {0};",
        "    size_t device_size = 0;",
        "    pxa_device_runtime_info_payload_t info;",
        "    if (!pxa_device_runtime_info_encode(",
        "            encoded_device + 4, sizeof(encoded_device) - 4,",
        f'            "{device["target"]}", "{device["architecture"]}",',
        f'            "{device["engine"]}", "{device["engine_abi"]}",',
        f"            {device['formats']}, &device_size) ||",
        "        device_size + 4 != sizeof(device_result) ||",
        "        memcmp(encoded_device, device_result, sizeof(device_result)) != 0)",
        "        return 12;",
        "    if (!pxa_device_runtime_info_decode(device_result,",
        "                                        sizeof(device_result), &info) ||",
        f'        strcmp(info.target, "{device["target"]}") != 0 ||',
        f'        strcmp(info.architecture, "{device["architecture"]}") != 0 ||',
        f'        strcmp(info.engine, "{device["engine"]}") != 0 ||',
        f'        strcmp(info.engine_abi, "{device["engine_abi"]}") != 0 ||',
        f"        info.formats != {device['formats']}) return 13;",
        "    for (size_t length = 0; length < sizeof(device_result); ++length)",
        "        if (pxa_device_runtime_info_decode(device_result, length, &info))",
        "            return 14;",
        "    uint8_t malformed_device[sizeof(device_result)];",
        "    memcpy(malformed_device, device_result, sizeof(device_result));",
        "    malformed_device[8] = 0xc0;",
        "    if (pxa_device_runtime_info_decode(malformed_device,",
        "                                       sizeof(malformed_device), &info))",
        "        return 15;",
        "    memcpy(malformed_device, device_result, sizeof(device_result));",
        "    malformed_device[4] = 2;",
        "    if (pxa_device_runtime_info_decode(malformed_device,",
        "                                       sizeof(malformed_device), &info))",
        "        return 16;",
        "    char oversized_target[33];",
        "    memset(oversized_target, 'a', 32);",
        "    oversized_target[32] = '\\0';",
        "    if (pxa_device_runtime_info_encode(",
        "            encoded_device, sizeof(encoded_device), oversized_target,",
        f'            "{device["architecture"]}", "{device["engine"]}",',
        f'            "{device["engine_abi"]}", {device["formats"]},',
        "            &device_size)) return 17;",
        "    char max_target[32], max_architecture[24];",
        "    char max_engine[24], max_engine_abi[80];",
        "    memset(max_target, 'a', 31); max_target[31] = '\\0';",
        "    memset(max_architecture, 'b', 23); max_architecture[23] = '\\0';",
        "    memset(max_engine, 'c', 23); max_engine[23] = '\\0';",
        "    memset(max_engine_abi, 'd', 79); max_engine_abi[79] = '\\0';",
        "    if (!pxa_device_runtime_info_encode(",
        "            encoded_device, PXA_DEVICE_RUNTIME_INFO_MAX_RECORD_BYTES,",
        "            max_target, max_architecture, max_engine, max_engine_abi,",
        "            3, &device_size) ||",
        "        device_size != PXA_DEVICE_RUNTIME_INFO_MAX_RECORD_BYTES)",
        "        return 19;",
        "    memset(encoded_device, 0, 4);",
        "    if (!pxa_device_runtime_info_encode(",
        "            encoded_device + 4, sizeof(encoded_device) - 4,",
        '            "\\xc3\\xa9", "x86_64", "wamr", "abi", 3, &device_size) ||',
        "        !pxa_device_runtime_info_decode(encoded_device,",
        "                                        device_size + 4, &info) ||",
        '        strcmp(info.target, "\\xc3\\xa9") != 0) return 18;',
        f"    static const uint8_t window_records[] = "
        f"{{{c_array(bytes.fromhex(window_records['wire_hex']))}}};",
        f"    static const uint8_t window_result[] = "
        f"{{{c_array(bytes.fromhex(window_result['message']['payload_hex']))}}};",
        "    pxa_window_snapshot_wire_t snapshot = {0};",
        "    uint8_t encoded_window[PXA_WINDOW_SNAPSHOT_RECORD_BYTES];",
        "    size_t window_size = 0;",
        "    snapshot.revision = 1;",
        "    snapshot.logical_width = 320; snapshot.logical_height = 240;",
        "    snapshot.pixel_width = 640; snapshot.pixel_height = 480;",
        "    snapshot.density_numerator = 2;",
        "    snapshot.density_denominator = 1;",
        "    snapshot.safe_insets.top = 24;",
        "    snapshot.safe_insets.bottom = 12;",
        "    snapshot.system_bar_insets.top = 20;",
        "    snapshot.system_bar_insets.bottom = 10;",
        "    snapshot.orientation = 2; snapshot.focused = 1;",
        "    if (!pxa_window_snapshot_records_encode(",
        "            encoded_window, sizeof(encoded_window), &snapshot,",
        "            &window_size) || window_size != sizeof(window_records) ||",
        "        memcmp(encoded_window, window_records, window_size) != 0)",
        "        return 20;",
        "    if (!pxa_window_snapshot_result_decode(",
        "            window_result, sizeof(window_result), &snapshot) ||",
        "        snapshot.revision != 1 || snapshot.pixel_width != 640 ||",
        "        snapshot.safe_insets.top != 24 ||",
        "        snapshot.system_bar_insets.bottom != 10 ||",
        "        snapshot.orientation != 2 || snapshot.focused != 1)",
        "        return 21;",
        "    if (!pxa_window_snapshot_records_decode(",
        "            window_records, sizeof(window_records), &snapshot) ||",
        "        snapshot.revision != 1 || snapshot.safe_insets.top != 24)",
        "        return 25;",
        "    if (pxa_window_snapshot_records_encode(",
        "            encoded_window, sizeof(encoded_window) - 1, &snapshot,",
        "            &window_size)) return 26;",
        "    for (size_t length = 0; length < sizeof(window_result); ++length)",
        "        if (pxa_window_snapshot_result_decode(",
        "                window_result, length, &snapshot)) return 22;",
        "    uint8_t malformed_window[sizeof(window_result)];",
        "    memcpy(malformed_window, window_result, sizeof(window_result));",
        "    malformed_window[4] = 2;",
        "    if (pxa_window_snapshot_result_decode(",
        "            malformed_window, sizeof(malformed_window), &snapshot))",
        "        return 23;",
        "    memcpy(malformed_window, window_result, sizeof(window_result));",
        "    malformed_window[5] = 0x80;",
        "    if (pxa_window_snapshot_result_decode(",
        "            malformed_window, sizeof(malformed_window), &snapshot))",
        "        return 24;",
        f"    puts(\""
        + ("" if guest else f"{len(messages)} v0 messages, ")
        + f"{len(records)} record lists, envelope, Device and Window payloads OK\");",
        "    return 0;",
        "}",
        "",
    ]
    return "\n".join(lines)


def main() -> int:
    compiler = sys.argv[1] if len(sys.argv) > 1 else "cc"
    with tempfile.TemporaryDirectory(prefix="pxa-wire-codec-test-") as temp:
        work = Path(temp)
        for name, header, service_header, window_header, include, guest in (
            ("host", "pxa/wire_generated.h",
             "pxa/device_runtime_info_generated.h",
             "pxa/window_snapshot_generated.h", SYSTEM / "libpxa/include", False),
            ("guest", "pxa_wire.h",
             "pxa_device_runtime_info_generated.h",
             "pxa_window_snapshot_generated.h", SYSTEM / "sdk/guest-c/include", True),
        ):
            input_path = work / f"{name}.c"
            output_path = work / name
            input_path.write_text(
                source(header, service_header, window_header, guest),
                encoding="utf-8")
            subprocess.run([
                compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                "-Wpedantic", f"-I{include}", str(input_path), "-o",
                str(output_path),
            ], check=True)
            subprocess.run([str(output_path)], check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
