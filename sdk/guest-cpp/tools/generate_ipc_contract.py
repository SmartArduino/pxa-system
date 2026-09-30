#!/usr/bin/env python3
"""Generate bounded C++ request/response codecs from a PXA IPC contract."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


IDENTIFIER = re.compile(r"[A-Za-z][A-Za-z0-9_]*\Z")
ENDPOINT = re.compile(r"[A-Za-z][A-Za-z0-9._-]{0,63}\Z")
TYPES = {"u32", "i32", "bool", "text"}
RESERVED = {
    "alignas", "alignof", "and", "asm", "auto", "bool", "break", "case",
    "catch", "char", "class", "concept", "const", "consteval", "constexpr",
    "continue", "co_await", "co_return", "co_yield", "decltype", "default",
    "delete", "do", "double", "else", "enum", "explicit", "export", "extern",
    "false", "float", "for", "friend", "goto", "if", "inline", "int", "long",
    "mutable", "namespace", "new", "noexcept", "nullptr", "operator", "or",
    "private", "protected", "public", "register", "requires", "return", "short",
    "signed", "sizeof", "static", "struct", "switch", "template", "this",
    "thread_local", "throw", "true", "try", "typedef", "typename", "union",
    "unsigned", "using", "virtual", "void", "volatile", "while", "std",
}


def identifier(value: object) -> bool:
    return isinstance(value, str) and IDENTIFIER.fullmatch(value) is not None and value not in RESERVED


def fields(document: dict, section: str) -> list[dict]:
    values = document.get(section)
    if not isinstance(values, list) or len(values) > 32:
        raise ValueError(f"{section} must contain at most 32 fields")
    previous = 0
    budget = 0
    for field in values:
        if not isinstance(field, dict) or not {"id", "name", "type"} <= field.keys():
            raise ValueError(f"{section} has an incomplete field")
        tag, name, kind = field["id"], field["name"], field["type"]
        if not isinstance(tag, int) or isinstance(tag, bool) or not previous < tag < 32768:
            raise ValueError(f"{section} tags must increase within 1..32767")
        if not identifier(name):
            raise ValueError(f"{section} has an invalid field name")
        if not isinstance(kind, str) or kind not in TYPES:
            raise ValueError(f"{section} has an unsupported type")
        if kind == "text":
            if set(field) != {"id", "name", "type", "max_bytes"}:
                raise ValueError(f"{section} text field has unknown properties")
            limit = field.get("max_bytes")
            if not isinstance(limit, int) or isinstance(limit, bool) or not 1 <= limit <= 512:
                raise ValueError(f"{section} text requires max_bytes in 1..512")
            width = limit
        else:
            if set(field) != {"id", "name", "type"}:
                raise ValueError(f"{section} scalar field has unknown properties")
            width = 1 if kind == "bool" else 4
        budget += 4 + width
        previous = tag
    if len({field["name"] for field in values}) != len(values):
        raise ValueError(f"{section} field names must be unique")
    if budget > 1024:
        raise ValueError(f"{section} exceeds IPC's 1024-byte payload limit")
    return values


def validate(document: dict) -> tuple[list[dict], list[dict]]:
    if not isinstance(document, dict):
        raise ValueError("IPC contract must be a JSON object")
    if set(document) != {"schema", "namespace", "name", "endpoint",
                         "version", "request", "response"}:
        raise ValueError("IPC contract has missing or unknown properties")
    if document.get("schema") != "pxa-ipc-contract-1":
        raise ValueError("unsupported IPC contract schema")
    namespace, name, endpoint = (document.get(key) for key in
                                 ("namespace", "name", "endpoint"))
    if not identifier(namespace):
        raise ValueError("invalid C++ namespace")
    if not identifier(name):
        raise ValueError("invalid C++ contract name")
    if not isinstance(endpoint, str) or not ENDPOINT.fullmatch(endpoint):
        raise ValueError("invalid IPC endpoint")
    version = document.get("version")
    if not isinstance(version, dict) or set(version) != {"major", "minor"} or any(
        not isinstance(version[key], int) or isinstance(version[key], bool) or
        not 0 <= version[key] <= 65535 for key in version):
        raise ValueError("version must contain u16 major and minor")
    return fields(document, "request"), fields(document, "response")


def field_type(field: dict) -> str:
    kind = field["type"]
    if kind == "text":
        return f"pxa::wire::OwnedText<{field['max_bytes']}>"
    return {"u32": "std::uint32_t", "i32": "std::int32_t",
            "bool": "bool"}[kind]


def max_bytes(spec: list[dict]) -> int:
    return sum(4 + (field["max_bytes"] if field["type"] == "text" else
                    1 if field["type"] == "bool" else 4) for field in spec)


def codec(lines: list[str], title: str, spec: list[dict]) -> None:
    lines.extend([
        f"    static pxa::Result<std::size_t> encode_{title}(",
        f"        const {title.capitalize()}& value, std::span<std::byte> output) noexcept {{",
        "        pxa::wire::Writer writer(output);",
    ])
    for field in spec:
        tag, name, kind = field["id"], field["name"], field["type"]
        lines.append("        {")
        if kind == "text":
            lines.extend([
                f"            const auto text = value.{name}.view();",
                "            const auto bytes = std::as_bytes(",
                "                std::span{text.data(), text.size()});",
                "            if (bytes.empty() || !pxa::wire::valid_utf8(bytes))",
                "                return std::unexpected(pxa::Error::invalid_argument);",
            ])
        elif kind == "bool":
            lines.append(f"            const std::array<std::byte, 1> bytes{{std::byte(value.{name} ? 1 : 0)}};")
        else:
            lines.extend([
                "            std::array<std::byte, 4> bytes{};",
                f"            pxa::wire::put32(bytes.data(), static_cast<std::uint32_t>(value.{name}));",
            ])
        lines.extend([
            f"            if (!pxa::wire::record(writer, {tag}, bytes))",
            "                return std::unexpected(pxa::Error::resource_limit);",
            "        }",
        ])
    lines.extend(["        return writer.size();", "    }", ""])
    lines.extend([
        f"    static pxa::Result<{title.capitalize()}> decode_{title}(",
        "        std::span<const std::byte> input) noexcept {",
        f"        {title.capitalize()} output{{}};",
        "        pxa::wire::Records records(input);",
    ])
    for field in spec:
        tag, name, kind = field["id"], field["name"], field["type"]
        length = 1 if kind == "bool" else 4
        lines.extend([
            "        {",
            (f"            auto bytes = records.take({tag});" if kind == "text" else
             f"            auto bytes = records.take({tag}, {length});"),
            "            if (!bytes) return std::unexpected(bytes.error());",
        ])
        if kind == "text":
            lines.extend([
                f"            auto assigned = output.{name}.assign(*bytes);",
                "            if (!assigned) return std::unexpected(assigned.error());",
            ])
        elif kind == "bool":
            lines.extend([
                "            if ((*bytes)[0] != std::byte{0} && (*bytes)[0] != std::byte{1})",
                "                return std::unexpected(pxa::Error::protocol_error);",
                f"            output.{name} = (*bytes)[0] == std::byte{{1}};",
            ])
        else:
            cast = "static_cast<std::int32_t>" if kind == "i32" else "static_cast<std::uint32_t>"
            lines.append(f"            output.{name} = {cast}(pxa::wire::get32(bytes->data()));")
        lines.append("        }")
    lines.extend([
        "        if (!records.empty())",
        "            return std::unexpected(pxa::Error::protocol_error);",
        "        return output;",
        "    }",
        "",
    ])


def render(document: dict) -> str:
    request, response = validate(document)
    namespace, name, endpoint = (document[key] for key in
                                 ("namespace", "name", "endpoint"))
    lines = [
        "// Generated from a PXA IPC contract. Do not edit by hand.",
        "#pragma once", "", "#include <pxa/ipc.hpp>", "", f"namespace {namespace} {{", "",
        f"struct {name} {{",
        f'    static constexpr std::string_view endpoint = "{endpoint}";',
        f"    static constexpr std::uint16_t major = {document['version']['major']};",
        f"    static constexpr std::uint16_t minor = {document['version']['minor']};",
        f"    static constexpr std::size_t request_bytes = {max_bytes(request)};",
        f"    static constexpr std::size_t response_bytes = {max_bytes(response)};",
        "    static constexpr std::size_t call_packet_bytes =",
        "        pxa::wire::header_bytes + 8 + endpoint.size() + request_bytes;",
        "    static constexpr std::size_t reply_packet_bytes =",
        "        pxa::wire::header_bytes + 20 + response_bytes;",
    ]
    for title, spec in (("Request", request), ("Response", response)):
        lines.extend([f"    struct {title} {{"])
        lines.extend(f"        {field_type(field)} {field['name']}{{}};" for field in spec)
        lines.extend(["    };", ""])
    codec(lines, "request", request)
    codec(lines, "response", response)
    lines.extend(["};", "", f"}} // namespace {namespace}", ""])
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("contract", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--check", action="store_true")
    arguments = parser.parse_args()
    try:
        content = render(json.loads(arguments.contract.read_text(encoding="utf-8")))
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.error(str(error))
    if arguments.check:
        if not arguments.output.exists() or arguments.output.read_text(encoding="utf-8") != content:
            parser.error(f"generated IPC contract is stale: {arguments.output}")
    else:
        arguments.output.parent.mkdir(parents=True, exist_ok=True)
        arguments.output.write_text(content, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
