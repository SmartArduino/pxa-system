# App and game interface content

Apply these defaults to new screens and changed UI. Preserve the user's chosen
style, branding, story, learning content and accessibility requirements.

## Decide what belongs on a screen

Before implementation, identify the main task, persistent content/controls,
conditional feedback and help entry. Keep this as a short working decision;
routine choices do not require a separate design document or approval round.
For an existing app, follow its established language and interaction patterns.

Visible copy should identify content, name an action, communicate relevant state,
explain a consequence or help someone recover. Default to omitting unsolicited
welcome slogans, motivational taglines, decorative subtitles, repeated headings,
feature self-praise and instructions that restate an obvious button label.
For example, use a clear Start button without a permanent "Tap Start to begin"
caption. Put complete controls in Help rather than a footer on every game frame.

Start with the actual timer, list, board or game world. Do not automatically wrap
every screen in a title/description/card/footer presentation. Empty space does
not need explanatory copy or extra panels. Use layout, original artwork,
typography, animation and meaningful feedback to express the chosen personality.
For faithful adaptations, use the reference's terminology and visual hierarchy;
do not invent substitute slogans or decorative English labels.

Keep clear control labels, accessible names, useful empty-state actions, error
causes/recovery, important operation consequences and requested story/dialogue.
Judge relevance in context rather than applying a global word count, keyword
blacklist or "fewer words is always better" score. Improve unclear controls or
layout before adding another explanatory sentence.

## Teaching and help have a lifecycle

For each non-obvious mechanic, choose:

- Trigger: first relevant use, a newly unlocked mechanic, or a contextual target.
- Completion/dismissal: performing the action or explicitly closing the hint.
- Persistence: a stable per-mechanic key, saved at the transition using the app's
  existing asynchronous storage. Version a changed teaching step only when needed;
  do not replay all teaching after every app update or visit to a screen.
- Recovery: an accessible Help/Controls entry and, when useful, Replay tutorial.
- Input mode: actual touch, physical keys/controller or supported keyboard/mouse.
  Simulator key mappings belong in simulator help unless they are a supported
  gameplay input. Debug injection does not prove physical controls exist.

A contextual "Interact" prompt may recur while its target is actionable.
Long-term HUD content needs ongoing decision value. Idle time alone does not
prove confusion; assistance should remain dismissible. Do not add a tutorial
overlay or help button to a self-evident counter merely to satisfy this pattern.

On storage failure, retain this session's dismissal, keep help available and
handle the error without overwriting an unread old save. Keep teaching state
separate from world progress so a replay/reset of teaching cannot erase a game.

## Verify normal use, not only the first frame

Exercise first use, hint completion/closing, sustained normal use, relaunch with
the same test save, and reopening help. Check supported input changes and target
screen geometry. Inspect original runtime images for unnecessary copy, obscured
play area, clipping and touch affordances. List concrete issues and their effect;
command success or an aesthetic score does not establish a good interface.

Use real retained-node assertions where available. In a `verify --scenario`
journey, wait for the app's actual saved/completed state before `relaunch`, then
assert that teaching stays dismissed and Help still opens it. Example steps
for an app whose actual button is named "关闭提示":

```json
[
  {"name":"dismiss-hint","action":"tap-node","selector":{"type":"button","text":"关闭提示"}},
  {"name":"hint-gone","action":"expect","selector":{"type":"text","text":"拖动移动"},"expected":{"count":0}}
]
```

Adapt labels and persistence completion checks to the real app. Surface/Canvas
drawn text may have no semantic nodes: verify actual input and frames instead of
treating an empty tree as proof that no hint is visible. The scenario format is
documented in pxa-debug's `references/scenarios.md`.
