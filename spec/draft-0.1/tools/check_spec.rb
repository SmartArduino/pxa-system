#!/usr/bin/env ruby
# frozen_string_literal: true

require "json"

ROOT = File.expand_path("..", __dir__)
SPEC_PATH = File.join(ROOT, "pxa-core.yaml")
WINDOW_SPEC_PATH = File.join(ROOT, "pxa-window.yaml")
UI_SPEC_PATH = File.join(ROOT, "pxa-ui.yaml")
CLOCK_SPEC_PATH = File.join(ROOT, "pxa-clock.yaml")
FS_SPEC_PATH = File.join(ROOT, "pxa-fs.yaml")
STORAGE_SPEC_PATH = File.join(ROOT, "pxa-storage.yaml")
IPC_SPEC_PATH = File.join(ROOT, "pxa-ipc.yaml")
SENSOR_SPEC_PATH = File.join(ROOT, "pxa-sensor.yaml")
NET_SPEC_PATH = File.join(ROOT, "pxa-net.yaml")
AUDIO_SPEC_PATH = File.join(ROOT, "pxa-audio.yaml")
PERMISSION_SPEC_PATH = File.join(ROOT, "pxa-permission.yaml")
WORK_SPEC_PATH = File.join(ROOT, "pxa-work.yaml")
WASI_SPEC_PATH = File.join(ROOT, "pxa-wasi.yaml")
DEVICE_SPEC_PATH = File.join(ROOT, "pxa-device.yaml")
PACKAGE_SPEC_PATH = File.join(ROOT, "pxa-package.yaml")
CONTAINER_SPEC_PATH = File.join(ROOT, "pxa-container.yaml")
GOLDEN_PATH = File.join(ROOT, "golden", "core-vectors.json")
PACKAGE_GOLDEN_PATH = File.join(ROOT, "golden", "package-vectors.json")

def fail_spec(message)
  warn "PXA spec error: #{message}"
  exit 1
end

def unique_values!(items, field, label)
  values = items.map { |item| item.fetch(field) }
  duplicate = values.group_by(&:itself).find { |_value, members| members.length > 1 }
  fail_spec("duplicate #{label} #{duplicate[0].inspect}") if duplicate
end

def integer_in_range!(value, range, label)
  fail_spec("#{label} must be an integer") unless value.is_a?(Integer)
  fail_spec("#{label} is out of range") unless range.cover?(value)
end

def parse_hex(value)
  fail_spec("golden hex must contain complete bytes") unless value.is_a?(String) && value.match?(/\A(?:[0-9a-f]{2})*\z/)
  [value].pack("H*")
end

def encode_message(message)
  payload = parse_hex(message.fetch("payload_hex"))
  service = message.fetch("service")
  opcode = message.fetch("opcode")
  request_id = message.fetch("request_id")
  integer_in_range!(service, 0..0xffff, "message service")
  integer_in_range!(opcode, 0..0xffff, "message opcode")
  integer_in_range!(request_id, 0..0xffff_ffff, "message request ID")
  [service, opcode, request_id, payload.bytesize].pack("v2V2") + payload
end

def decode_message(bytes)
  fail_spec("golden message is shorter than the envelope") if bytes.bytesize < 12
  service, opcode, request_id, payload_len = bytes.unpack("v2V2")
  fail_spec("golden payload length does not match envelope") unless bytes.bytesize == 12 + payload_len
  {
    "service" => service,
    "opcode" => opcode,
    "request_id" => request_id,
    "payload_hex" => bytes.byteslice(12, payload_len).unpack1("H*")
  }
end

def encode_records(records)
  records.map do |record|
    payload = parse_hex(record.fetch("payload_hex"))
    tag = record.fetch("tag")
    integer_in_range!(tag, 0..0xffff, "record tag")
    fail_spec("record payload is too large") if payload.bytesize > 0xffff
    [tag, payload.bytesize].pack("v2") + payload
  end.join
end

spec = JSON.parse(File.read(SPEC_PATH))
window_spec = JSON.parse(File.read(WINDOW_SPEC_PATH))
ui_spec = JSON.parse(File.read(UI_SPEC_PATH))
clock_spec = JSON.parse(File.read(CLOCK_SPEC_PATH))
fs_spec = JSON.parse(File.read(FS_SPEC_PATH))
storage_spec = JSON.parse(File.read(STORAGE_SPEC_PATH))
ipc_spec = JSON.parse(File.read(IPC_SPEC_PATH))
sensor_spec = JSON.parse(File.read(SENSOR_SPEC_PATH))
net_spec = JSON.parse(File.read(NET_SPEC_PATH))
audio_spec = JSON.parse(File.read(AUDIO_SPEC_PATH))
permission_spec = JSON.parse(File.read(PERMISSION_SPEC_PATH))
work_spec = JSON.parse(File.read(WORK_SPEC_PATH))
wasi_spec = JSON.parse(File.read(WASI_SPEC_PATH))
device_spec = JSON.parse(File.read(DEVICE_SPEC_PATH))
package_spec = JSON.parse(File.read(PACKAGE_SPEC_PATH))
container_spec = JSON.parse(File.read(CONTAINER_SPEC_PATH))
fail_spec("unexpected schema") unless spec["schema"] == "pxa-core-spec-0.1"
fail_spec("Draft 0.1 must remain marked draft") unless spec["status"] == "draft"

abi = spec.fetch("abi")
fail_spec("encoded ABI version mismatch") unless abi.fetch("encoded") == ((abi.fetch("major") << 16) | abi.fetch("minor"))
fail_spec("ABI patch version must be zero") unless abi.fetch("patch") == 0
fail_spec("only little-endian is supported") unless abi.fetch("byte_order") == "little-endian"
integer_in_range!(abi.fetch("max_control_message"), 12..0xffff_ffff, "max control message")
integer_in_range!(abi.fetch("max_record_payload"), 0..0xffff, "max record payload")

envelope = spec.fetch("envelope")
fail_spec("envelope size must be 12") unless envelope.fetch("size") == 12
expected_envelope = [
  ["service", "u16", 0], ["opcode", "u16", 2],
  ["request_id", "u32", 4], ["payload_len", "u32", 8]
]
actual_envelope = envelope.fetch("fields").map { |field| [field["name"], field["type"], field["offset"]] }
fail_spec("envelope layout mismatch") unless actual_envelope == expected_envelope

record = spec.fetch("record")
fail_spec("record header size must be 4") unless record.fetch("header_size") == 4
fail_spec("record optional mask must be the tag high bit") unless record.fetch("optional_mask") == 0x8000
fail_spec("record tag mask must reserve the high bit") unless record.fetch("tag_mask") == 0x7fff

unique_values!(spec.fetch("statuses"), "name", "status name")
unique_values!(spec.fetch("statuses"), "value", "status value")
fail_spec("ok must be zero") unless spec.fetch("statuses").find { |item| item["name"] == "ok" }&.fetch("value") == 0
spec.fetch("statuses").each do |status|
  integer_in_range!(status.fetch("value"), -0x8000_0000..0, "status #{status.fetch('name')}")
end

%w[services core_opcodes io_operations ready_flags stop_reasons lease_kinds config_tags lease_tags].each do |section|
  items = spec.fetch(section)
  unique_values!(items, "name", "#{section} name")
  numeric_field = section == "services" || section == "core_opcodes" || section.end_with?("_tags") ? "id" : "value"
  numeric_field = "id" if section == "io_operations"
  unique_values!(items, numeric_field, "#{section} #{numeric_field}")
end

spec.fetch("services").each { |item| integer_in_range!(item.fetch("id"), 1..0xffff, "service ID") }
spec.fetch("core_opcodes").each { |item| integer_in_range!(item.fetch("id"), 1..0xffff, "Core opcode") }
spec.fetch("config_tags").each { |item| integer_in_range!(item.fetch("id"), 1..0x7fff, "config tag") }
spec.fetch("lease_tags").each { |item| integer_in_range!(item.fetch("id"), 1..0x7fff, "lease tag") }

fail_spec("unexpected Window schema") unless window_spec["schema"] == "pxa-window-spec-0.1"
fail_spec("Window draft must remain marked draft") unless window_spec["status"] == "draft"
window_service = window_spec.fetch("service")
fail_spec("Window service ID mismatch") unless window_service.fetch("id") == 2
fail_spec("Window service must be 0.1.0") unless window_service.values_at("major", "minor", "patch") == [0, 1, 0]
core_window = spec.fetch("services").find { |item| item["name"] == "window" }
fail_spec("Core and Window service definitions differ") unless core_window == {
  "name" => "window", "id" => window_service.fetch("id"), "state" => "defined",
  "major" => window_service.fetch("major"), "minor" => window_service.fetch("minor"),
  "patch" => window_service.fetch("patch")
}
%w[opcodes configure_tags snapshot_tags bar_modes icon_styles orientations].each do |section|
  items = window_spec.fetch(section)
  unique_values!(items, "name", "Window #{section} name")
  numeric_field = section.end_with?("_tags") || section == "opcodes" ? "id" : "value"
  unique_values!(items, numeric_field, "Window #{section} #{numeric_field}")
end
window_spec.fetch("opcodes").each { |item| integer_in_range!(item.fetch("id"), 1..0xffff, "Window opcode") }
window_spec.fetch("configure_tags").each { |item| integer_in_range!(item.fetch("id"), 1..0x7fff, "Window configure tag") }
window_spec.fetch("snapshot_tags").each { |item| integer_in_range!(item.fetch("id"), 1..0x7fff, "Window snapshot tag") }

[[ui_spec, "ui", 3], [clock_spec, "clock", 4], [fs_spec, "fs", 5],
 [storage_spec, "storage", 6],
 [ipc_spec, "ipc", 7],
 [sensor_spec, "sensor", 8], [net_spec, "net", 9],
 [audio_spec, "audio", 10],
 [permission_spec, "permission", 11],
 [work_spec, "work", 13], [wasi_spec, "wasi", 14],
 [device_spec, "device", 15]].each do |service_spec, name, id|
  fail_spec("unexpected #{name} schema") unless service_spec["schema"] == "pxa-#{name}-spec-0.1"
  fail_spec("#{name} draft must remain marked draft") unless service_spec["status"] == "draft"
  service = service_spec.fetch("service")
  major = 0
  minor = %w[net audio].include?(name) ? 2 : 1
  fail_spec("#{name} service mismatch") unless service == {
    "name" => name, "id" => id, "major" => major, "minor" => minor,
    "patch" => 0
  }
  core_service = spec.fetch("services").find { |item| item["name"] == name }
  fail_spec("Core and #{name} service definitions differ") unless core_service == {
    "name" => name, "id" => id, "state" => "defined", "major" => major,
    "minor" => minor, "patch" => 0
  }
  unique_values!(service_spec.fetch("opcodes"), "name", "#{name} opcode name")
  unique_values!(service_spec.fetch("opcodes"), "id", "#{name} opcode ID")
  service_spec.fetch("opcodes").each do |opcode|
    integer_in_range!(opcode.fetch("id"), 1..0xffff, "#{name} opcode")
  end
end

fail_spec("WASI ABI mismatch") unless wasi_spec.fetch("abi") == {
  "version" => "preview1", "execution_model" => "reactor", "libc" => "wasi-libc"
}
unique_values!(wasi_spec.fetch("features"), "name", "WASI feature name")
unique_values!(wasi_spec.fetch("features"), "bit", "WASI feature bit")
wasi_spec.fetch("features").each do |feature|
  integer_in_range!(feature.fetch("bit"), 0..63, "WASI feature bit")
end
fail_spec("WASI shared clock import mismatch") unless
  wasi_spec.fetch("security").fetch("clock_import_features") ==
    ["monotonic-clock", "wall-clock"]
fail_spec("WASI non-interactive feature mismatch") unless
  wasi_spec.fetch("security").fetch("noninteractive_features") ==
    ["monotonic-clock", "random", "wall-clock"]

%w[open_flags record_tags file_kinds seek_origins].each do |section|
  items = fs_spec.fetch(section)
  unique_values!(items, "name", "FS #{section} name")
  numeric_field = section == "record_tags" ? "id" : "value"
  unique_values!(items, numeric_field, "FS #{section} #{numeric_field}")
  minimum = section == "seek_origins" ? 0 : 1
  items.each do |item|
    integer_in_range!(item.fetch(numeric_field), minimum..0x7fff, "FS #{section}")
  end
end
fs_spec.fetch("opcodes").each { |item| integer_in_range!(item.fetch("id"), 1..0xffff, "FS opcode") }
fs_limits = fs_spec.fetch("limits")
fail_spec("FS path limit mismatch") unless fs_limits.fetch("max_path_bytes") == 255
fail_spec("FS path segment limit mismatch") unless fs_limits.fetch("max_segment_bytes") == 64

%w[record_tags].each do |section|
  items = storage_spec.fetch(section)
  unique_values!(items, "name", "Storage #{section} name")
  unique_values!(items, "id", "Storage #{section} ID")
end
storage_spec.fetch("opcodes").each { |item| integer_in_range!(item.fetch("id"), 1..0xffff, "Storage opcode") }
storage_limits = storage_spec.fetch("limits")
fail_spec("Storage key limit mismatch") unless storage_limits.fetch("max_key_bytes") == 64
fail_spec("Storage entry limit mismatch") unless storage_limits.fetch("max_entries") == 32
fail_spec("Storage list page limit mismatch") unless storage_limits.fetch("max_list_result_entries") == 14
fail_spec("Storage value limit mismatch") unless storage_limits.fetch("max_value_bytes") == 2048

%w[record_tags events].each do |section|
  items = ipc_spec.fetch(section)
  unique_values!(items, "name", "IPC #{section} name")
  numeric_field = section == "record_tags" ? "id" : "opcode"
  unique_values!(items, numeric_field, "IPC #{section} #{numeric_field}")
  items.each { |item| integer_in_range!(item.fetch(numeric_field), 1..0xffff, "IPC #{section}") }
end
ipc_spec.fetch("opcodes").each { |item| integer_in_range!(item.fetch("id"), 1..0xffff, "IPC opcode") }
ipc_limits = ipc_spec.fetch("limits")
fail_spec("IPC endpoint limit mismatch") unless ipc_limits.fetch("max_endpoint_bytes") == 64
fail_spec("IPC payload limit mismatch") unless ipc_limits.fetch("max_payload_bytes") == 1024
fail_spec("IPC pending-call limit mismatch") unless ipc_limits.fetch("max_pending_calls") == 16

%w[record_tags units].each do |section|
  items = sensor_spec.fetch(section)
  unique_values!(items, "name", "Sensor #{section} name")
  numeric_field = section == "record_tags" ? "id" : "value"
  unique_values!(items, numeric_field, "Sensor #{section} #{numeric_field}")
end
sensor_spec.fetch("opcodes").each { |item| integer_in_range!(item.fetch("id"), 1..0xffff, "Sensor opcode") }
sensor_limits = sensor_spec.fetch("limits")
fail_spec("Sensor semantic limit mismatch") unless sensor_limits.fetch("max_semantic_bytes") == 64
fail_spec("Sensor dimension limit mismatch") unless sensor_limits.fetch("max_dimensions") == 3
fail_spec("Sensor subscription limit mismatch") unless sensor_limits.fetch("max_subscriptions_per_component") == 2
fail_spec("Sensor sample batch limit mismatch") unless sensor_limits.fetch("max_samples_per_event") == 8
fail_spec("unexpected Network schema") unless net_spec["schema"] == "pxa-net-spec-0.1"
fail_spec("Network draft must remain marked draft") unless net_spec["status"] == "draft"
net_methods = net_spec.fetch("methods")
unique_values!(net_methods, "name", "Network method name")
unique_values!(net_methods, "value", "Network method value")
fail_spec("Network method assignments mismatch") unless net_methods == [
  {"name" => "get", "value" => 1},
  {"name" => "head", "value" => 2},
  {"name" => "post", "value" => 3},
  {"name" => "put", "value" => 4},
  {"name" => "patch", "value" => 5},
  {"name" => "delete", "value" => 6}
]
%w[record_tags header_tags response_flags].each do |section|
  items = net_spec.fetch(section)
  unique_values!(items, "name", "Network #{section} name")
  numeric_field = section == "response_flags" ? "value" : "id"
  unique_values!(items, numeric_field, "Network #{section} #{numeric_field}")
end
net_limits = net_spec.fetch("limits")
fail_spec("Network URL limit mismatch") unless net_limits.fetch("max_url_bytes") == 512
fail_spec("Network response limit mismatch") unless net_limits.fetch("max_response_bytes") == 4096
fail_spec("Network request limit mismatch") unless net_limits.fetch("max_requests_per_component") == 2
fail_spec("Network header limit mismatch") unless net_limits.fetch("max_headers") == 8 &&
                                                net_limits.fetch("max_header_name_bytes") == 64 &&
                                                net_limits.fetch("max_header_value_bytes") == 256
fail_spec("Network body limit mismatch") unless net_limits.fetch("max_inline_body_bytes") == 2048
fail_spec("Network timeout range mismatch") unless net_limits.fetch("min_timeout_ms") == 100 &&
                                                  net_limits.fetch("default_timeout_ms") == 15_000 &&
                                                  net_limits.fetch("max_timeout_ms") == 60_000

%w[record_tags decisions].each do |section|
  items = permission_spec.fetch(section)
  unique_values!(items, "name", "Permission #{section} name")
  numeric_field = section == "record_tags" ? "id" : "value"
  unique_values!(items, numeric_field, "Permission #{section} #{numeric_field}")
end
permission_spec.fetch("opcodes").each { |item| integer_in_range!(item.fetch("id"), 1..0xffff, "Permission opcode") }
permission_limits = permission_spec.fetch("limits")
fail_spec("Permission name limit mismatch") unless permission_limits.fetch("max_name_bytes") == 96
fail_spec("Permission scope limit mismatch") unless permission_limits.fetch("max_scope_bytes") == 1024

audio_limits = audio_spec.fetch("limits")
fail_spec("Audio session limit mismatch") unless audio_limits.fetch("max_sessions_per_component") == 1
fail_spec("Audio EQ limit mismatch") unless audio_limits.fetch("max_eq_bands") == 5
fail_spec("Audio sample-rate range mismatch") unless audio_limits.fetch("min_sample_rate") == 8000 && audio_limits.fetch("max_sample_rate") == 48_000
fail_spec("Audio frame range mismatch") unless audio_limits.fetch("min_frame_ms") == 5 && audio_limits.fetch("max_frame_ms") == 120
%w[record_tags query_state_tags graph_tags usages routes].each do |section|
  items = audio_spec.fetch(section)
  unique_values!(items, "name", "Audio #{section} name")
  unique_values!(items, section == "usages" || section == "routes" ? "value" : "id", "Audio #{section} value")
end
audio_spec.fetch("opcodes").each { |item| integer_in_range!(item.fetch("id"), 1..0xffff, "Audio opcode") }

work_limits = work_spec.fetch("limits")
fail_spec("Work entry limit mismatch") unless work_limits.fetch("max_entries_per_app") == 8
fail_spec("Work initial delay mismatch") unless work_limits.fetch("min_initial_delay_ms") == 0
fail_spec("Work maximum delay mismatch") unless work_limits.fetch("max_delay_ms") == 604_800_000
fail_spec("Work execution limit mismatch") unless work_limits.fetch("max_execution_ms") == 60_000
fail_spec("Work input limit mismatch") unless work_limits.fetch("max_input_bytes") == 24
fail_spec("Work attempt limit mismatch") unless work_limits.fetch("max_attempts") == 5
%w[record_tags].each do |section|
  items = work_spec.fetch(section)
  unique_values!(items, "name", "Work #{section} name")
  unique_values!(items, "id", "Work #{section} ID")
end
work_spec.fetch("opcodes").each { |item| integer_in_range!(item.fetch("id"), 1..0xffff, "Work opcode") }

%w[commands properties].each do |section|
  unique_values!(ui_spec.fetch(section), "name", "UI #{section} name")
  unique_values!(ui_spec.fetch(section), "id", "UI #{section} ID")
end
%w[node_types pointer_phases].each do |section|
  unique_values!(ui_spec.fetch(section), "name", "UI #{section} name")
  unique_values!(ui_spec.fetch(section), "value", "UI #{section} value")
end
canvas = ui_spec.fetch("canvas")
fail_spec("Canvas primitive limit mismatch") unless canvas.fetch("max_primitives") == 1024
unique_values!(canvas.fetch("primitives"), "name", "Canvas primitive name")
unique_values!(canvas.fetch("primitives"), "id", "Canvas primitive ID")
unique_values!(canvas.fetch("alignments"), "name", "Canvas alignment name")
unique_values!(canvas.fetch("alignments"), "value", "Canvas alignment value")

fail_spec("unexpected Package schema") unless package_spec["schema"] == "pxa-package-spec-0.1"
fail_spec("Package draft must remain marked draft") unless package_spec["status"] == "draft"
manifest_spec = package_spec.fetch("manifest")
fail_spec("Package manifest constants mismatch") unless manifest_spec == {
  "magic" => "PXAM", "major" => 0, "minor" => 1, "patch" => 0,
  "header_size" => 12, "max_size" => 16_384
}
signature_spec = package_spec.fetch("signature")
fail_spec("Package signature constants mismatch") unless signature_spec == {
  "magic" => "PXAS", "version" => "0.1.0", "encoded_version" => 1,
  "header_size" => 44,
  "algorithm" => "ecdsa-p256-sha256", "algorithm_id" => 1,
  "signature_size" => 64, "encoding" => "p1363-low-s",
  "domain" => "PXA-PACKAGE-MANIFEST"
}
%w[top_tags component_tags artifact_tags file_tags service_requirement_tags permission_tags ipc_endpoint_tags].each do |section|
  items = package_spec.fetch(section)
  unique_values!(items, "name", "Package #{section} name")
  unique_values!(items, "id", "Package #{section} ID")
  items.each { |item| integer_in_range!(item.fetch("id"), 1..0x7fff, "Package #{section} ID") }
end
%w[component_kinds artifact_kinds memory_models wasm_features].each do |section|
  items = package_spec.fetch(section)
  unique_values!(items, "name", "Package #{section} name")
  unique_values!(items, "value", "Package #{section} value")
end

fail_spec("unexpected Container schema") unless container_spec["schema"] == "pxa-container-spec-0.1"
fail_spec("Container draft must remain marked draft") unless container_spec["status"] == "draft"
fail_spec("Container extension mismatch") unless container_spec["extension"] == ".pxa"
fail_spec("Container byte order mismatch") unless container_spec["byte_order"] == "little-endian"
container_header = container_spec.fetch("header")
fail_spec("Container header identity mismatch") unless container_header.values_at("magic", "major", "minor", "patch", "size") == ["PXAC", 0, 1, 0, 64]
expected_container_header = [
  ["magic", "bytes4", 0], ["major", "u16", 4], ["minor", "u16", 6],
  ["header_size", "u16", 8], ["flags", "u16", 10], ["codec", "u16", 12],
  ["chunk_size_log2", "u16", 14], ["manifest_size", "u32", 16],
  ["package_signature_size", "u32", 20], ["container_signature_size", "u32", 24],
  ["file_count", "u32", 28], ["payload_size", "u64", 32],
  ["unpacked_size", "u64", 40], ["container_size", "u64", 48],
  ["reserved", "u32", 56], ["header_crc32", "u32", 60]
]
actual_container_header = container_header.fetch("fields").map { |field| [field["name"], field["type"], field["offset"]] }
fail_spec("Container header layout mismatch") unless actual_container_header == expected_container_header
fail_spec("Container header constants mismatch") unless container_header.values_at("flags", "reserved", "crc32_range", "crc32_polynomial") == [0, 0, [0, 60], "ieee"]
fail_spec("Container Manifest constants mismatch") unless container_spec.fetch("package_manifest") == {"magic" => "PXAM", "max_size" => 16_384}
fail_spec("Container Package signature constants mismatch") unless container_spec.fetch("package_signature") == {"magic" => "PXAS", "version" => "0.1.0", "encoded_version" => 1, "size" => 108}
container_signature = container_spec.fetch("container_signature")
fail_spec("Container signature constants mismatch") unless container_signature == {
  "magic" => "PXCS", "version" => "0.1.0", "encoded_version" => 1,
  "size" => 108,
  "algorithm" => "ecdsa-p256-sha256", "algorithm_id" => 1,
  "signature_size" => 64, "encoding" => "p1363-low-s",
  "domain" => "PXA-PACKAGE-CONTAINER-DIGEST",
  "message" => "sha256(header || manifest || package_signature || payload)",
  "covered_sections" => %w[header manifest package_signature payload]
}
container_codecs = container_spec.fetch("codecs")
unique_values!(container_codecs, "name", "Container codec name")
unique_values!(container_codecs, "value", "Container codec value")
fail_spec("Container codec assignments mismatch") unless container_codecs == [
  {"name" => "store", "value" => 0},
  {"name" => "lz4-block-independent", "value" => 1}
]
fail_spec("Container chunk size mismatch") unless container_spec.values_at("chunk_size_log2", "chunk_size") == [12, 4096]
file_record = container_spec.fetch("file_record")
expected_file_record = [
  ["file_index", "u16", 0], ["flags", "u16", 2],
  ["chunk_count", "u32", 4], ["encoded_size", "u64", 8]
]
actual_file_record = file_record.fetch("fields").map { |field| [field["name"], field["type"], field["offset"]] }
fail_spec("Container file record mismatch") unless file_record.values_at("size", "flags") == [16, 0] && actual_file_record == expected_file_record
chunk_record = container_spec.fetch("chunk_record")
expected_chunk_record = [["stored_size", "u16", 0], ["decoded_size", "u16", 2]]
actual_chunk_record = chunk_record.fetch("fields").map { |field| [field["name"], field["type"], field["offset"]] }
fail_spec("Container chunk record mismatch") unless chunk_record.values_at("header_size", "max_stored_size", "max_decoded_size") == [4, 4096, 4096] && actual_chunk_record == expected_chunk_record
manifest_0_2 = container_spec.fetch("manifest_0_2")
fail_spec("Container update ordering profile mismatch") unless manifest_0_2 == {
  "release_sequence_tag" => 9, "release_sequence_type" => "u64",
  "release_sequence_required" => true, "release_sequence_min" => 1,
  "publisher_lineage_tag" => 10, "publisher_lineage_required" => false
}
manifest_0_2.values_at("release_sequence_tag", "publisher_lineage_tag").each do |tag|
  fail_spec("Manifest 0.2 tag collides with the 0.1 baseline") if package_spec.fetch("top_tags").any? { |item| item["id"] == tag }
end
publisher_lineage = container_spec.fetch("publisher_lineage")
fail_spec("Publisher lineage constants mismatch") unless publisher_lineage.values_at(
  "magic", "major", "minor", "patch", "header_size", "max_links", "max_spki_size",
  "signature_size", "signature_domain", "link_header_size"
) == ["PXKL", 0, 1, 0, 16, 8, 160, 64, "PXA-PUBLISHER-KEY-ROTATION", 80]
lineage_flags = publisher_lineage.fetch("flags")
unique_values!(lineage_flags, "name", "Publisher lineage flag name")
unique_values!(lineage_flags, "value", "Publisher lineage flag value")
fail_spec("Publisher lineage flags mismatch") unless lineage_flags == [
  {"name" => "revoke-old-signer", "value" => 1}
]
expected_lineage_header = [
  ["magic", "bytes4", 0], ["major", "u16", 4], ["minor", "u16", 6],
  ["header_size", "u16", 8], ["link_count", "u16", 10],
  ["body_size", "u32", 12]
]
actual_lineage_header = publisher_lineage.fetch("header_fields").map { |field| [field["name"], field["type"], field["offset"]] }
fail_spec("Publisher lineage header layout mismatch") unless actual_lineage_header == expected_lineage_header
expected_lineage_link = [
  ["generation", "u32", 0], ["flags", "u32", 4],
  ["old_key_id", "bytes32", 8], ["new_key_id", "bytes32", 40],
  ["new_spki_size", "u16", 72], ["signature_size", "u16", 74],
  ["reserved", "u32", 76]
]
actual_lineage_link = publisher_lineage.fetch("link_fields").map { |field| [field["name"], field["type"], field["offset"]] }
fail_spec("Publisher lineage link layout mismatch") unless actual_lineage_link == expected_lineage_link
fail_spec("Container update policy mismatch") unless container_spec.fetch("update_policy") == {
  "downgrade_allowed" => true, "downgrade_requires_confirmation" => true,
  "automatic_downgrade_allowed" => false,
  "automatic_signer_rollback_allowed" => false
}
fail_spec("Container Host profile limits mismatch") unless container_spec.fetch("host_profiles") == {"portable_max_files" => 128, "esp_product_max_files" => 32}

fail_spec("missing Package golden vectors") unless File.exist?(PACKAGE_GOLDEN_PATH)
package_golden = JSON.parse(File.read(PACKAGE_GOLDEN_PATH))
fail_spec("Package golden schema mismatch") unless package_golden["schema"] == "pxa-package-golden-0.1"
manifest_bytes = parse_hex(package_golden.fetch("manifest_hex"))
signature_bytes = parse_hex(package_golden.fetch("signature_hex"))
signed_message = parse_hex(package_golden.fetch("signature_message_hex"))
fail_spec("Package golden manifest magic mismatch") unless manifest_bytes.byteslice(0, 4) == "PXAM"
major, minor, body_size = manifest_bytes.byteslice(4, 8).unpack("v2V")
fail_spec("Package golden manifest version mismatch") unless [major, minor] == [0, 1]
fail_spec("Package golden manifest length mismatch") unless manifest_bytes.bytesize == 12 + body_size
fail_spec("Package golden manifest exceeds limit") if manifest_bytes.bytesize > manifest_spec.fetch("max_size")
fail_spec("Package golden signature length mismatch") unless signature_bytes.bytesize == 44 + 64
fail_spec("Package golden signature magic mismatch") unless signature_bytes.byteslice(0, 4) == "PXAS"
fail_spec("Package golden signature version mismatch") unless signature_bytes.byteslice(4, 2).unpack1("v") == 1
fail_spec("Package golden signature message mismatch") unless signed_message == "PXA-PACKAGE-MANIFEST\0" + manifest_bytes

unless File.exist?(GOLDEN_PATH)
  fail_spec("missing #{GOLDEN_PATH}; run tools/generate_golden.rb")
end

golden = JSON.parse(File.read(GOLDEN_PATH))
fail_spec("golden schema mismatch") unless golden["schema"] == "pxa-core-golden-0.1"
names = golden.fetch("vectors").map { |vector| vector.fetch("name") }
fail_spec("duplicate golden vector name") unless names.uniq.length == names.length

golden.fetch("vectors").each do |vector|
  encoded = case vector.fetch("kind")
            when "message"
              encode_message(vector.fetch("message"))
            when "records"
              encode_records(vector.fetch("records"))
            else
              fail_spec("unknown golden kind #{vector['kind'].inspect}")
            end
  expected = parse_hex(vector.fetch("wire_hex"))
  fail_spec("golden vector #{vector['name']} is stale") unless encoded == expected

  next unless vector["kind"] == "message"
  decoded = decode_message(expected)
  fail_spec("golden vector #{vector['name']} does not round-trip") unless decoded == vector.fetch("message")
end

required_service_vectors = %w[ui.begin.reset ui.batch.create-screen ui.commit ui.canvas.begin ui.canvas.append ui.canvas.present ui.event ui.pointer clock.set-period clock.tick fs.open fs.read-directory storage.set storage.get ipc.call ipc.request ipc.result permission.acquire permission.revoked audio.open-session audio.commit-graph work.enqueue work.cancel work.complete device.get-mac]
missing_service_vectors = required_service_vectors - names
fail_spec("missing service golden vectors: #{missing_service_vectors.join(', ')}") unless missing_service_vectors.empty?

puts "PXA Draft 0.1 spec OK (#{golden.fetch('vectors').length} golden vectors)"
puts "PXA Package Draft 0.1 spec OK (#{package_golden.fetch('inventory').length} payload files)"
puts "PXA Container Draft 0.1 schema OK"
