#!/usr/bin/env python3
"""Compile PXA App YAML catalogs into a read-only C header."""

import argparse
import json
import pathlib
import re
import sys


KEY_RE = re.compile(r"^[a-z][a-z0-9]*(?:[._-][a-z0-9]+)*$")
LOCALE_RE = re.compile(r"^[a-z]{2,3}(?:-[A-Z][a-z]{3})?(?:-[A-Z]{2}|-[0-9]{3})?(?:-[A-Za-z0-9]{5,8})*$")
PLACEHOLDER_RE = re.compile(r"\{([a-z][a-z0-9_]*)\}")


def load(path):
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        raise ValueError(f"{path}: {error}") from error
    root = {}
    stack = [(-2, root)]
    for line_number, raw_line in enumerate(lines, 1):
        if not raw_line.strip() or raw_line.lstrip().startswith("#"):
            continue
        indent = len(raw_line) - len(raw_line.lstrip(" "))
        if "\t" in raw_line[:indent] or indent % 2 != 0:
            raise ValueError(f"{path}:{line_number}: indentation must use two spaces")
        content = raw_line[indent:]
        if ":" not in content:
            raise ValueError(f"{path}:{line_number}: expected a mapping entry")
        key, value = content.split(":", 1)
        key = key.strip()
        value = value.strip()
        if not key or key[0] in "-?![]{}&*!|>@`\"'":
            raise ValueError(f"{path}:{line_number}: unsupported mapping key")
        while stack and indent <= stack[-1][0]:
            stack.pop()
        if not stack or indent != stack[-1][0] + 2:
            raise ValueError(f"{path}:{line_number}: invalid indentation level")
        parent = stack[-1][1]
        if key in parent:
            raise ValueError(f"{path}:{line_number}: duplicate key {key}")
        if not value:
            parent[key] = {}
            stack.append((indent, parent[key]))
            continue
        if value.startswith('"'):
            try:
                parsed = json.loads(value)
            except json.JSONDecodeError as error:
                raise ValueError(f"{path}:{line_number}: {error.msg}") from error
        elif value.startswith("'"):
            if len(value) < 2 or not value.endswith("'"):
                raise ValueError(f"{path}:{line_number}: unterminated string")
            parsed = value[1:-1].replace("''", "'")
        elif value.isdecimal():
            parsed = int(value)
        elif value in ("null", "~"):
            parsed = None
        elif value in ("true", "false"):
            parsed = value == "true"
        elif value[0] in "[{&*!|>@`":
            raise ValueError(f"{path}:{line_number}: unsupported YAML construct")
        else:
            parsed = value
        parent[key] = parsed
    return root


def c_string(value):
    return json.dumps(value, ensure_ascii=False)


def symbol(key):
    return "PXA_MSG_" + re.sub(r"[^A-Za-z0-9]", "_", key).upper()


def validate(source_path, locale_paths):
    source = load(source_path)
    if ("namespace" not in source or "default_locale" not in source or
            set(source) - {"namespace", "default_locale", "messages"}):
        raise ValueError(
            f"{source_path}: expected namespace, default_locale and optional messages")
    namespace = source["namespace"]
    default_locale = source["default_locale"]
    messages = source.get("messages", {})
    if not isinstance(namespace, str) or not KEY_RE.fullmatch(namespace):
        raise ValueError(f"{source_path}: invalid namespace")
    if not isinstance(default_locale, str) or not LOCALE_RE.fullmatch(default_locale):
        raise ValueError(f"{source_path}: invalid default_locale")
    if not isinstance(messages, dict):
        raise ValueError(f"{source_path}: messages must be an object")
    for key, definition in messages.items():
        if not KEY_RE.fullmatch(key) or not isinstance(definition, dict):
            raise ValueError(f"{source_path}: invalid message {key!r}")
        if set(definition) - {"source", "context", "max_bytes", "placeholders"}:
            raise ValueError(f"{source_path}: unknown field in {key}")
        if not isinstance(definition.get("source"), str) or not definition["source"]:
            raise ValueError(f"{source_path}: {key}.source must be non-empty")
        if not isinstance(definition.get("context"), str) or not definition["context"]:
            raise ValueError(f"{source_path}: {key}.context must be non-empty")
        maximum = definition.get("max_bytes", 1024)
        if not isinstance(maximum, int) or maximum < 1 or maximum > 65535:
            raise ValueError(f"{source_path}: invalid max_bytes for {key}")
        if len(definition["source"].encode()) > maximum:
            raise ValueError(f"{source_path}: source exceeds max_bytes for {key}")
        placeholders = definition.get("placeholders", {})
        if not isinstance(placeholders, dict) or any(
                value not in ("string", "u32", "i32")
                for value in placeholders.values()):
            raise ValueError(f"{source_path}: invalid placeholders for {key}")
        if set(PLACEHOLDER_RE.findall(definition["source"])) != set(placeholders):
            raise ValueError(f"{source_path}: placeholder declaration mismatch for {key}")
    catalogs = {default_locale: {key: value["source"] for key, value in messages.items()}}
    metadata_catalogs = {}
    seen_locales = {default_locale}
    for path in locale_paths:
        document = load(path)
        if "locale" not in document or set(document) - {
                "locale", "metadata", "translations"}:
            raise ValueError(
                f"{path}: expected locale and optional metadata/translations")
        locale = document["locale"]
        translations = document.get("translations", {})
        metadata = document.get("metadata", {})
        if not isinstance(locale, str) or not LOCALE_RE.fullmatch(locale):
            raise ValueError(f"{path}: invalid locale")
        if locale in seen_locales:
            raise ValueError(f"{path}: duplicate locale {locale}")
        seen_locales.add(locale)
        if not isinstance(translations, dict):
            raise ValueError(f"{path}: translations must be an object")
        if not isinstance(metadata, dict) or set(metadata) - {
                "name", "description", "icon"}:
            raise ValueError(f"{path}: invalid metadata")
        for field, maximum in (("name", 128), ("description", 512),
                               ("icon", 1024)):
            if field not in metadata:
                continue
            value = metadata[field]
            if (not isinstance(value, str) or not value or "\0" in value or
                    len(value.encode()) > maximum):
                raise ValueError(f"{path}: invalid metadata.{field}")
        if not metadata and not translations:
            raise ValueError(f"{path}: locale catalog is empty")
        unknown = set(translations) - set(messages)
        if unknown:
            raise ValueError(f"{path}: unknown keys: {', '.join(sorted(unknown))}")
        for key, value in translations.items():
            if not isinstance(value, str) or not value:
                raise ValueError(f"{path}: translation for {key} must be non-empty")
            if len(value.encode()) > messages[key].get("max_bytes", 1024):
                raise ValueError(f"{path}: translation exceeds max_bytes for {key}")
            expected = set(messages[key].get("placeholders", {}))
            if set(PLACEHOLDER_RE.findall(value)) != expected:
                raise ValueError(f"{path}: placeholder mismatch for {key}")
        if translations:
            catalogs[locale] = translations
        if metadata:
            metadata_catalogs[locale] = metadata
    return namespace, default_locale, messages, catalogs, metadata_catalogs


def emit(output, namespace, default_locale, messages, catalogs,
         metadata_catalogs):
    del metadata_catalogs
    keys = sorted(messages)
    guard = "PXA_APP_MESSAGES_" + re.sub(r"[^A-Za-z0-9]", "_", namespace).upper() + "_H"
    lines = ["/* Generated by compile_catalog.py. Do not edit. */",
             f"#ifndef {guard}", f"#define {guard}", "",
             '#include "pxa_i18n.h"', "", "enum {"]
    if keys:
        for index, key in enumerate(keys, 1):
            lines.append(f"    {symbol(key)} = {index}u,")
    else:
        lines.append("    PXA_MSG_INVALID = 0u,")
    lines.extend(["};", ""])
    catalog_names = []
    for catalog_index, (locale, translations) in enumerate(catalogs.items()):
        name = f"pxa_app_i18n_entries_{catalog_index}"
        catalog_names.append((locale, name if translations else None))
        if not translations:
            continue
        lines.append(f"static const pxa_i18n_entry_t {name}[] = {{")
        for index, key in enumerate(keys, 1):
            if key in translations:
                value = translations[key]
                lines.append(f"    {{{index}u, {c_string(value)}, {len(value.encode())}u}},")
        lines.extend(["};", ""])
    lines.append("static const pxa_i18n_catalog_t pxa_app_i18n_catalogs[] = {")
    for locale, name in catalog_names:
        if name is None:
            lines.append(
                f"    {{{c_string(locale)}, {len(locale.encode())}u, NULL, 0u}},")
        else:
            lines.append(f"    {{{c_string(locale)}, {len(locale.encode())}u, {name}, "
                         f"(uint16_t)(sizeof({name}) / sizeof({name}[0]))}},")
    lines.extend(["};", "", "static const pxa_i18n_bundle_t pxa_app_i18n_bundle = {",
                  "    pxa_app_i18n_catalogs,",
                  "    (uint8_t)(sizeof(pxa_app_i18n_catalogs) / sizeof(pxa_app_i18n_catalogs[0])),",
                  f"    {list(catalogs).index(default_locale)}u,", "};", "",
                  f"#endif /* {guard} */", ""])
    output.write_text("\n".join(lines), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=pathlib.Path)
    parser.add_argument("locales", nargs="*", type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    args = parser.parse_args()
    try:
        values = validate(args.source, args.locales)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        emit(args.output, *values)
    except ValueError as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
