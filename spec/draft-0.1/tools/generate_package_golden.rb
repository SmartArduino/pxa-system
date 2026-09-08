#!/usr/bin/env ruby
# frozen_string_literal: true

require "digest"
require "fileutils"
require "json"

ROOT = File.expand_path("..", __dir__)
OUTPUT = ARGV.empty? ? File.join(ROOT, "golden", "package-vectors.json") : File.expand_path(ARGV[0])

def record(tag, payload)
  [tag, payload.bytesize].pack("v2") + payload
end

def records(items)
  items.map { |tag, payload| record(tag, payload) }.join
end

def artifact(kind:, path:, features:, memory:, target: nil, engine: nil, engine_abi: nil)
  items = [[1, [kind].pack("C")], [2, path.b]]
  items << [3, target.b] if target
  items << [4, engine.b] if engine
  items << [5, engine_abi.b] if engine_abi
  items << [6, [features].pack("Q<")]
  items << [7, [memory].pack("C")]
  records(items)
end

def service(id, min, max, features = 0)
  records([
    [1, [id].pack("v")],
    [2, min.pack("v2")],
    [3, max.pack("v2")],
    [4, [features].pack("Q<")]
  ])
end

def component(id, kind, artifacts, services)
  records(
    [[1, id.b], [2, [kind].pack("C")]] +
    artifacts.sort_by { |item| item.fetch(:path) }.map { |item| [4, artifact(**item)] } +
    services.sort_by { |item| item.fetch(:id) }.map do |item|
      [5, service(item.fetch(:id), item.fetch(:min), item.fetch(:max), item.fetch(:features, 0))]
    end
  )
end

def file_entry(path, content)
  records([
    [1, path.b],
    [2, [content.bytesize].pack("Q<")],
    [3, Digest::SHA256.digest(content)]
  ])
end

def permission(name, required, scope = nil)
  items = [[1, name.b], [2, [required ? 1 : 0].pack("C")]]
  items << [3, scope] if scope
  records(items)
end

publisher_key_id = (0...32).map { |index| index + 1 }.pack("C*")
payloads = {
  "artifacts/main.esp32p4.aot" => "aot-p4-main\x00".b,
  "artifacts/main.esp32s3.aot" => "aot-s3-main\x00".b,
  "artifacts/main.wasm" => "\x00asm-portable-main".b,
  "artifacts/sync.wasm" => "\x00asm-portable-sync".b,
  "assets/icon.png" => "PNG-golden-icon".b
}

main_artifacts = [
  {kind: 2, path: "artifacts/main.esp32p4.aot", target: "esp32-p4",
   engine: "wamr", engine_abi: "wamr-2.4.0-aot-v1-pxa0", features: 9, memory: 1},
  {kind: 2, path: "artifacts/main.esp32s3.aot", target: "esp32-s3",
   engine: "wamr", engine_abi: "wamr-2.4.0-aot-v1-pxa0", features: 1, memory: 1},
  {kind: 1, path: "artifacts/main.wasm", features: 1, memory: 1}
]
sync_artifacts = [
  {kind: 1, path: "artifacts/sync.wasm", features: 0, memory: 1}
]

top = [
  [1, publisher_key_id],
  [2, "com.example.reader".b],
  [3, "0.3.0".b],
  [4, "Reader".b],
  [5, "Golden multi-artifact package".b],
  [6, "assets/icon.png".b],
  [7, [0, 1].pack("v2")],
  [8, [0, 3].pack("v2")],
  [16, component("main", 1, main_artifacts,
                 [{id: 2, min: [0, 1], max: [0, 1]},
                  {id: 3, min: [0, 1], max: [0, 1], features: 1}])],
  [16, component("sync", 2, sync_artifacts,
                 [{id: 7, min: [0, 1], max: [0, 1]}])]
]
payloads.sort.each { |path, content| top << [17, file_entry(path, content)] }
top << [18, permission("fs.private", true)]
net_scope = records([[1, "api.example.com".b], [2, [443].pack("v")]])
top << [18, permission("net.client", false, net_scope)]

body = records(top)
manifest = "PXAM".b + [0, 1, body.bytesize].pack("v2V") + body
signature_bytes = (0...64).map { |index| 0xa0 + (index % 16) }.pack("C*")
signature = "PXAS".b + [0x0001, 1].pack("v2") + publisher_key_id +
            [64, 0].pack("v2") + signature_bytes
signed_message = "PXA-PACKAGE-MANIFEST\x00".b + manifest

document = {
  "schema" => "pxa-package-golden-0.1",
  "generated_by" => "tools/generate_package_golden.rb",
  "manifest_hex" => manifest.unpack1("H*"),
  "signature_hex" => signature.unpack1("H*"),
  "signature_message_hex" => signed_message.unpack1("H*"),
  "identity" => {
    "publisher_key_id_hex" => publisher_key_id.unpack1("H*"),
    "app_id" => "com.example.reader",
    "private_data_key" => "com.example.reader"
  },
  "inventory" => payloads.sort.map do |path, content|
    {"path" => path, "size" => content.bytesize,
     "sha256_hex" => Digest::SHA256.hexdigest(content)}
  end
}

FileUtils.mkdir_p(File.dirname(OUTPUT))
File.write(OUTPUT, JSON.pretty_generate(document) + "\n")
puts "Wrote #{OUTPUT}"
