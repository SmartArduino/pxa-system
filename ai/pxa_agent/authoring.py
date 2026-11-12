"""Shared, compact app/game guidance for CLI, context and MCP entry points."""
from .common import KIT


AUTHORING_BRIEF = (
    "Design each app/game screen around its main user task. Default to useful content and clear controls; "
    "omit unsolicited slogans, decorative subtitles, repeated instructions and filler panels. "
    "Give teaching hints a trigger, dismissal/completion condition, persisted progress and a help/replay entry; "
    "match the actual input device. Preserve requested narrative, branding, accessibility and necessary feedback. "
    "For games, read pxa-app-dev's game-development reference: build a playable loop early, select rendering, "
    "text and audio paths from the pinned SDK, then measure cold start and representative interaction. "
    "Review the normal post-tutorial screen as well as first use and relaunch; build success is insufficient."
    " For store delivery, use store build for a mandatory separate Wasm package plus requested native targets, "
    "then publish-targets; reuse compilation caches. Write factual listing copy about the actual task or gameplay, "
    "without slogans, generic praise, unverified features or control tutorials."
)

STORE_LISTING_GUIDANCE = {
    'summary': 'One sentence describing the main task or core gameplay; aim for 20–60 Chinese characters or 8–25 English words, maximum 200 characters.',
    'app': 'State what the user can do and, when useful, the concrete result or intended use.',
    'game': 'State the core player action and goal, scoring or progression that actually exists.',
    'description': 'Optional: explain 1–3 distinctive, implemented features in short prose; add material device/input limits when needed.',
    'claims': 'Use observed or verified behavior. Do not invent offline use, saves, multiplayer, compatibility, performance or privacy guarantees.',
    'omit': 'Promotional slogans, generic praise, AI/PXA implementation credits, repeated titles, keyword piles and control tutorials.',
    'missing': 'Inspect the app and runtime, then draft store.json; ask only when facts cannot be determined. Never fabricate a placeholder summary.',
}


def authoring_context():
    return {'principles': AUTHORING_BRIEF, 'references': {
        'interface_content': str(KIT / 'skills/pxa-app-dev/references/interface-content.md'),
        'game_development': str(KIT / 'skills/pxa-app-dev/references/game-development.md'),
        'store_listing': str(KIT / 'skills/pxa-release/references/store-listing.md'),
    }}
