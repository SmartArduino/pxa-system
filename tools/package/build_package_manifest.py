#!/usr/bin/env python3
"""Build and sign a canonical PXA Draft 0.1 package manifest."""

import hashlib
import json
import re
import struct
import subprocess
import sys
from pathlib import Path

TOOL_DIR = Path(__file__).resolve().parent
if str(TOOL_DIR) not in sys.path:
    sys.path.insert(0, str(TOOL_DIR))
from protocol_metadata import (  # noqa: E402
    DECLARABLE_SERVICE_IDS,
    SERVICE_IDS,
    SERVICE_VERSIONS,
    WASI_FEATURES,
)

SAFE_ID = re.compile(r"[a-z][a-z0-9._-]{0,63}")
PERMISSION_ID = re.compile(r"[a-z][a-z0-9._-]{0,95}")
PACKAGE_PATH = re.compile(r"[A-Za-z0-9._-]+(?:/[A-Za-z0-9._-]+)*")
VERSION = re.compile(r"[0-9A-Za-z][0-9A-Za-z._+-]{0,63}")
COMPONENT_KINDS = {"ui": 1, "service": 2, "job": 3}
COMPONENT_FLAG_PINNED_MEMORY = 1
P256_ORDER = int("FFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551", 16)
EC_PUBLIC_KEY_OID = bytes.fromhex("2a8648ce3d0201")
P256_OID = bytes.fromhex("2a8648ce3d030107")


class PackageError(Exception):
    pass


def require(condition, message):
    if not condition:
        raise PackageError(message)


def record(tag, payload):
    require(len(payload) <= 0xFFFF, "record payload too large")
    return struct.pack("<HH", tag, len(payload)) + payload


def records(items):
    return b"".join(record(tag, payload) for tag, payload in items)


def service(service_id, features=0):
    major, min_minor = SERVICE_VERSIONS[service_id]
    max_minor = min_minor
    return records([
        (1, struct.pack("<H", service_id)),
        (2, struct.pack("<HH", major, min_minor)),
        (3, struct.pack("<HH", major, max_minor)),
        (4, struct.pack("<Q", features)),
    ])


def artifact(kind, path, features, memory, target=None, engine=None, engine_abi=None):
    items = [(1, struct.pack("<B", kind)), (2, path.encode("ascii"))]
    if target is not None:
        items.append((3, target.encode("ascii")))
    if engine is not None:
        items.append((4, engine.encode("ascii")))
    if engine_abi is not None:
        items.append((5, engine_abi.encode("ascii")))
    items.extend([(6, struct.pack("<Q", features)), (7, struct.pack("<B", memory))])
    return records(items)


def component(component_id, kind, flags, aot_targets, engine_abi, services, include_wasm):
    artifacts = [
        (f"artifacts/{component_id}.{target}.aot",
         artifact(2, f"artifacts/{component_id}.{target}.aot", 0, 1,
                  target=target, engine="wamr", engine_abi=engine_abi))
        for target in aot_targets
    ]
    if include_wasm:
        artifacts.append((f"artifacts/{component_id}.wasm",
                          artifact(1, f"artifacts/{component_id}.wasm", 0, 1)))
    return records(
        [(1, component_id.encode("ascii")), (2, struct.pack("<B", kind)),
         (3, struct.pack("<B", flags))]
        + [(4, value) for _, value in sorted(artifacts)]
        + [(5, service(service_id, features)) for service_id, features in services]
    )


def permission(item):
    name = item.get("name")
    required = item.get("required")
    scope = item.get("scope")
    require(isinstance(name, str) and PERMISSION_ID.fullmatch(name) and "." in name,
            "invalid permission name")
    require(isinstance(required, bool), "invalid permission required flag")
    require(scope is None or
            (isinstance(scope, str) and "\0" not in scope and
             len(scope.encode("utf-8")) <= 1024),
            "invalid permission scope")
    items = [(1, name.encode("ascii")), (2, struct.pack("<B", int(required)))]
    if scope is not None:
        items.append((3, scope.encode("utf-8")))
    return records(items)


def ipc_endpoint(item):
    name = item.get("name")
    component_id = item.get("component")
    require(isinstance(name, str) and SAFE_ID.fullmatch(name), "invalid IPC endpoint name")
    require(isinstance(component_id, str) and SAFE_ID.fullmatch(component_id),
            "invalid IPC endpoint component")
    return records([(1, name.encode("ascii")), (2, component_id.encode("ascii"))])


def canonical_locale_tag(value):
    if not isinstance(value, str):
        return False
    try:
        encoded = value.encode("ascii")
    except UnicodeEncodeError:
        return False
    if not 2 <= len(encoded) <= 63:
        return False
    subtags = value.split("-")
    if not 2 <= len(subtags[0]) <= 8 or not subtags[0].isalpha() or not subtags[0].islower():
        return False
    for subtag in subtags[1:]:
        if not 1 <= len(subtag) <= 8 or not subtag.isalnum():
            return False
        if len(subtag) == 4:
            if subtag != subtag[:1].upper() + subtag[1:].lower():
                return False
        elif len(subtag) == 2:
            if subtag != subtag.upper():
                return False
        elif any(character.isalpha() and not character.islower() for character in subtag):
            return False
    return True


def localization(locale, item, file_paths):
    require(canonical_locale_tag(locale), f"invalid localization locale: {locale}")
    require(isinstance(item, dict) and set(item).issubset({"name", "description", "icon"}),
            f"invalid localization for {locale}")
    require(item, f"empty localization for {locale}")
    encoded = [(1, locale.encode("ascii"))]
    if "name" in item:
        name = item["name"]
        require(isinstance(name, str) and name and "\0" not in name and
                len(name.encode("utf-8")) <= 128,
                f"invalid localized name for {locale}")
        encoded.append((2, name.encode("utf-8")))
    if "description" in item:
        description = item["description"]
        require(isinstance(description, str) and description and
                "\0" not in description and
                len(description.encode("utf-8")) <= 512,
                f"invalid localized description for {locale}")
        encoded.append((3, description.encode("utf-8")))
    if "icon" in item:
        icon = item["icon"]
        require(isinstance(icon, str) and PACKAGE_PATH.fullmatch(icon) and
                icon in file_paths, f"invalid localized icon for {locale}")
        encoded.append((4, icon.encode("ascii")))
    return records(encoded)


def catalog_application_localizations(package_source_dir, app_id):
    i18n_dir = package_source_dir / "i18n"
    source_path = i18n_dir / "messages.yaml"
    if not source_path.is_file():
        require(not i18n_dir.is_dir() or not any(i18n_dir.glob("*.yaml")),
                "i18n/messages.yaml is required when locale catalogs exist")
        return {}
    i18n_tool_dir = Path(__file__).resolve().parent.parent / "i18n"
    if str(i18n_tool_dir) not in sys.path:
        sys.path.insert(0, str(i18n_tool_dir))
    try:
        from compile_catalog import validate as validate_i18n_catalog
    except ImportError as error:
        raise PackageError("i18n catalog compiler is unavailable") from error
    locale_paths = sorted(
        path for path in source_path.parent.glob("*.yaml")
        if path.name != source_path.name
    )
    try:
        namespace, _default_locale, _messages, _catalogs, metadata_catalogs = (
            validate_i18n_catalog(source_path, locale_paths))
    except ValueError as error:
        raise PackageError(f"invalid i18n catalog: {error}") from error
    require(namespace == f"app.{app_id}",
            f"i18n namespace must be app.{app_id}")

    return metadata_catalogs


def read_der_tlv(data, offset):
    require(offset < len(data), "invalid DER")
    tag = data[offset]
    offset += 1
    require(offset < len(data), "invalid DER length")
    length_octet = data[offset]
    offset += 1
    if length_octet & 0x80:
        length_size = length_octet & 0x7F
        require(0 < length_size <= 4 and offset + length_size <= len(data),
                "invalid DER length")
        length = int.from_bytes(data[offset:offset + length_size], "big")
        require(length >= 128, "non-canonical DER length")
        offset += length_size
    else:
        length = length_octet
    end = offset + length
    require(end <= len(data), "invalid DER length")
    return tag, data[offset:end], end


def validate_p256_public_key(public_der):
    tag, subject_public_key_info, end = read_der_tlv(public_der, 0)
    require(tag == 0x30 and end == len(public_der), "signing key must be EC P-256")
    tag, algorithm, offset = read_der_tlv(subject_public_key_info, 0)
    require(tag == 0x30, "signing key must be EC P-256")
    tag, key_bits, end = read_der_tlv(subject_public_key_info, offset)
    require(tag == 0x03 and end == len(subject_public_key_info) and key_bits[:1] == b"\0",
            "signing key must be EC P-256")
    tag, algorithm_oid, offset = read_der_tlv(algorithm, 0)
    require(tag == 0x06 and algorithm_oid == EC_PUBLIC_KEY_OID,
            "signing key must be EC P-256")
    tag, curve_oid, end = read_der_tlv(algorithm, offset)
    require(tag == 0x06 and curve_oid == P256_OID and end == len(algorithm),
            "signing key must be EC P-256")


def parse_ecdsa_signature(signature_der):
    tag, sequence, end = read_der_tlv(signature_der, 0)
    require(tag == 0x30 and end == len(signature_der), "invalid ECDSA signature")
    tag, r_bytes, offset = read_der_tlv(sequence, 0)
    require(tag == 0x02 and r_bytes, "invalid ECDSA signature")
    tag, s_bytes, end = read_der_tlv(sequence, offset)
    require(tag == 0x02 and s_bytes and end == len(sequence), "invalid ECDSA signature")
    return int.from_bytes(r_bytes, "big"), int.from_bytes(s_bytes, "big")


def openssl(arguments, input_bytes=None):
    try:
        return subprocess.run(
            ["openssl", *arguments], input=input_bytes, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, check=True).stdout
    except FileNotFoundError as error:
        raise PackageError("OpenSSL is required to sign PXA packages") from error
    except subprocess.CalledProcessError as error:
        detail = error.stderr.decode("utf-8", "replace").strip()
        raise PackageError(f"OpenSSL failed: {detail or 'unknown error'}") from error


def package_files(package_dir):
    files = []
    for path in package_dir.rglob("*"):
        if not path.is_file():
            continue
        relative = path.relative_to(package_dir).as_posix()
        if relative in {"manifest.pxm", "signature.pxs"}:
            continue
        require(PACKAGE_PATH.fullmatch(relative), f"invalid package path: {relative}")
        content = path.read_bytes()
        require(not (relative.startswith("artifacts/") and not content),
                f"empty artifact: {relative}")
        files.append((relative, content))
    files.sort(key=lambda entry: entry[0])
    require(files, "package has no payload")
    return files


def component_artifacts(file_paths, component_id, target, artifact_mode):
    wasm_path = f"artifacts/{component_id}.wasm"
    include_wasm = artifact_mode in ("wasm", "both")
    include_aot = artifact_mode in ("aot", "both")
    if include_wasm:
        require(wasm_path in file_paths,
                f"required artifact is missing: {wasm_path}")
    pattern = re.compile(rf"artifacts/{re.escape(component_id)}\.([a-z0-9._-]+)\.aot")
    aot_targets = sorted(
        match.group(1) for path in file_paths if (match := pattern.fullmatch(path)))
    if include_aot:
        require(aot_targets, f"package has no AOT artifact for component: {component_id}")
        require(target in aot_targets,
                f"required artifact is missing: artifacts/{component_id}.{target}.aot")
    else:
        require(not aot_targets,
                f"unexpected AOT artifact for WASM-only component: {component_id}")
    return aot_targets, include_wasm


def parse_services(names, label):
    require(isinstance(names, list), f"{label} must be an array")
    require(all(isinstance(name, str) and name in DECLARABLE_SERVICE_IDS for name in names),
            f"invalid {label[:-1]} name")
    require(len(names) == len(set(names)), f"{label} must be unique")
    return names


def parse_wasi(value):
    if value is None:
        return None
    require(isinstance(value, dict), "component wasi must be an object")
    require(set(value).issubset({"version", "libc", "features"}),
            "component wasi has an unknown field")
    require(value.get("version") == "preview1",
            "component wasi version must be preview1")
    require(value.get("libc") == "wasi-libc",
            "component wasi libc must be wasi-libc")
    features = value.get("features", [])
    require(isinstance(features, list), "component wasi features must be an array")
    require(all(isinstance(name, str) and name in WASI_FEATURES for name in features),
            "invalid component wasi feature")
    require(features == sorted(set(features)),
            "component wasi features must be sorted uniquely")
    feature_bits = 0
    for name in features:
        feature_bits |= WASI_FEATURES[name]
    return feature_bits


def parse_sdk(metadata, name, default):
    value = metadata.get(name, default)
    require(isinstance(value, list) and len(value) == 2 and
            all(isinstance(item, int) and not isinstance(item, bool) and
                0 <= item <= 0xFFFF for item in value),
            f"{name} must be [major, minor]")
    return tuple(value)


def main(argv):
    aot_only = False
    if len(argv) == 6 and argv[-1] == "--aot-only":
        aot_only = True
        argv = argv[:-1]
    require(len(argv) == 5,
            "usage: build_package_manifest.py <package.json> <package-dir> "
            "<private-key.pem> <target> <engine-abi> [--aot-only]")
    metadata_path, package_dir_arg, private_key_path, target, engine_abi = argv
    package_dir = Path(package_dir_arg).resolve()
    package_source_dir = Path(metadata_path).resolve().parent
    try:
        metadata = json.loads(Path(metadata_path).read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise PackageError(f"unable to read package metadata: {error}") from error
    require(isinstance(metadata, dict) and metadata.get("format") == "pxa-package-source-0.1",
            "unsupported source format")

    app_id = metadata.get("id")
    version = metadata.get("version")
    require(isinstance(app_id, str) and SAFE_ID.fullmatch(app_id), "invalid app id")
    require(isinstance(version, str) and VERSION.fullmatch(version), "invalid version")
    release_sequence = metadata.get("release_sequence", 1)
    require(isinstance(release_sequence, int) and not isinstance(release_sequence, bool)
            and 0 < release_sequence <= 0xFFFFFFFFFFFFFFFF,
            "release_sequence must be a positive u64")
    min_sdk = parse_sdk(metadata, "min_sdk", [0, 1])
    target_sdk = parse_sdk(metadata, "target_sdk", list(min_sdk))
    compile_sdk = parse_sdk(metadata, "compile_sdk", list(target_sdk))
    require(min_sdk[0] == target_sdk[0] == compile_sdk[0] and
            min_sdk <= target_sdk <= compile_sdk,
            "SDK versions must use one major and min <= target <= compile")

    public_der = openssl(["pkey", "-in", private_key_path, "-pubout", "-outform", "DER"])
    validate_p256_public_key(public_der)
    publisher_id = hashlib.sha256(public_der).digest()
    files = package_files(package_dir)
    file_paths = {path for path, _ in files}
    catalog_localizations = catalog_application_localizations(
        package_source_dir, app_id)

    name = metadata.get("name")
    description = metadata.get("description")
    icon = metadata.get("icon")
    require(name is None or
            (isinstance(name, str) and name and "\0" not in name and
             len(name.encode("utf-8")) <= 128), "invalid name")
    require(description is None or
            (isinstance(description, str) and description and
             "\0" not in description and
             len(description.encode("utf-8")) <= 512),
            "invalid description")
    require(icon is None or
            (isinstance(icon, str) and PACKAGE_PATH.fullmatch(icon)),
            "invalid icon path")
    require(icon is None or icon in file_paths, "icon is missing")

    raw_localizations = metadata.get("localizations")
    require(raw_localizations is None or not catalog_localizations,
            "package.json localizations conflict with i18n metadata resources")
    if raw_localizations is None:
        raw_localizations = catalog_localizations
    require(isinstance(raw_localizations, dict) and len(raw_localizations) <= 32,
            "localizations must be an object with at most 32 locales")
    localization_entries = [
        (locale, localization(locale, item, file_paths))
        for locale, item in sorted(raw_localizations.items())
    ]

    top = [(1, publisher_id), (2, app_id.encode("ascii")), (3, version.encode("ascii"))]
    for tag, value in ((4, name), (5, description)):
        if value is not None:
            top.append((tag, value.encode("utf-8")))
    if icon is not None:
        top.append((6, icon.encode("ascii")))
    top.extend([(7, struct.pack("<HH", *min_sdk)), (8, struct.pack("<HH", *target_sdk))])
    top.append((9, struct.pack("<Q", release_sequence)))
    lineage_path = metadata.get("publisher_lineage")
    if lineage_path is not None:
        require(isinstance(lineage_path, str) and lineage_path,
                "publisher_lineage must be a relative file path")
        lineage_file = (Path(metadata_path).resolve().parent / lineage_path).resolve()
        require(lineage_file.is_relative_to(Path(metadata_path).resolve().parent),
                "publisher_lineage must remain inside the App source")
        try:
            lineage = lineage_file.read_bytes()
        except OSError as error:
            raise PackageError(f"unable to read publisher_lineage: {error}") from error
        require(lineage.startswith(b"PXKL"), "invalid publisher_lineage")
        top.append((10, lineage))
    top.append((11, public_der))

    permissions = metadata.get("permissions", [])
    require(isinstance(permissions, list) and len(permissions) <= 64,
            "permissions must be an array")
    permission_entries = []
    for item in permissions:
        require(isinstance(item, dict), "invalid permission")
        encoded = permission(item)
        permission_entries.append((item["name"], item.get("scope", "").encode("utf-8"), encoded))
    require(all((left[0], left[1]) < (right[0], right[1])
                for left, right in zip(permission_entries, permission_entries[1:])),
            "permissions must be sorted uniquely")

    ipc_endpoints = metadata.get("ipc_endpoints", [])
    require(isinstance(ipc_endpoints, list) and len(ipc_endpoints) <= 32,
            "ipc_endpoints must be an array")
    ipc_entries = []
    for item in ipc_endpoints:
        require(isinstance(item, dict), "invalid IPC endpoint")
        encoded = ipc_endpoint(item)
        ipc_entries.append((item["name"], item["component"], encoded))
    require(all(left[0] < right[0] for left, right in zip(ipc_entries, ipc_entries[1:])),
            "IPC endpoints must be sorted uniquely")

    build = metadata.get("build", {"system": "direct"})
    require(isinstance(build, dict) and
            set(build) <= {"system", "source_dir", "linear_memory"},
            "build must contain only system, source_dir, and linear_memory")
    build_system = build.get("system", "direct")
    require(build_system in ("direct", "cmake"),
            "build system must be direct or cmake")
    linear_memory = build.get("linear_memory")
    pinned_memory = False
    if linear_memory is not None:
        require(build_system == "direct",
                "build linear_memory is only supported by direct builds")
        require(isinstance(linear_memory, dict) and
                set(linear_memory) == {"maximum_bytes", "pinned"} and
                isinstance(linear_memory["maximum_bytes"], int) and
                not isinstance(linear_memory["maximum_bytes"], bool) and
                65536 <= linear_memory["maximum_bytes"] <= 4294967296 and
                linear_memory["maximum_bytes"] % 65536 == 0 and
                linear_memory["pinned"] is True,
                "build linear_memory must declare a page-aligned maximum_bytes and pinned=true")
        pinned_memory = True

    declared_services = parse_services(metadata.get("services", []), "services")
    raw_components = metadata.get("components", [{"id": "main", "kind": "ui"}])
    require(isinstance(raw_components, list) and 0 < len(raw_components) <= 32,
            "components must be a non-empty array")
    components = []
    for item in raw_components:
        require(isinstance(item, dict), "invalid component")
        component_id = item.get("id")
        kind_name = item.get("kind")
        require(isinstance(component_id, str) and SAFE_ID.fullmatch(component_id),
                "invalid component id")
        require(isinstance(kind_name, str) and kind_name in COMPONENT_KINDS,
                "invalid component kind")
        service_names = parse_services(item.get("services", declared_services), "component services")
        wasi_features = parse_wasi(item.get("wasi"))
        artifact_mode = item.get("artifact", "aot" if aot_only else "both")
        require(artifact_mode in ("aot", "wasm", "both"),
                "component artifact must be aot, wasm, or both")
        flags = COMPONENT_FLAG_PINNED_MEMORY if pinned_memory else 0
        components.append((component_id, COMPONENT_KINDS[kind_name], flags,
                           service_names, wasi_features, artifact_mode))
    require(all(left[0] < right[0] for left, right in zip(components, components[1:])),
            "components must be sorted uniquely")
    require(sum(component_id == "main" and kind == COMPONENT_KINDS["ui"]
                for component_id, kind, _, _, _, _ in components) == 1,
            "Package requires exactly one main UI component")
    component_ids = {component_id for component_id, _, _, _, _, _ in components}
    require(all(component_id in component_ids for _, component_id, _ in ipc_entries),
            "IPC endpoint references an unknown component")

    endpoint_components = {component_id for _, component_id, _ in ipc_entries}
    for component_id, kind, flags, service_names, wasi_features, artifact_mode in components:
        automatic_services = [1]
        if kind == COMPONENT_KINDS["ui"]:
            automatic_services.extend([2, 3, 4])
        if permission_entries:
            automatic_services.append(11)
        if component_id in endpoint_components:
            automatic_services.append(7)
        service_features = {
            service_id: 0
            for service_id in automatic_services +
            [SERVICE_IDS[name] for name in service_names]
        }
        if wasi_features is not None:
            service_features[SERVICE_IDS["wasi"]] = wasi_features
        services = sorted(service_features.items())
        aot_targets, include_wasm = component_artifacts(
            file_paths, component_id, target, artifact_mode)
        top.append((16, component(component_id, kind, flags, aot_targets, engine_abi,
                                  services, include_wasm=include_wasm)))

    for path, content in files:
        top.append((17, records([
            (1, path.encode("ascii")),
            (2, struct.pack("<Q", len(content))),
            (3, hashlib.sha256(content).digest()),
        ])))
    top.extend((18, entry[2]) for entry in permission_entries)
    top.extend((19, entry[2]) for entry in ipc_entries)
    top.extend((20, entry[1]) for entry in localization_entries)

    body = records(top)
    manifest_minor = 7
    manifest = b"PXAM" + struct.pack("<HHI", 0, manifest_minor, len(body)) + body
    require(len(manifest) <= 16 * 1024, "manifest exceeds Draft limit")
    signature_der = openssl(["dgst", "-sha256", "-sign", private_key_path],
                            b"PXA-PACKAGE-MANIFEST\0" + manifest)
    r, s = parse_ecdsa_signature(signature_der)
    require(0 < r < P256_ORDER and 0 < s < P256_ORDER, "invalid ECDSA signature")
    if s > P256_ORDER // 2:
        s = P256_ORDER - s
    signature = r.to_bytes(32, "big") + s.to_bytes(32, "big")
    envelope = b"PXAS" + struct.pack("<HH", 0x0001, 1) + publisher_id + struct.pack("<HH", 64, 0) + signature
    (package_dir / "manifest.pxm").write_bytes(manifest)
    (package_dir / "signature.pxs").write_bytes(envelope)
    print(f"{app_id} publisher={publisher_id.hex()} files={len(files)}")


if __name__ == "__main__":
    try:
        main(sys.argv[1:])
    except PackageError as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
