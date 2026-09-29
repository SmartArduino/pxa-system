#!/usr/bin/env python3
"""Generate identical freestanding envelope codecs for Host and Guest."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


SPEC = Path(__file__).resolve().parent.parent
SYSTEM = SPEC.parent.parent
HOST_OUTPUT = SYSTEM / "libpxa/include/pxa/wire_generated.h"
GUEST_OUTPUT = SYSTEM / "sdk/guest-c/include/pxa_wire.h"
OUTPUTS = (HOST_OUTPUT, GUEST_OUTPUT)
WIDTHS = {"u16": 2, "u32": 4, "u64": 8}


def layout(path: Path, envelope: dict, label: str) -> dict:
    fields = envelope["fields"]
    cursor = 0
    for field in fields:
        width = WIDTHS[field["type"]]
        if field["offset"] != cursor:
            raise ValueError(f"{label}: field {field['name']} is not contiguous")
        cursor += width
    if cursor != envelope["size"]:
        raise ValueError(f"{label}: envelope size disagrees with fields")
    return {
        "name": label,
        "size": cursor,
        "fields": fields,
        "max_size": envelope["max_control_message"],
        "source": path.name,
    }


def layouts() -> tuple[dict, dict]:
    core_path = SPEC / "pxa-core.json"
    v1_path = SPEC / "abi-1.0-envelope.json"
    core = json.loads(core_path.read_text(encoding="utf-8"))
    v1 = json.loads(v1_path.read_text(encoding="utf-8"))
    legacy = layout(core_path, {
        **core["envelope"],
        "max_control_message": core["abi"]["max_control_message"],
    }, "v0")
    current = layout(v1_path, v1, "v1")
    if legacy["max_size"] != current["max_size"]:
        raise ValueError("Host and Guest envelope limits disagree")
    return legacy, current


def record_layout() -> dict:
    core = json.loads((SPEC / "pxa-core.json").read_text(encoding="utf-8"))
    record = core["record"]
    cursor = 0
    for field in record["fields"]:
        if field["offset"] != cursor:
            raise ValueError("record header fields are not contiguous")
        cursor += WIDTHS[field["type"]]
    if cursor != record["header_size"]:
        raise ValueError("record header size disagrees with fields")
    return record


def emit_record(record: dict) -> list[str]:
    fields = {field["name"]: field for field in record["fields"]}
    tag = fields["tag"]["offset"]
    length = fields["payload_len"]["offset"]
    return [
        f"#define PXA_WIRE_RECORD_SIZE ((size_t){record['header_size']})",
        f"#define PXA_WIRE_RECORD_TAG_OFFSET ((size_t){tag})",
        f"#define PXA_WIRE_RECORD_PAYLOAD_LEN_OFFSET ((size_t){length})",
        f"#define PXA_WIRE_RECORD_OPTIONAL_MASK UINT16_C({record['optional_mask']})",
        f"#define PXA_WIRE_RECORD_TAG_MASK UINT16_C({record['tag_mask']})",
        "",
        "typedef struct {",
        "    uint16_t raw_tag;",
        "    uint16_t tag;",
        "    uint8_t optional;",
        "    const uint8_t *payload;",
        "    uint16_t payload_size;",
        "} pxa_wire_record_view_t;",
        "",
        "static inline int pxa_wire_record_decode(",
        "    const uint8_t *data, size_t size, pxa_wire_record_view_t *output,",
        "    size_t *consumed) {",
        "    uint16_t raw_tag;",
        "    uint16_t payload_size;",
        "    if (consumed != NULL) *consumed = 0;",
        "    if (output == NULL || consumed == NULL) return 0;",
        "    output->raw_tag = 0;",
        "    output->tag = 0;",
        "    output->optional = 0;",
        "    output->payload = NULL;",
        "    output->payload_size = 0;",
        "    if (data == NULL || size < PXA_WIRE_RECORD_SIZE) return 0;",
        "    raw_tag = pxa_wire_generated_load_u16(",
        "        data + PXA_WIRE_RECORD_TAG_OFFSET);",
        "    payload_size = pxa_wire_generated_load_u16(",
        "        data + PXA_WIRE_RECORD_PAYLOAD_LEN_OFFSET);",
        "    if ((raw_tag & PXA_WIRE_RECORD_TAG_MASK) == 0 ||",
        "        payload_size > size - PXA_WIRE_RECORD_SIZE) return 0;",
        "    output->raw_tag = raw_tag;",
        "    output->tag = raw_tag & PXA_WIRE_RECORD_TAG_MASK;",
        "    output->optional =",
        "        (uint8_t)((raw_tag & PXA_WIRE_RECORD_OPTIONAL_MASK) != 0);",
        "    output->payload = data + PXA_WIRE_RECORD_SIZE;",
        "    output->payload_size = payload_size;",
        "    *consumed = PXA_WIRE_RECORD_SIZE + payload_size;",
        "    return 1;",
        "}",
        "",
        "static inline int pxa_wire_record_encode(",
        "    uint8_t *out, size_t capacity, uint16_t raw_tag,",
        "    const uint8_t *payload, size_t payload_size, size_t *written) {",
        "    if (written != NULL) *written = 0;",
        "    if (out == NULL || written == NULL ||",
        "        (raw_tag & PXA_WIRE_RECORD_TAG_MASK) == 0 ||",
        "        (payload == NULL && payload_size != 0) ||",
        "        payload_size > UINT16_MAX ||",
        "        capacity < PXA_WIRE_RECORD_SIZE + payload_size) return 0;",
        "    pxa_wire_generated_store_u16(",
        "        out + PXA_WIRE_RECORD_TAG_OFFSET, raw_tag);",
        "    pxa_wire_generated_store_u16(",
        "        out + PXA_WIRE_RECORD_PAYLOAD_LEN_OFFSET,",
        "        (uint16_t)payload_size);",
        "    for (size_t i = 0; i < payload_size; ++i)",
        "        out[PXA_WIRE_RECORD_SIZE + i] = payload[i];",
        "    *written = PXA_WIRE_RECORD_SIZE + payload_size;",
        "    return 1;",
        "}",
        "",
    ]


def emit_layout(schema: dict) -> list[str]:
    name = schema["name"]
    prefix = f"PXA_WIRE_{name.upper()}" if name else "PXA_WIRE"
    function = f"pxa_wire_{name}" if name else "pxa_wire"
    fields = schema["fields"]
    payload = next(field for field in fields if field["name"] == "payload_len")
    body = [
        f"#define {prefix}_SIZE ((size_t){schema['size']})",
        *(f"#define {prefix}_{field['name'].upper()}_OFFSET "
          f"((size_t){field['offset']})" for field in fields),
        "",
        "typedef struct {",
        *(f"    uint{WIDTHS[field['type']] * 8}_t {field['name']};"
          for field in fields if field["name"] not in ("payload_len", "flags")),
        "    const uint8_t *payload;",
        "    uint32_t payload_size;",
        f"}} {function}_view_t;",
        "",
        f"static inline int {function}_decode(",
        "    const uint8_t *data, size_t size, size_t max_size,",
        f"    {function}_view_t *output) {{",
        "    uint32_t payload_size;",
        "    if (output == NULL) return 0;",
        *(f"    output->{field['name']} = 0;" for field in fields
          if field["name"] not in ("payload_len", "flags")),
        "    output->payload = NULL;",
        "    output->payload_size = 0;",
    ]
    conditions = ["data == NULL", f"size < {prefix}_SIZE", "size > max_size"]
    if name in ("v1", ""):
        conditions.append("size > PXA_WIRE_MAX_CONTROL_MESSAGE")
    body += [
        "    if (" + " ||\n        ".join(conditions) + ") return 0;",
        f"    payload_size = pxa_wire_generated_load_u32(",
        f"        data + {prefix}_{payload['name'].upper()}_OFFSET);",
        f"    if ((size_t)payload_size != size - {prefix}_SIZE) return 0;",
    ]
    for field in fields:
        if "required_value" in field or field.get("required_nonzero"):
            comparison = (f"!= {field['required_value']}" if "required_value" in field
                          else "== 0")
            body += [
                f"    if (pxa_wire_generated_load_{field['type']}(data + "
                f"{prefix}_{field['name'].upper()}_OFFSET) "
                f"{comparison}) return 0;"
            ]
    for field in fields:
        if field["name"] in ("payload_len", "flags"):
            continue
        body += [
            f"    output->{field['name']} = pxa_wire_generated_load_"
            f"{field['type']}(data + {prefix}_"
            f"{field['name'].upper()}_OFFSET);",
        ]
    body += [
        f"    output->payload = data + {prefix}_SIZE;",
        "    output->payload_size = payload_size;",
        "    return 1;",
        "}",
        "",
    ]
    args = ", ".join(
        f"uint{WIDTHS[field['type']] * 8}_t {field['name']}"
        for field in fields if field["name"] not in ("payload_len", "flags")
    )
    body += [
        f"static inline int {function}_encode(",
        f"    uint8_t *out, size_t capacity, {args},",
        "    const uint8_t *payload, size_t payload_size, size_t *written) {",
        "    if (written != NULL) *written = 0;",
        "    if (out == NULL || written == NULL ||",
        "        (payload == NULL && payload_size != 0) ||",
        f"        payload_size > PXA_WIRE_MAX_CONTROL_MESSAGE - {prefix}_SIZE ||",
        f"        capacity < {prefix}_SIZE + payload_size) return 0;",
    ]
    for field in fields:
        if field.get("required_nonzero"):
            body.append(f"    if ({field['name']} == 0) return 0;")
    for field in fields:
        value = ("(uint32_t)payload_size" if field["name"] == "payload_len"
                 else str(field["required_value"]) if "required_value" in field
                 else field["name"])
        body += [f"    pxa_wire_generated_store_{field['type']}(out + "
                 f"{prefix}_{field['name'].upper()}_OFFSET, {value});"]
    body += [
        "    for (size_t i = 0; i < payload_size; ++i)",
        f"        out[{prefix}_SIZE + i] = payload[i];",
        f"    *written = {prefix}_SIZE + payload_size;",
        "    return 1;",
        "}",
        "",
    ]
    return body


def render_header(*, guest: bool = False) -> str:
    legacy, current = layouts()
    record = record_layout()
    if guest:
        current = {**current, "name": ""}
    banner = (
        "/* Generated from spec/draft/abi-1.0-envelope.json and",
        " * spec/draft/pxa-core.json for the Guest SDK. Do not edit by hand. */",
    ) if guest else (
        "/* Generated from spec/draft/pxa-core.json and",
        " * spec/draft/abi-1.0-envelope.json. Do not edit by hand. */",
    )
    guard = "PXA_GUEST_WIRE_H" if guest else "PXA_WIRE_GENERATED_H"
    other = "PXA_WIRE_GENERATED_H" if guest else "PXA_GUEST_WIRE_H"
    lines = [
        *banner,
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "",
        f"#define PXA_WIRE_MAX_CONTROL_MESSAGE ((size_t){legacy['max_size']})",
        "",
        "/* The Host and Guest copies share the byte and record helpers; keep one",
        " * definition when a translation unit includes both. */",
        f"#ifndef {other}",
        "static inline uint16_t pxa_wire_generated_load_u16(",
        "    const uint8_t *bytes) {",
        "    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);",
        "}",
        "",
        "static inline uint32_t pxa_wire_generated_load_u32(",
        "    const uint8_t *bytes) {",
        "    return (uint32_t)pxa_wire_generated_load_u16(bytes) |",
        "           ((uint32_t)pxa_wire_generated_load_u16(bytes + 2) << 16);",
        "}",
        "",
        "static inline uint64_t pxa_wire_generated_load_u64(",
        "    const uint8_t *bytes) {",
        "    return (uint64_t)pxa_wire_generated_load_u32(bytes) |",
        "           ((uint64_t)pxa_wire_generated_load_u32(bytes + 4) << 32);",
        "}",
        "",
        "static inline void pxa_wire_generated_store_u16(",
        "    uint8_t *bytes, uint16_t value) {",
        "    bytes[0] = (uint8_t)value;",
        "    bytes[1] = (uint8_t)(value >> 8);",
        "}",
        "",
        "static inline void pxa_wire_generated_store_u32(",
        "    uint8_t *bytes, uint32_t value) {",
        "    pxa_wire_generated_store_u16(bytes, (uint16_t)value);",
        "    pxa_wire_generated_store_u16(bytes + 2, (uint16_t)(value >> 16));",
        "}",
        "",
        "static inline void pxa_wire_generated_store_u64(",
        "    uint8_t *bytes, uint64_t value) {",
        "    pxa_wire_generated_store_u32(bytes, (uint32_t)value);",
        "    pxa_wire_generated_store_u32(bytes + 4, (uint32_t)(value >> 32));",
        "}",
        "",
    ]
    lines += emit_record(record)
    lines += [
        f"#endif /* {other} */",
        "",
    ]
    if guest:
        lines += emit_layout(current)
    else:
        lines += emit_layout(legacy)
        lines += emit_layout(current)
    lines += [
        "#endif",
        "",
    ]
    return "\n".join(lines)


RENDERERS = (
    (HOST_OUTPUT, render_header),
    (GUEST_OUTPUT, lambda: render_header(guest=True)),
)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true",
                        help="fail if either checked-in codec is stale")
    args = parser.parse_args()
    stale = [path for path, renderer in RENDERERS
             if not path.is_file()
             or path.read_text(encoding="utf-8") != renderer()]
    if args.check:
        for path in stale:
            print(f"stale generated codec: {path}")
        return 1 if stale else 0
    for path, renderer in RENDERERS:
        path.write_text(renderer(), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
