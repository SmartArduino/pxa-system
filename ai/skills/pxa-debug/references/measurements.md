# Measurements that support a conclusion

Use the same map/seed, camera, resolution, DPI, view distance, graphics options, simulation duration and input sequence before and after a change. Warm-up and loading are separate from steady running. Repeat enough times to distinguish noise from a regression; record ranges rather than a single favorable sample.

Break game work into update/physics, geometry, command encode/submit, Host rasterization, queue/backpressure and actual display. An asynchronous submission can finish while old frames remain queued. Software timestamps bound app response, not physical touch-to-photon or speaker onset; label unmeasured display/audio latency accordingly.

For new games and path changes, measure cold start to the first complete readable
screen separately from warm interaction. Include font/asset readiness, a repeatable
camera drag/zoom, dense gameplay/AI and audio overlap when relevant. Record slow
frames or a frame-time distribution as well as average submitted FPS. Compare
cached text with its cold fetch cost; record actual direct/composited presentation
metadata rather than inferring it from GameRender/Surface flags. A nonblocking
audio request or readiness event does not measure gaps at the speaker. Keep
profiling output in logs, with app-owned overlays disabled in normal delivery
unless requested and always disabled for store captures.

On a firmware with the `memory` diagnostic capability, `pxa device memory --port <port> --baud <baud>` returns JSON. Capture before start, steady running and after stop, with restart/initialization boundaries. Distinguish ESP SRAM/PSRAM allocator totals, Guest linear memory, executable mapping, frame/depth buffers and command queues. Some values overlap; do not sum them blindly. Historical allocator minima/high-water marks are not this run's true peak unless reset at a documented boundary.

The simulator bridge of DevKit 0.3.0 does not expose the ESP heap diagnostic. Record unavailable data instead of borrowing another workload's report. Screenshots do not measure refresh FPS. Avoid allocations, permanent caches or background tasks solely to make a benchmark look better.
