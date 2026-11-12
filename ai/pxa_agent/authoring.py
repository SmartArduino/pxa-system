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
)


def authoring_context():
    return {'principles': AUTHORING_BRIEF, 'references': {
        'interface_content': str(KIT / 'skills/pxa-app-dev/references/interface-content.md'),
        'game_development': str(KIT / 'skills/pxa-app-dev/references/game-development.md'),
    }}
