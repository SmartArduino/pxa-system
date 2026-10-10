#!/usr/bin/env python3
"""Compile PXA YAML catalogs into independent, read-only C or C++ headers."""

import argparse
import json
import pathlib
import os
import tempfile
import re
import sys

from message_compiler import parse as parse_message, TYPES
from plural_rules import for_locale


KEY_RE = re.compile(r"^[a-z][a-z0-9]*(?:[._-][a-z0-9]+)*$")
LOCALE_RE = re.compile(r"^[a-z]{2,8}(?:-[A-Z][a-z]{3})?(?:-[A-Z]{2}|-[0-9]{3})?(?:-(?:[A-Za-z0-9]{5,8}|[0-9][A-Za-z0-9]{3}))*$")
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


def write_output(output, text):
    data = text.encode('utf-8')
    if output.exists() and output.read_bytes() == data:
        return
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=output.parent, delete=False) as file:
            temporary = pathlib.Path(file.name)
            file.write(data)
        os.replace(temporary, output)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def c_string(value):
    return json.dumps(value, ensure_ascii=False)


def symbol(key):
    return "PXA_MSG_" + re.sub(r"[^A-Za-z0-9]", "_", key).upper()


def validate_number_symbols(value):
    if value is None:
        return
    if not isinstance(value, dict) or set(value) - {'decimal', 'group', 'digits', 'primary_group', 'secondary_group'}:
        raise ValueError('invalid number_symbols fields')
    for field in ('decimal', 'group', 'digits'):
        text = value.get(field, {'decimal': '.', 'group': ',', 'digits': '0123456789'}[field])
        if not isinstance(text, str) or '\0' in text or len(text.encode('utf-8')) > 64:
            raise ValueError(f'invalid number_symbols.{field}')
        if (field == 'decimal' and not text) or (field == 'digits' and (len(text) != 10 or len(set(text)) != 10)):
            raise ValueError(f'invalid number_symbols.{field}')
    for field in ('primary_group', 'secondary_group'):
        size = value.get(field, 0)
        if type(size) is not int or not 0 <= size <= 9:
            raise ValueError(f'invalid number_symbols.{field}')
    if value.get('primary_group', 0) and not value.get('group', ','):
        raise ValueError('group separator required for grouped numbers')


def validate(source_path, locale_paths):
    source = load(source_path)
    if ("namespace" not in source or "default_locale" not in source or
            set(source) - {"namespace", "default_locale", "messages", "number_symbols"}):
        raise ValueError(
            f"{source_path}: expected namespace, default_locale and optional messages")
    namespace = source["namespace"]
    default_locale = source["default_locale"]
    messages = source.get("messages", {})
    validate_number_symbols(source.get("number_symbols"))
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
        definition["context"].encode("utf-8")
        maximum = definition.get("max_bytes", 1024)
        if type(maximum) is not int or maximum < 1 or maximum > 65535:
            raise ValueError(f"{source_path}: invalid max_bytes for {key}")
        if "\0" in definition["source"] or "\0" in definition["context"]:
            raise ValueError(f"{source_path}: NUL is forbidden in {key}")
        if len(definition["source"].encode()) > maximum:
            raise ValueError(f"{source_path}: source exceeds max_bytes for {key}")
        placeholders = definition.get("placeholders", {})
        if not isinstance(placeholders, dict) or any(
                not isinstance(value, str) or value not in TYPES
                for value in placeholders.values()):
            raise ValueError(f"{source_path}: invalid placeholders for {key}")
        try:
            parse_message(definition["source"], placeholders)
        except (ValueError, UnicodeError) as error:
            raise ValueError(f"{source_path}: {key}: {error}") from error
    symbols = [symbol(key) for key in messages]
    if len(messages) > 65535 or len(set(symbols)) != len(symbols):
        raise ValueError(f"{source_path}: too many messages or colliding generated identifiers")
    catalogs = {default_locale: {key: value["source"] for key, value in messages.items()}}
    metadata_catalogs = {}
    seen_locales = {default_locale}
    for path in locale_paths:
        document = load(path)
        if "locale" not in document or set(document) - {
                "locale", "metadata", "translations", "number_symbols"}:
            raise ValueError(
                f"{path}: expected locale and optional metadata/translations")
        locale = document["locale"]
        translations = document.get("translations", {})
        metadata = document.get("metadata", {})
        validate_number_symbols(document.get("number_symbols"))
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
            try:
                parse_message(value, messages[key].get("placeholders", {}))
            except (ValueError, UnicodeError) as error:
                raise ValueError(f"{path}: {key}: {error}") from error
        if translations:
            catalogs[locale] = translations
        if metadata:
            metadata_catalogs[locale] = metadata
    return namespace, default_locale, messages, catalogs, metadata_catalogs


def emit(output, namespace, default_locale, messages, catalogs,
         metadata_catalogs):
    del metadata_catalogs
    if len(catalogs) > 255:
        raise ValueError("C catalog count exceeds 255")
    for key, definition in messages.items():
        if any(kind not in ("string", "u32", "i32") for kind in definition.get("placeholders", {}).values()):
            raise ValueError(f"{key}: extended placeholders require --language cpp")
    for translations in catalogs.values():
        for key, value in translations.items():
            parameters = messages[key].get("placeholders", {})
            if set(PLACEHOLDER_RE.findall(value)) != set(parameters) or parse_message(value, parameters)[1]:
                raise ValueError(f"{key}: rich message formats require --language cpp")
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
    write_output(output, "\n".join(lines))


CPP_KEYWORDS = set("alignas alignof and and_eq asm bitand bitor char8_t char16_t char32_t compl asm const_cast dynamic_cast not_eq or_eq reinterpret_cast static_cast xor_eq auto bool break case catch char class concept const consteval constexpr constinit continue co_await co_return co_yield decltype default delete do double else enum explicit export extern false float for friend goto if inline int long mutable namespace new noexcept not nullptr operator or private protected public register requires return short signed sizeof static static_assert struct switch template this thread_local throw true try typedef typeid typename union unsigned using virtual void volatile wchar_t while xor".split())


def cpp_identifier(value):
    result = re.sub(r"[^A-Za-z0-9]", "_", value)
    if not re.fullmatch(r"[a-z][A-Za-z0-9_]*", result) or result in CPP_KEYWORDS or "__" in result:
        raise ValueError(f"invalid or reserved C++ identifier: {value}")
    return result


def cpp_bytes(value):
    # Fixed-width octal escapes cannot consume following ASCII digits.
    return '"' + ''.join(chr(c) if 32 <= c < 127 and c not in (34, 92) else f"\\{c:03o}" for c in value) + '"'


def emit_cpp(output, namespace, default_locale, messages, catalogs, metadata_catalogs, cpp_namespace=None, number_symbols=None):
    del metadata_catalogs
    cpp_namespace = cpp_namespace or "pxa::messages::" + cpp_identifier(namespace)
    for part in cpp_namespace.split("::"):
        if not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*", part) or part in CPP_KEYWORDS or "__" in part:
            raise ValueError(f"invalid or reserved C++ namespace: {cpp_namespace}")
    keys = sorted(messages)
    identifiers = {key: cpp_identifier(key) for key in keys}
    if any(value in ("bundle", "detail", "args") for value in identifiers.values()):
        raise ValueError("message identifiers bundle, detail and args are reserved")
    lines = ["// Generated by compile_catalog.py. Do not edit.", "#pragma once", '#include <pxa/i18n.hpp>', "", f"namespace {cpp_namespace} {{", "namespace detail {"]
    rule_names = {}
    def rules(locale, kind, enabled):
        values = tuple(for_locale(locale, kind)) if enabled else ()
        if not values:
            return "{}"
        if values in rule_names:
            return rule_names[values]
        name = f"rules_{len(rule_names)}"
        rule_names[values] = name
        lines.append(f"inline constexpr pxa::i18n::PluralRule {name}[] = {{")
        for category, data in values:
            lines.append(f"    {{pxa::i18n::PluralCategory::{category}, {{{cpp_bytes(data)}, {len(data)}}}}},")
        lines.extend(["};", ""])
        return name
    number_symbols = number_symbols or {}
    catalogs_info = []
    for ci, (locale, translations) in enumerate(catalogs.items()):
        entries = []
        features = set()
        for index, key in enumerate(keys, 1):
            if key not in translations:
                continue
            data, used = parse_message(translations[key], messages[key].get("placeholders", {}))
            features.update(used)
            formatted = isinstance(data, bytes)
            data = data if formatted else data.encode('utf-8')
            entries.append(f"    {{{index}, {{{cpp_bytes(data)}, {len(data)}}}, {'true' if formatted else 'false'}}},")
        cardinal = rules(locale, 'cardinal', 'plural' in features)
        ordinal = rules(locale, 'ordinal', 'selectordinal' in features)
        if entries:
            lines.append(f"inline constexpr pxa::i18n::Entry entries_{ci}[] = {{")
            lines.extend(entries); lines.extend(["};", ""])
        numbers = number_symbols.get(locale)
        if numbers:
            lines.append(f"inline constexpr pxa::i18n::NumberSymbols numbers_{ci}{{{cpp_bytes(numbers.get('decimal', '.').encode('utf-8'))}, {cpp_bytes(numbers.get('group', ',').encode('utf-8'))}, {cpp_bytes(numbers.get('digits', '0123456789').encode('utf-8'))}, {numbers.get('primary_group', 0)}, {numbers.get('secondary_group', 0)}}};")
        catalogs_info.append((locale, f"entries_{ci}" if entries else "{}", cardinal, ordinal, f"&numbers_{ci}" if numbers else "nullptr"))
    lines.append("inline constexpr pxa::i18n::Catalog catalogs[] = {")
    for locale, entries, cardinal, ordinal, numbers in catalogs_info:
        lines.append(f"    {{{c_string(locale)}, {entries}, {cardinal}, {ordinal}, {numbers}}},")
    lines.extend(["};", "} // namespace detail", ""])
    for index, key in enumerate(keys, 1):
        params = messages[key].get('placeholders', {})
        if params:
            lines.append("// Arguments: " + ", ".join(f"{p}: {params[p]}" for p in sorted(params)))
        kinds = ', '.join('pxa::i18n::ArgumentKind::' + params[p] for p in sorted(params))
        lines.append(f"inline constexpr pxa::i18n::Message<{kinds}> {identifiers[key]}{{{index}}};")
    lines.append("namespace args {")
    type_names = {'string': 'std::string_view', 'u32': 'std::uint32_t', 'i32': 'std::int32_t',
                  'u64': 'std::uint64_t', 'i64': 'std::int64_t', 'number': 'pxa::i18n::Decimal'}
    for key in keys:
        params = messages[key].get('placeholders', {})
        if not params:
            continue
        for parameter in params:
            cpp_identifier(parameter)
        lines.append(f"struct {identifiers[key]} {{")
        for parameter in sorted(params):
            lines.append(f"    {type_names[params[parameter]]} {parameter}{{}};")
        lines.append(f"    static constexpr auto _pxa_message = ::{cpp_namespace}::{identifiers[key]};")
        lines.append("    auto _pxa_arguments() const noexcept { return std::tie(" + ', '.join(sorted(params)) + "); }")
        lines.append("};")
    lines.extend(["} // namespace args", ""])
    lines.extend([f"inline constexpr pxa::i18n::Bundle bundle{{detail::catalogs, {list(catalogs).index(default_locale)}}};", f"}} // namespace {cpp_namespace}", ""])
    write_output(output, "\n".join(lines))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=pathlib.Path)
    parser.add_argument("locales", nargs="*", type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--language", choices=("c", "cpp"), default="c")
    parser.add_argument("--cpp-namespace", help="C++ namespace override, e.g. game::messages")
    args = parser.parse_args()
    try:
        values = validate(args.source, args.locales)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        if args.language == "cpp":
            symbols = {values[1]: load(args.source).get('number_symbols')}
            for path in args.locales:
                document = load(path)
                symbols[document['locale']] = document.get('number_symbols')
            emit_cpp(args.output, *values, cpp_namespace=args.cpp_namespace, number_symbols=symbols)
        else:
            if args.cpp_namespace:
                raise ValueError("--cpp-namespace requires --language cpp")
            if any(load(path).get('number_symbols') for path in [args.source, *args.locales]):
                raise ValueError("number_symbols requires --language cpp")
            emit(args.output, *values)
    except (ValueError, UnicodeError, OSError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
