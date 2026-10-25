# Scripted input

The helper accepts a JSON object keyed by screen profile. Missing profiles receive only a launch/screenshot check. Unknown profiles, malformed actions and out-of-screen coordinates are rejected before building. Every step has a safe filename `name`; supported actions are `tap`, `swipe`, `controller`, `cancel`, `wait` and `screenshot`.

For the unchanged Hello template on pai-touch:

```json
{
  "pai-touch": [
    {"name": "increment", "action": "tap", "x": 148, "y": 140},
    {"name": "after-input", "action": "wait", "ms": 180},
    {"name": "count-one", "action": "screenshot"}
  ]
}
```

Run with `--profiles pai-touch --scenario hello.json`. Inspect `pai-touch/before.png` and `pai-touch/count-one.png`; the displayed count must change from 0 to 1. Existing simulator saves can affect other apps' initial screens; create a separate project instance for a clean starting state.

A swipe adds `x2`, `y2` and `ms` (1–5000); a wait permits 0–5000 ms. Example: `{"name":"turn","action":"swipe","x":220,"y":120,"x2":180,"y2":110,"ms":200}`. No shell commands, file paths or executable expressions are accepted in a scenario. Command transcripts and screenshots are evidence; the agent still judges the actual expected behavior.

Controller example: `{"name":"confirm","action":"controller","buttons":16,"ms":100}`. Use the current C/C++ controller enum for masks. Holds are 1–3000 ms and masks 0–4095. The simulator adapter sends phased SDL keys through the existing UI control socket and releases all controller keys even on failure; it does not modify the installed SDK. `{"name":"release","action":"cancel"}` also releases controller and touch input. Physical-device MCP input uses PXADB with INPUT CANCEL. Input injection is not a mechanical press.

## Live MCP batches

After `pxa_run` reports its owned simulator selector, call `pxa_interact` with
the run's `job_id` and a `steps` array using the same action objects above.
A batch accepts 1–32 steps, unique lowercase names (letters, digits, hyphens;
start with a letter; at most 48 characters), at most 8 screenshots and
15 seconds of total waits/holds. The complete batch is validated against
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
