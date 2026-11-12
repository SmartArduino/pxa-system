# Store listing copy

Write in the listing's language, using facts from the implementation and observed
runtime behavior. Inspect an existing listing before revising it and preserve
the user's chosen name and tone. Do not infer a feature from a proposed plan.

The summary should answer what this product does in one sentence. Aim for
20–60 Chinese characters or 8–25 English words; shorter is fine when clear.
The store's hard limit is 200 characters, not a target to fill.

- Apps: name the main user action and useful result or situation.
- Games: describe the core player action and actual goal, scoring or progression.
- Mention a distinguishing feature only when it exists and helps users choose.

For example, if those features are implemented: “记录每天的收支，按月份查看各类开销。”
or “移动挡板接住小球，击碎砖块后进入下一关。” Verify categories/monthly views
or level progression before using those claims.

Omit welcome messages, “开启旅程”, “精心打造”, generic praise such as “极致流畅”
or “强大易用”, repeated app names, implementation credits such as “AI 制作” or
“基于 PXA”, and piles of keywords. Preserve credits or branding explicitly
requested by the user. Do not put WASD/mouse instructions in the summary.

The longer description is optional. Use short prose to explain 1–3 useful,
implemented features or the concrete gameplay loop. Include device/input limits
or other restrictions when they affect someone's choice. Avoid repeating the
summary in a longer marketing paragraph. Keep controls in help/onboarding.

Do not invent saves, offline play, multiplayer, supported devices, content counts,
performance or privacy guarantees. A valid build or package signature is not
evidence for these claims. Required Wasm delivery does not by itself prove that
every hardware feature works in the browser. Say what was actually verified.

Save the copy in `store.json` (`name`, `summary`, optional `description`). Run
`store prepare` and resolve `listing_text.needs_text`. Missing facts should first
be recovered from the app and runtime; ask only if they remain unclear. Do not
replace missing copy with a generic “使用 PXA 创作的应用” summary, silently truncate
overlong text, or modify unspecified existing public fields.
