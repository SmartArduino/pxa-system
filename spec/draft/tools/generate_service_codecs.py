#!/usr/bin/env python3
"""Generate fixed-shape Service payload codecs from the draft schema."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


SPEC = Path(__file__).resolve().parent.parent
SYSTEM = SPEC.parent.parent
OUTPUTS = (
    SYSTEM / "libpxa/include/pxa/device_runtime_info_generated.h",
    SYSTEM / "sdk/guest-c/include/pxa_device_runtime_info_generated.h",
)
WINDOW_OUTPUTS = (
    SYSTEM / "libpxa/include/pxa/window_snapshot_generated.h",
    SYSTEM / "sdk/guest-c/include/pxa_window_snapshot_generated.h",
)
HOST_WIRE_GUARD = "PXA_WIRE_GENERATED_H"
GUEST_WIRE_GUARD = "PXA_GUEST_WIRE_H"


def wire_guard(*, guest: bool) -> str:
    return GUEST_WIRE_GUARD if guest else HOST_WIRE_GUARD


def header_guard(base: str, *, guest: bool) -> str:
    return f"PXA_GUEST_{base}" if guest else f"PXA_{base}"

WINDOW_LAYOUT = (
    ("revision", "u64", ("revision",)),
    ("logical-size", "u32-u32", ("logical_width", "logical_height")),
    ("pixel-size", "u32-u32", ("pixel_width", "pixel_height")),
    ("density", "u32-u32", ("density_numerator", "density_denominator")),
    ("safe-insets", "u32-u32-u32-u32",
     ("safe_insets.left", "safe_insets.top", "safe_insets.right",
      "safe_insets.bottom")),
    ("system-bar-insets", "u32-u32-u32-u32",
     ("system_bar_insets.left", "system_bar_insets.top",
      "system_bar_insets.right", "system_bar_insets.bottom")),
    ("orientation", "orientation-u8", ("orientation",)),
    ("focused", "bool-u8", ("focused",)),
)


def window_fields() -> list[tuple[dict, tuple[str, ...], int, int]]:
    document = json.loads((SPEC / "pxa-window.json").read_text(encoding="utf-8"))
    if [(item["name"], item["value"]) for item in
            document["orientations"]] != [
                ("unspecified", 0), ("portrait", 1), ("landscape", 2)]:
        raise ValueError("Window orientation domain changed")
    fields = document["snapshot_tags"]
    result = []
    offset = 0
    if len(fields) != len(WINDOW_LAYOUT):
        raise ValueError("Window snapshot field count changed")
    for index, (field, (name, payload, members)) in enumerate(
            zip(fields, WINDOW_LAYOUT), 1):
        if (field["id"] != index or field["name"] != name or
                field["payload"] != payload or field["cardinality"] != "one"):
            raise ValueError("Window snapshot layout changed")
        width = 8 if payload == "u64" else 1 if payload.endswith("u8") else 4
        size = width * len(members)
        result.append((field, members, offset, size))
        offset += 4 + size
    return result


def render_window_header(*, guest: bool = False) -> str:
    fields = window_fields()
    record_size = sum(4 + size for _, _, _, size in fields)
    lines = [
        "/* Generated from spec/draft/pxa-window.json. Do not edit by hand. */",
        f"#ifndef {header_guard('WINDOW_SNAPSHOT_GENERATED_H', guest=guest)}",
        f"#define {header_guard('WINDOW_SNAPSHOT_GENERATED_H', guest=guest)}",
        "",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "",
        f"#ifndef {wire_guard(guest=guest)}",
        '#error "include the generated PXA wire codec first"',
        "#endif",
        "",
        f"#define PXA_WINDOW_SNAPSHOT_RECORD_BYTES ((size_t){record_size})",
        "",
        "typedef struct {",
        "    uint32_t left, top, right, bottom;",
        "} pxa_window_wire_insets_t;",
        "",
        "typedef struct {",
        "    int32_t status;",
        "    uint64_t revision;",
        "    uint32_t logical_width, logical_height;",
        "    uint32_t pixel_width, pixel_height;",
        "    uint32_t density_numerator, density_denominator;",
        "    pxa_window_wire_insets_t safe_insets;",
        "    pxa_window_wire_insets_t system_bar_insets;",
        "    uint8_t orientation, focused;",
        "} pxa_window_snapshot_wire_t;",
        "",
        "static inline int pxa_window_snapshot_wire_valid(",
        "    const pxa_window_snapshot_wire_t *value) {",
        "    return value != NULL && value->revision != 0 &&",
        "           value->logical_width != 0 && value->logical_height != 0 &&",
        "           value->pixel_width != 0 && value->pixel_height != 0 &&",
        "           value->density_numerator != 0 &&",
        "           value->density_denominator != 0 &&",
        "           value->orientation <= 2 && value->focused <= 1;",
        "}",
        "",
        "static inline int pxa_window_snapshot_records_encode(",
        "    uint8_t *out, size_t capacity,",
        "    const pxa_window_snapshot_wire_t *value, size_t *written) {",
        "    if (written != NULL) *written = 0;",
        "    if (out == NULL || written == NULL ||",
        "        capacity < PXA_WINDOW_SNAPSHOT_RECORD_BYTES ||",
        "        !pxa_window_snapshot_wire_valid(value)) return 0;",
    ]
    for field, members, offset, size in fields:
        lines += [
            f"    pxa_wire_generated_store_u16(out + {offset}, {field['id']});",
            f"    pxa_wire_generated_store_u16(out + {offset + 2}, {size});",
        ]
        width = size // len(members)
        for part, member in enumerate(members):
            position = offset + 4 + part * width
            if width == 1:
                lines.append(f"    out[{position}] = value->{member};")
            else:
                lines.append(
                    f"    pxa_wire_generated_store_u{width * 8}"
                    f"(out + {position}, value->{member});")
    lines += [
        "    *written = PXA_WINDOW_SNAPSHOT_RECORD_BYTES;",
        "    return 1;",
        "}",
        "",
        "static inline int pxa_window_snapshot_records_decode(",
        "    const uint8_t *data, size_t size,",
        "    pxa_window_snapshot_wire_t *output) {",
        "    if (output == NULL) return 0;",
        "    for (size_t i = 0; i < sizeof(*output); ++i)",
        "        ((uint8_t *)output)[i] = 0;",
        "    if (data == NULL || size != PXA_WINDOW_SNAPSHOT_RECORD_BYTES)",
        "        return 0;",
    ]
    for field, _, offset, size in fields:
        lines += [
            f"    if (pxa_wire_generated_load_u16(data + {offset}) != "
            f"{field['id']} ||",
            f"        pxa_wire_generated_load_u16(data + {offset + 2}) != "
            f"{size}) return 0;",
        ]
    for _, members, offset, size in fields:
        width = size // len(members)
        for part, member in enumerate(members):
            position = offset + 4 + part * width
            if width == 1:
                lines.append(f"    output->{member} = data[{position}];")
            else:
                lines.append(
                    f"    output->{member} = pxa_wire_generated_load_u"
                    f"{width * 8}(data + {position});")
    lines += [
        "    return pxa_window_snapshot_wire_valid(output);",
        "}",
        "",
        "static inline int pxa_window_snapshot_result_decode(",
        "    const uint8_t *data, size_t size,",
        "    pxa_window_snapshot_wire_t *output) {",
        "    if (output == NULL) return 0;",
        "    for (size_t i = 0; i < sizeof(*output); ++i)",
        "        ((uint8_t *)output)[i] = 0;",
        "    if (data == NULL || size < 4) return 0;",
        "    output->status = (int32_t)pxa_wire_generated_load_u32(data);",
        "    if (output->status != 0) return size == 4;",
        "    return pxa_window_snapshot_records_decode(data + 4, size - 4,",
        "                                              output);",
        "}",
        "",
        "#endif",
        "",
    ]
    return "\n".join(lines)


def device_fields() -> list[dict]:
    document = json.loads((SPEC / "pxa-device.json").read_text(encoding="utf-8"))
    fields = document["runtime_info_record_tags"]
    expected = ("target", "architecture", "engine", "engine-abi", "formats")
    if tuple(field["name"] for field in fields) != expected:
        raise ValueError("Device runtime-info field order changed")
    for index, field in enumerate(fields, 1):
        if field["id"] != index:
            raise ValueError("Device runtime-info tags must be consecutive")
        if index < 5 and (field["payload"] != "utf8" or
                          not 0 < field["max_bytes"] < 256):
            raise ValueError("Device runtime-info text bound is invalid")
    if fields[4]["payload"] != "u32":
        raise ValueError("Device runtime-info formats must be u32")
    return fields


def render_header(*, guest: bool = False) -> str:
    fields = device_fields()
    text_fields = fields[:4]
    lines = [
        "/* Generated from spec/draft/pxa-device.json. Do not edit by hand. */",
        f"#ifndef {header_guard('DEVICE_RUNTIME_INFO_GENERATED_H', guest=guest)}",
        f"#define {header_guard('DEVICE_RUNTIME_INFO_GENERATED_H', guest=guest)}",
        "",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "",
        f"#ifndef {wire_guard(guest=guest)}",
        '#error "include the generated PXA wire codec first"',
        "#endif",
        "",
        *(f"#define PXA_DEVICE_INFO_TAG_{field['name'].replace('-', '_').upper()} "
          f"UINT16_C({field['id']})" for field in fields),
        "#define PXA_DEVICE_RUNTIME_INFO_MAX_RECORD_BYTES "
        f"((size_t){sum(4 + field['max_bytes'] for field in text_fields) + 8})",
        "",
        "typedef struct {",
        "    int32_t status;",
        *(f"    char {field['name'].replace('-', '_')}[{field['max_bytes'] + 1}];"
          for field in text_fields),
        "    uint32_t formats;",
        "} pxa_device_runtime_info_payload_t;",
        "",
        "static inline int pxa_device_info_valid_utf8(",
        "    const uint8_t *bytes, size_t size) {",
        "    size_t index = 0;",
        "    while (index < size) {",
        "        uint8_t first = bytes[index++];",
        "        unsigned continuation;",
        "        if (first == 0) return 0;",
        "        if (first < 0x80) continue;",
        "        if (first < 0xc2 || first > 0xf4) return 0;",
        "        continuation = first < 0xe0 ? 1u : first < 0xf0 ? 2u : 3u;",
        "        if (continuation > size - index) return 0;",
        "        uint8_t second = bytes[index];",
        "        if (second < 0x80 || second > 0xbf ||",
        "            (first == 0xe0 && second < 0xa0) ||",
        "            (first == 0xed && second >= 0xa0) ||",
        "            (first == 0xf0 && second < 0x90) ||",
        "            (first == 0xf4 && second >= 0x90)) return 0;",
        "        for (unsigned part = 0; part < continuation; ++part) {",
        "            uint8_t byte = bytes[index++];",
        "            if (byte < 0x80 || byte > 0xbf) return 0;",
        "        }",
        "    }",
        "    return 1;",
        "}",
        "",
        "static inline int pxa_device_runtime_info_decode(",
        "    const uint8_t *data, size_t size,",
        "    pxa_device_runtime_info_payload_t *output) {",
        "    size_t offset = 4;",
        "    if (output == NULL) return 0;",
        "    for (size_t i = 0; i < sizeof(*output); ++i)",
        "        ((uint8_t *)output)[i] = 0;",
        "    if (data == NULL || size < 4) return 0;",
        "    output->status = (int32_t)pxa_wire_generated_load_u32(data);",
        "    if (output->status != 0) return size == 4;",
    ]
    for field in fields:
        name = field["name"].replace("-", "_")
        macro = f"PXA_DEVICE_INFO_TAG_{name.upper()}"
        lines += [
            "    {",
            "        pxa_wire_record_view_t record;",
            "        size_t consumed = 0;",
            "        if (offset >= size ||",
            "            !pxa_wire_record_decode(data + offset, size - offset,",
            "                                    &record, &consumed) ||",
            f"            record.raw_tag != {macro}) return 0;",
        ]
        if field["payload"] == "utf8":
            lines += [
                f"        if (record.payload_size == 0 ||",
                f"            record.payload_size > {field['max_bytes']} ||",
                "            !pxa_device_info_valid_utf8(record.payload,",
                "                                        record.payload_size)) return 0;",
                "        for (size_t i = 0; i < record.payload_size; ++i)",
                f"            output->{name}[i] = (char)record.payload[i];",
                f"        output->{name}[record.payload_size] = '\\0';",
            ]
        else:
            lines += [
                "        if (record.payload_size != 4) return 0;",
                "        output->formats =",
                "            pxa_wire_generated_load_u32(record.payload);",
            ]
        lines += ["        offset += consumed;", "    }"]
    lines += ["    return offset == size;", "}", ""]
    args = ", ".join(f"const char *{field['name'].replace('-', '_')}"
                     for field in text_fields)
    lines += [
        "static inline int pxa_device_runtime_info_encode(",
        f"    uint8_t *out, size_t capacity, {args},",
        "    uint32_t formats, size_t *written) {",
        "    size_t offset = 0;",
        "    if (written != NULL) *written = 0;",
        "    if (out == NULL || written == NULL) return 0;",
    ]
    for field in text_fields:
        name = field["name"].replace("-", "_")
        macro = f"PXA_DEVICE_INFO_TAG_{name.upper()}"
        lines += [
            "    {",
            "        size_t size = 0;",
            "        size_t encoded = 0;",
            f"        if ({name} == NULL) return 0;",
            f"        while (size <= {field['max_bytes']} && {name}[size] != '\\0')",
            "            ++size;",
            f"        if (size == 0 || size > {field['max_bytes']} ||",
            "            !pxa_device_info_valid_utf8(",
            f"                (const uint8_t *){name}, size) ||",
            "            offset > capacity ||",
            "            !pxa_wire_record_encode(out + offset, capacity - offset,",
            f"                                    {macro},",
            f"                                    (const uint8_t *){name}, size,",
            "                                    &encoded)) return 0;",
            "        offset += encoded;",
            "    }",
        ]
    lines += [
        "    {",
        "        uint8_t value[4];",
        "        size_t encoded = 0;",
        "        pxa_wire_generated_store_u32(value, formats);",
        "        if (offset > capacity ||",
        "            !pxa_wire_record_encode(out + offset, capacity - offset,",
        "                                    PXA_DEVICE_INFO_TAG_FORMATS, value,",
        "                                    sizeof(value), &encoded)) return 0;",
        "        offset += encoded;",
        "    }",
        "    *written = offset;",
        "    return 1;",
        "}",
        "",
        "#endif",
        "",
    ]
    return "\n".join(lines)


def render_cpp_device_header() -> str:
    fields = device_fields()
    lines = [
        "// Generated from spec/draft/pxa-device.json. Do not edit by hand.",
        "#pragma once",
        '#include "service_wire.hpp"',
        "",
        "namespace pxa {",
        "struct DeviceRuntimeInfo {",
        *(f"    FixedText<{field['max_bytes']}> "
          f"{field['name'].replace('-', '_')};" for field in fields[:4]),
        "    std::uint32_t formats = 0;",
        "};",
        "",
        "inline Result<DeviceRuntimeInfo> decode_device_runtime_info(",
        "    std::span<const std::byte> payload) noexcept {",
        "    auto body = wire::result_body(payload);",
        "    if (!body) return std::unexpected(body.error());",
        "    wire::Records records(*body);",
        "    DeviceRuntimeInfo output;",
    ]
    for field in fields[:4]:
        name = field['name'].replace('-', '_')
        lines += [
            f"    auto {name} = records.take({field['id']});",
            f"    if (!{name} || !output.{name}.assign(*{name}))",
            "        return std::unexpected(Error::protocol_error);",
        ]
    lines += [
        f"    auto formats = records.take({fields[4]['id']}, 4);",
        "    if (!formats || !records.empty())",
        "        return std::unexpected(Error::protocol_error);",
        "    output.formats = wire::get32(formats->data());",
        "    return output;",
        "}",
        "} // namespace pxa",
        "",
    ]
    return "\n".join(lines)


def render_cpp_device_golden() -> str:
    document = json.loads((SPEC / "golden/device-runtime-info.json").read_text(encoding="utf-8"))
    data = bytes.fromhex(document["result_hex"])
    return "\n".join([
        "// Generated from spec/draft/golden/device-runtime-info.json.",
        "#pragma once",
        "#include <array>",
        "#include <cstddef>",
        f"inline constexpr std::array<std::byte, {len(data)}> device_info_golden{{",
        *("    " + ", ".join(f"std::byte{{0x{byte:02x}}}" for byte in data[i:i + 8]) + ","
          for i in range(0, len(data), 8)),
        "};",
        "",
    ])


RENDERERS = (
    (OUTPUTS[0], render_header),
    (OUTPUTS[1], lambda: render_header(guest=True)),
    (WINDOW_OUTPUTS[0], render_window_header),
    (WINDOW_OUTPUTS[1], lambda: render_window_header(guest=True)),
    (SYSTEM / "sdk/guest-cpp/include/pxa/device_runtime_info_generated.hpp",
     render_cpp_device_header),
    (SYSTEM / "sdk/guest-cpp/tests/device_runtime_info_golden.hpp",
     render_cpp_device_golden),
)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--language", choices=("all", "c", "cpp"), default="all")
    args = parser.parse_args()
    renderers = tuple((path, renderer) for path, renderer in RENDERERS
                      if args.language == "all" or
                      (path.suffix == ".hpp") == (args.language == "cpp"))
    stale = [path for path, renderer in renderers
             if not path.is_file()
             or path.read_text(encoding="utf-8") != renderer()]
    if args.check:
        for path in stale:
            print(f"stale Service codec: {path}")
        return 1 if stale else 0
    for path, renderer in renderers:
        path.write_text(renderer(), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
