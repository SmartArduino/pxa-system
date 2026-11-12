# Scripted input

The helper accepts a JSON object keyed by screen profile. Missing profiles receive only a launch/screenshot check. Unknown profiles, malformed actions and out-of-screen coordinates are rejected before building. Every step has a safe filename `name`; supported actions are `tap`, `swipe`, `controller`, `cancel`, `wait`, `screenshot`, `tap-node`, `expect`, `wait-for` and `relaunch`.

For the unchanged Hello template on pai-touch:

```json
{
  "pai-touch": [
    {"name": "initial-zero", "action": "expect", "selector": {"type":"text", "text":"0"}, "expected": {}},
    {"name": "increment", "action": "tap-node", "selector": {"type":"button", "text":"+1"}},
    {"name": "after-input", "action": "wait-for", "selector": {"type":"text", "text":"1"}, "expected": {}, "timeout_ms": 3000},
    {"name": "count-one", "action": "screenshot"}
  ]
}
```

Run with `--profiles pai-touch --scenario hello.json`. Inspect `pai-touch/before.png` and `pai-touch/count-one.png`; the displayed count must change from 0 to 1. Existing simulator saves can affect other apps' initial screens; create a separate project instance for a clean starting state.

A swipe adds `x2`, `y2` and `ms` (1–5000); a wait permits 0–5000 ms. Example: `{"name":"turn","action":"swipe","x":220,"y":120,"x2":180,"y2":110,"ms":200}`. No shell commands, file paths or executable expressions are accepted in a scenario. Command transcripts and screenshots are evidence; the agent still judges the actual expected behavior.

Controller example: `{"name":"confirm","action":"controller","buttons":16,"ms":100}`. Use the current C/C++ controller enum for masks. Holds are 1–3000 ms and masks 0–4095. The simulator adapter sends phased SDL keys through the existing UI control socket and releases all controller keys even on failure; it does not modify the installed SDK. `{"name":"release","action":"cancel"}` also releases controller and touch input. Physical-device MCP input uses PXADB with INPUT CANCEL. Input injection is not a mechanical press.

## Teaching and saved-state journeys

`{"name":"reopen","action":"relaunch"}` is available in `verify --scenario`
(including MCP `pxa_verify`), not live `pxa_interact` batches. It releases input,
stops and runs only the verified package in the same owned simulator and isolated
profile state. It does not rebuild, reinstall or clear data, restart the Host,
or affect daily simulator saves. Package-control support is required; transport
errors or changed/ambiguous identity fail the check.

Before relaunch, wait for a real persistence-completion signal from the app.
Then assert the restored state: an acknowledged close in memory alone does not
prove a tutorial flag reached storage. For teaching, exercise the actual trigger,
completion/close, absence during normal play, persisted absence after relaunch,
and Help/Controls reopening. Capture the normal gameplay screen as well as the
teaching screen. Use actual app labels; Surface/Canvas text needs interaction and
original image review because absence from the retained tree is insufficient.
This checks app-restart persistence, not a cold Host boot or power-loss durability.

## Semantic UI checks

These actions use the Host's real retained tree, not OCR, source scraping or a
Guest test flag. `selector` accepts positive `id` / `surface`, `type` and exact
UTF-8 `text`. A typed button can match its descendant text; an untyped text selector
matches actual text nodes. `tap-node` resolves their nearest clickable ancestor and
injects a real pointer at its clipped rectangle center. Hidden/disabled targets,
zero-sized regions and ambiguous matches fail. System overlays can still intercept
input; assert the resulting state instead of treating injection as proof.

`expected` accepts `visible` (default true), `enabled`, integer `value`, `count`
(default one) and `within: [x,y,width,height]`. The count is the number of selected
nodes satisfying all conditions: use `count:0` for absence of visible matches,
`visible:false` for a hidden retained node. `within` checks the full node bounds,
not just its clipped piece. Value zero is valid. `wait-for` adds `timeout_ms`
(0–60000) and repeatedly checks the same conditions until success or failure;
transport/malformed-data errors remain failures. Failed steps record diagnostics
and return nonzero. Do not choose arbitrary duplicate labels; refine by type or ID.
Password values are redacted and truncated text is not used for exact matching.

Manual CLI equivalents:

```sh
pxa device ui inspect --simulator PROFILE@INSTANCE
pxa device ui tap --type button --text '+1' --simulator PROFILE@INSTANCE
pxa device ui wait-for --type text --text '1' --timeout-ms 3000 --simulator PROFILE@INSTANCE
pxa device ui expect --type progress --value 0 --simulator PROFILE@INSTANCE
```

MCP equivalents are `pxa_ui_inspect`, `pxa_tap_node` and `pxa_ui_expect`.
The same commands can use `--port` and `--baud` with firmware that provides
`UI INSPECT`. Unsupported older Hosts fail explicitly. Native tree exports are
bounded, omit system chrome, redact password input and report rectangular geometry;
they do not verify visual contrast, glyph rasterization or Surface-only content.

## Live MCP batches

After `pxa_run` reports its owned simulator selector, call `pxa_interact` with
the run's `job_id` and a `steps` array using the same action objects above.
A batch accepts 1–32 steps, unique lowercase names (letters, digits, hyphens;
start with a letter; at most 48 characters), at most 8 screenshots and
15 seconds of total waits/holds, including the maximum `wait-for` timeouts. The complete batch is validated against
the active profile before any input. It uses one SDK connection and releases
input on completion or execution failure; failed writes are never replayed.

Poll the returned batch job, then read `result.json` and inspect original
images using `pxa_view_image`. Paths returned at launch do not mean the files
already exist. Runtime success is not visual approval. Pause between batches
whenever the next action depends on what appeared. Other commands to the same
simulator are rejected until the batch finishes. The live batch tool only
targets this server's owned simulator, never physical USB devices.

`pxa_job_status` includes the completed `batch` result, so an extra file read
is optional. Check `passed`, `cancelled` and `batch.input_released`; a cancelled
job is never a passing test. Use `pxa_job_stop` with the batch job ID to cancel
without closing the simulator. It waits for the actual worker to release input;
if that worker does not respond, it stops only its owned simulator. Inspect the
current screen before issuing new steps, and never replay an entire cancelled
batch: earlier actions may already have taken effect.
