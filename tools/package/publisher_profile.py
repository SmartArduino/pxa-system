"""Shared publisher display metadata rules; names never grant signing authority."""
import json
import re
import unicodedata

PROFILE_PATH = "assets/pxa-publisher.json"
PROFILE_MAX_BYTES = 1024
HANDLE = re.compile(r"[a-z][a-z0-9]*(?:-[a-z0-9]+)*\Z")
RESERVED = {"system", "app", "data", "media", "pxa", "admin", "official", "con", "prn", "aux", "nul"} | {f"{p}{i}" for p in ("com", "lpt") for i in range(1, 10)}

class PublisherProfileError(ValueError):
    def __init__(self, code, message):
        self.code = code
        super().__init__(message)


def validate_publisher(handle, display_name):
    if not isinstance(handle, str) or not HANDLE.fullmatch(handle) or len(handle) > 24 or handle in RESERVED:
        raise PublisherProfileError("INVALID_PUBLISHER_HANDLE", "作者标识须为 1–24 位小写字母、数字和单连字符，且不能使用保留名称")
    if not isinstance(display_name, str) or not display_name.strip() or any(unicodedata.category(c) in ("Cc", "Cf", "Cs") for c in display_name):
        raise PublisherProfileError("INVALID_PUBLISHER_NAME", "作者显示名不能为空或包含控制字符")
    if len(display_name.encode("utf-8")) > 128:
        raise PublisherProfileError("INVALID_PUBLISHER_NAME", "作者显示名最多 128 UTF-8 字节")
    return {"schema": "pxa.publisher-profile/1", "handle": handle, "display_name": display_name.strip()}


def encode_publisher(publisher):
    if not isinstance(publisher, dict) or set(publisher) != {"handle", "display_name"}:
        raise PublisherProfileError("INVALID_PUBLISHER_PROFILE", "publisher 只能包含 handle 和 display_name")
    profile = validate_publisher(publisher["handle"], publisher["display_name"])
    encoded = (json.dumps(profile, ensure_ascii=False, sort_keys=True, separators=(",", ":")) + "\n").encode("utf-8")
    if len(encoded) > PROFILE_MAX_BYTES:
        raise PublisherProfileError("INVALID_PUBLISHER_PROFILE", "作者资料超出大小限制")
    return encoded


def decode_publisher(encoded):
    if len(encoded) > PROFILE_MAX_BYTES:
        raise PublisherProfileError("INVALID_PUBLISHER_PROFILE", "作者资料超出大小限制")
    def unique(pairs):
        result = {}
        for k, v in pairs:
            if k in result: raise ValueError("duplicate publisher field")
            result[k] = v
        return result
    try:
        value = json.loads(encoded.decode("utf-8"), object_pairs_hook=unique)
        if not isinstance(value, dict) or value.get("schema") != "pxa.publisher-profile/1" or set(value) != {"schema", "handle", "display_name"}:
            raise ValueError("invalid publisher schema")
        return validate_publisher(value["handle"], value["display_name"])
    except (UnicodeError, ValueError) as error:
        if isinstance(error, PublisherProfileError): raise
        raise PublisherProfileError("INVALID_PUBLISHER_PROFILE", "作者资料格式无效") from error
