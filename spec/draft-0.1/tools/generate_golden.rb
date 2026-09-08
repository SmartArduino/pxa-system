#!/usr/bin/env ruby
# frozen_string_literal: true

require "json"
require "fileutils"

ROOT = File.expand_path("..", __dir__)
OUTPUT = ARGV.empty? ? File.join(ROOT, "golden", "core-vectors.json") : File.expand_path(ARGV[0])

def hex(bytes)
  bytes.unpack1("H*")
end

def message(service, opcode, request_id, payload)
  [service, opcode, request_id, payload.bytesize].pack("v2V2") + payload
end

def record(tag, payload)
  [tag, payload.bytesize].pack("v2") + payload
end

def message_vector(name, service, opcode, request_id, payload)
  {
    "name" => name,
    "kind" => "message",
    "message" => {
      "service" => service,
      "opcode" => opcode,
      "request_id" => request_id,
      "payload_hex" => hex(payload)
    },
    "wire_hex" => hex(message(service, opcode, request_id, payload))
  }
end

def records_vector(name, records)
  {
    "name" => name,
    "kind" => "records",
    "records" => records.map { |item| {"tag" => item[0], "payload_hex" => hex(item[1])} },
    "wire_hex" => hex(records.map { |item| record(item[0], item[1]) }.join)
  }
end

vectors = []
vectors << message_vector("core.close-handle", 1, 2, 0, [0x0102_0304].pack("V"))
vectors << message_vector("core.handle-ready", 1, 0x8001, 0,
                          [0x1122_3344, 0x0000_0003].pack("V2"))

lease_records = [
  [1, [2].pack("v")],
  [2, [30_000].pack("V")],
  [0x8007, "ignored-by-draft-host".b]
]
lease_payload = lease_records.map { |item| record(item[0], item[1]) }.join
vectors << records_vector("core.acquire-lease.records", lease_records)
vectors << message_vector("core.acquire-lease", 1, 3, 42, lease_payload)

lease_result = [0].pack("l<") + record(4, [0xa001_0001].pack("V"))
vectors << message_vector("core.acquire-lease.result", 1, 3, 42, lease_result)

config_records = [
  [1, [0x0102_0304_0506_0708].pack("Q<")],
  [2, "com.example.player".b],
  [3, "main".b],
  [4, [1, 0, 1, 0].pack("v4")],
  [4, [3, 0, 3, 0].pack("v4")],
  [6, [3, 1, 64].pack("v2Q<")],
  [7, [0x0001_0001].pack("V")]
]
vectors << records_vector("core.start-config.records", config_records)

window_config_records = [
  [1, [1].pack("C")],
  [2, [2].pack("C")],
  [4, [2].pack("C")],
  [6, [0x1020_30ff].pack("V")]
]
window_config_payload = window_config_records.map { |item| record(item[0], item[1]) }.join
vectors << records_vector("window.configure.records", window_config_records)
vectors << message_vector("window.configure", 2, 1, 0, window_config_payload)

window_snapshot_records = [
  [1, [1].pack("Q<")],
  [2, [320, 240].pack("V2")],
  [3, [640, 480].pack("V2")],
  [4, [2, 1].pack("V2")],
  [5, [0, 24, 0, 12].pack("V4")],
  [6, [0, 20, 0, 10].pack("V4")],
  [7, [2].pack("C")],
  [8, [1].pack("C")]
]
window_snapshot = window_snapshot_records.map { |item| record(item[0], item[1]) }.join
vectors << records_vector("window.snapshot.records", window_snapshot_records)
vectors << message_vector("window.metrics-changed", 2, 0x8001, 0, window_snapshot)
vectors << message_vector("window.get-snapshot.result", 2, 2, 51,
                          [0].pack("l<") + window_snapshot)
vectors << message_vector("window.back-requested", 2, 0x8002, 0, "".b)

screen_create = [1, 0, 13].pack("CCv") + [1, 0, 0, 1].pack("V3C")
canvas_create = [1, 0, 13].pack("CCv") + [2, 1, 0, 11].pack("V3C")
canvas_rect = [1, 0, 28, 10, 20, 40, 30, 0xff80_40ff, 3, 0, 0].pack("C2vl<2V3v2V")
vectors << message_vector("ui.begin.reset", 3, 1, 0, [1, 1].pack("VC"))
vectors << message_vector("ui.batch.create-screen", 3, 2, 0,
                          screen_create + canvas_create)
vectors << message_vector("ui.commit", 3, 3, 0, [1].pack("V"))
vectors << message_vector("ui.canvas.begin", 3, 5, 0, [2, 1, 0].pack("V2C"))
vectors << message_vector("ui.canvas.append", 3, 6, 0,
                          [2, 1].pack("V2") + canvas_rect)
vectors << message_vector("ui.canvas.present", 3, 7, 0, [2, 1, 0].pack("V2C"))
vectors << message_vector("ui.event", 3, 0x8001, 0,
                          [12, 1, 0, 0].pack("Vv2l<"))
vectors << message_vector("ui.pointer", 3, 0x8002, 0,
                          [2, 0, 1, 1, 120, 80, 1_234_567].pack("VC2vl<2Q<"))
vectors << message_vector("clock.set-period", 4, 1, 0, [33].pack("v"))
vectors << message_vector("clock.tick", 4, 0x8001, 0,
                          [1_234_567].pack("Q<"))

fs_open_records = [
  [1, "notes/today.txt".b],
  [3, [1 | 2 | 4].pack("V")]
]
fs_open_payload = fs_open_records.map { |item| record(item[0], item[1]) }.join
vectors << records_vector("fs.open.records", fs_open_records)
vectors << message_vector("fs.open", 5, 1, 71, fs_open_payload)
vectors << message_vector("fs.open.result", 5, 1, 71,
                          [0, 0x0001_0001].pack("l<V"))
vectors << message_vector("fs.read-directory", 5, 7, 72,
                          [0x0001_0002].pack("V"))

storage_records = [[1, "counter.total".b], [2, [7].pack("V")]]
storage_payload = storage_records.map { |item| record(item[0], item[1]) }.join
vectors << records_vector("storage.set.records", storage_records)
vectors << message_vector("storage.set", 6, 2, 73, storage_payload)
vectors << message_vector("storage.get", 6, 1, 74,
                          record(1, "counter.total".b))
vectors << message_vector("storage.get.result", 6, 1, 74,
                          [0].pack("l<") + record(2, [7].pack("V")))

ipc_records = [[1, "example.echo".b], [2, "hi".b]]
ipc_payload = ipc_records.map { |item| record(item[0], item[1]) }.join
vectors << records_vector("ipc.call.records", ipc_records)
vectors << message_vector("ipc.call", 7, 1, 75, ipc_payload)
vectors << message_vector("ipc.call.result", 7, 1, 75,
                          [0, 91].pack("l<V"))
vectors << message_vector("ipc.request", 7, 0x8001, 91, ipc_payload)
vectors << message_vector("ipc.result", 7, 0x8002, 91,
                          [0].pack("l<") + record(3, "ok".b))

sensor_subscribe_records = [
  [1, [1].pack("v")],
  [2, [500].pack("V")],
  [3, [0x0001_0003].pack("V")]
]
sensor_subscribe_payload = sensor_subscribe_records.map { |item| record(item[0], item[1]) }.join
vectors << records_vector("sensor.subscribe.records", sensor_subscribe_records)
vectors << message_vector("sensor.subscribe", 8, 2, 76, sensor_subscribe_payload)
sensor_descriptor = [
  [1, [1].pack("v")],
  [2, "ambient.temperature".b],
  [3, [1].pack("v")],
  [4, [1].pack("C")],
  [5, [100].pack("V")],
  [6, [10_000].pack("V")]
].map { |item| record(item[0], item[1]) }.join
vectors << message_vector("sensor.list.result", 8, 1, 76,
                          [0].pack("l<") + record(1, sensor_descriptor))
sensor_sample = [
  [4, [0x0001_0004].pack("V")],
  [2, [1_234_567].pack("Q<")],
  [3, [1].pack("v")],
  [4, [21_500].pack("l<")]
].map { |item| record(item[0], item[1]) }.join
vectors << message_vector("sensor.sample", 8, 0x8001, 0, sensor_sample)

net_fetch_records = [
  [1, "https://example.test/hello".b],
  [2, [1].pack("v")],
  [3, [0x0001_0003].pack("V")],
  [4, [64].pack("V")]
]
net_fetch_payload = net_fetch_records.map { |item| record(item[0], item[1]) }.join
vectors << records_vector("net.fetch.records", net_fetch_records)
vectors << message_vector("net.fetch", 9, 1, 77, net_fetch_payload)
net_fetch_result = [0].pack("l<") + record(5, [200].pack("v")) +
                   record(6, "text/plain".b) + record(7, [0x0001_0004].pack("V"))
vectors << message_vector("net.fetch.result", 9, 1, 77, net_fetch_result)

net_header = record(1, "content-type".b) + record(2, "application/json".b)
net_http_records = [
  [1, "https://example.test?mode=post".b],
  [2, [3].pack("v")],
  [3, [0x0001_0003].pack("V")],
  [4, [1024].pack("V")],
  [8, [2500].pack("V")],
  [9, net_header],
  [10, "{}".b],
  [11, "etag".b]
]
net_http_payload = net_http_records.map { |item| record(item[0], item[1]) }.join
vectors << records_vector("net.http-request.records", net_http_records)
vectors << message_vector("net.http-request", 9, 2, 78, net_http_payload)
net_response_header = record(1, "etag".b) + record(2, '"test"'.b)
net_http_result = [0].pack("l<") + record(5, [200].pack("v")) +
                  record(6, "application/json".b) +
                  record(7, [0x0001_0004].pack("V")) +
                  record(9, net_response_header) + record(12, [11].pack("Q<")) +
                  record(13, [3].pack("V"))
vectors << message_vector("net.http-request.result", 9, 2, 78, net_http_result)

audio_open = record(1, [0x0001_0002].pack("V")) + record(2, [1].pack("v"))
vectors << message_vector("audio.open-session", 10, 1, 81, audio_open)
audio_graph = record(3, [0x0001_0003].pack("V")) + record(2, [-256].pack("s<")) +
              record(3, [1500, 256, 256].pack("v s< v")) + record(4, [1].pack("v"))
vectors << message_vector("audio.commit-graph", 10, 2, 82, audio_graph)

work_records = [
  [1, "sync.job".b],
  [2, [1_000].pack("V")],
  [3, [5_000].pack("V")],
  [5, "abc".b],
  [6, [2_000].pack("V")],
  [7, [3].pack("C")]
]
work_payload = work_records.map { |item| record(item[0], item[1]) }.join
vectors << records_vector("work.enqueue.records", work_records)
vectors << message_vector("work.enqueue", 13, 1, 79, work_payload)
vectors << message_vector("work.enqueue.result", 13, 1, 79,
                          [0].pack("l<") +
                          record(4, [0x0001_0001].pack("V")) +
                          record(8, [5_000].pack("V")))
work_cancel = record(4, [0x0001_0001].pack("V"))
vectors << message_vector("work.cancel", 13, 2, 80, work_cancel)
work_complete = work_cancel + record(9, [1].pack("C"))
vectors << message_vector("work.complete", 13, 3, 81, work_complete)

permission_records = [[1, "net.client".b], [2, "api".b]]
permission_payload = permission_records.map { |item| record(item[0], item[1]) }.join
vectors << records_vector("permission.acquire.records", permission_records)
vectors << message_vector("permission.acquire", 11, 2, 81, permission_payload)
vectors << message_vector("permission.acquire.result", 11, 2, 81,
                          [0, 0x0001_0003].pack("l<V"))
vectors << message_vector("permission.revoked", 11, 0x8001, 0, permission_payload)

device_records = [[1, [1].pack("v")], [2, [0xa001_0002].pack("V")]]
device_payload = device_records.map { |item| record(item[0], item[1]) }.join
vectors << records_vector("device.get-mac.records", device_records)
vectors << message_vector("device.get-mac", 15, 1, 82, device_payload)
device_result = [0].pack("l<") + record(1, [1].pack("v")) +
                record(2, [0x24, 0x6f, 0x28, 0x70, 0x14, 0x01].pack("C*")) +
                record(3, [1].pack("V"))
vectors << message_vector("device.get-mac.result", 15, 1, 82, device_result)

document = {
  "schema" => "pxa-core-golden-0.1",
  "generated_by" => "tools/generate_golden.rb",
  "vectors" => vectors
}

FileUtils.mkdir_p(File.dirname(OUTPUT))
File.write(OUTPUT, JSON.pretty_generate(document) + "\n")
puts "Wrote #{OUTPUT}"
