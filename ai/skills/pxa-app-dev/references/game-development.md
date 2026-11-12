# Build playable PXA games with bounded frame work

Use for new games, substantial game changes and performance work. Pixel-grid
fonts/scaling have additional guidance in `pixel-games.md`. Confirm real symbols,
features and service versions with the selected SDK's `context` and headers;
these choices do not authorize SDK, Host or firmware edits.

## Get to the complete gameplay loop early

Inspect the existing project, target device, supported input and relevant SDK
examples once. For an adaptation, pin the upstream revision, examine the actual
rules/data and representative screens, then map source assets and their layers,
scale, transparency and license. Preserve required notices in the app. Keep
app-specific dependencies/assets separate from the system's license boundary.
Do not substitute invented mechanics, icons or copy when fidelity is requested.

Implement a small complete loop with real input, update, rendering, feedback,
win/lose or turn completion, and restart/continue. Use it as an implementation
milestone; continue to the user's full requested scope. Separate rules/state,
rendering/layout, input and assets/save handling enough to change them without
rewriting the whole game. Reuse compatible existing app helpers after checking
their contracts; do not copy a huge engine or add abstraction for hypothetical use.

Decide the drawing, font and audio path during that first loop so content growth
does not force a late renderer rewrite. Convert assets reproducibly offline and
reuse the generated output. Keep asset import revision/hash, layers and font
coverage explicit. Do not redownload/reconvert unchanged resources each iteration.
Validate the changed rule with a deterministic seed/state or short input journey,
run one representative screen during iteration, and the required screen matrix
before delivery. Reuse watch/incremental builds and checked `--no-build`; build
additional CPU targets when needed for delivery/device testing. Avoid rebuilding
every target or running a full simulation suite after every layout adjustment.

## Select a rendering path from the workload

For tile/sprite-heavy games, evaluate the SDK's GameRender batching before writing
large per-pixel Wasm loops. Batch compatible draws, reuse texture handles/atlases,
cull offscreen objects and keep stable terrain/minimap layers cached or dirty.
A bounded RGB565 Surface is still useful for suitable custom rasterization;
ordinary menus can use retained UI. Choose with a representative measurement,
visual correctness and available memory, not the API's name.

If a single game frame can contain world, HUD and glyphs, evaluate that path to
reduce cross-layer composition. GameRender and Surface may request direct
scanout; actual eligibility depends on the Host, format, geometry and overlays.
Check actual capture/presentation metadata. Handle normal system dialogs and
fallback composition correctly. Never disable necessary system UI to make a
performance claim, or equate using LVGL, GameRender or a request flag with FPS.

## Make text ready when it is first needed

Measure cold-start time to the first complete readable screen separately from
warm scrolling. For known UI text, consider a licensed, subsetted glyph atlas
generated at build time, including digits/punctuation and the actual fixed sizes.
For dynamic text, cache system glyphs with all relevant font/size keys and retain
required character coverage/fallback. Do not ship a fixed subset as complete
support for arbitrary player names, translations or user text.

When runtime glyph fetches are needed, prioritize visible text, bound in-flight
requests to actual reply capacity, replenish on completion, reuse sizes, and
coalesce redraws. Avoid a separate long frame/wait cycle between each request
batch or waiting for an entire screen before showing ready content. Prewarm only
the likely next content within a measured memory/startup budget. System glyph
fetching is not automatically faster than cached retained text.

## Keep simulation, presentation and sound independent

Use monotonic elapsed time and a bounded fixed-step accumulator where suitable.
Cap catch-up after suspend; reset held input on cancel, background and focus loss.
Budget the entire frame: update + render + submit + wait. Schedule toward the
next deadline rather than adding a full frame delay after drawing. Respect
presentation/backpressure and avoid busy loops or an unbounded queue of stale
frames. Idle and turn-based screens can redraw on state changes; camera dragging
needs responsive updates even when the world simulation is unchanged.

Keep allocations, asset decoding, filesystem writes and avoidable synchronous
calls out of hot paths. Cache pathfinding/reachability, city/resource outputs,
power connectivity and minimaps according to their state dependencies. Invalidate
on the relevant topology/rule/state change; camera movement alone should not
recalculate world rules. Bound entity scans and AI work; distribute expensive
work when semantics permit. Verify cache invalidation and gameplay correctness.

For short effects, prefer the pinned SDK's supported prepared sounds/effects and
Host-controlled voices/tracks. Stream longer music through the supported decoder
path. Do not make playback depend on sending one small PCM chunk per rendered
frame: slow drawing/AI then starves audio. If PCM synthesis is required, schedule
it from audio demand/clock with bounded buffering. Account for preload bytes,
overlapping voices, permission/session type and playback events; a successful
play request alone does not prove readiness or uninterrupted speaker output.

Save asynchronously at meaningful transitions/checkpoints, coalesce rapid changes
and preserve old saves on read/migration failure. Do not write Flash each frame.
Budget Wasm/AOT, textures, frame/depth/double buffers, glyphs, audio and temporary
peaks together. Give caches bounds and eviction; increasing every budget hides
neither allocation failure nor a frame bottleneck.

## Measure the experience that can regress

Use the same seed/save, camera path, entity density, resolution, DPI and input
sequence before/after. Include cold text, drag/zoom, a busy gameplay/AI phase,
repeated audio and pause/resume when relevant. Record frame-time distribution or
slow frames as well as average submitted FPS. Separate update, drawing commands,
Host rasterization, queue/presentation, display transfer and memory peaks.
Change the measured bottleneck, verify output/rules, then compare. Keep useful
fallbacks; avoid retaining two complete renderers after the comparison without
a concrete need. See pxa-debug's measurement reference for reporting limits.

Inspect the post-tutorial gameplay view for HUD obstruction, clear selection,
touch target visibility and supported pinch/drag transforms. For touch games with
drag/pinch, test transitions so a pinch cannot accidentally place, move or activate
something. Use the same transform for drawing and hit testing. Reuse help and
conditional teaching from `interface-content.md` rather than permanent controls
paragraphs. Report measured scope honestly: submitted FPS, simulator throughput,
first-frame preparation and physical display/audio latency are different results.
