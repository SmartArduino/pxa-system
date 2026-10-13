# Scripted input

The helper accepts a JSON object keyed by screen profile. Missing profiles receive only a launch/screenshot check. Unknown profiles, malformed actions and out-of-screen coordinates are rejected before building. Every step has a safe filename `name`; supported actions are `tap`, `swipe`, `wait` and `screenshot`.

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
