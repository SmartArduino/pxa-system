"""Small, runnable app recipes with actual user journeys, not SDK mutations."""
from __future__ import annotations

import json
from pathlib import Path
import re
import shlex

from .common import KIT, ToolError, atomic_json
from .projects import initialize
from . import scope

RECIPES = {'timer': '单调时钟计时、开始/暂停/重置、专注/短休、检查点存档',
           'counter': '加减/重置、触摸与手柄、合并异步存档',
           'reaction': '30秒点中10个目标、触摸与手柄、暂停/继续、胜负与重开'}


def catalog():
    return {'minimum_devkit': '0.10.0', 'recipes': RECIPES,
            'other_starters': 'Use version-matched SDK virtual-list, device-sensor and game-input examples via context.'}


def expect(name, text, *, button=False, visible=True):
    return {'name': name, 'action': 'wait-for', 'selector': {'type': 'button' if button else 'text', 'text': text},
            'expected': {'visible': visible, 'count': 1, **({'enabled': True} if button else {})}, 'timeout_ms': 3000}


def tap(name, text):
    return {'name': name, 'action': 'tap-node', 'selector': {'type': 'button', 'text': text}}


def create(env, project, recipe, *, app_id=None, title=None, minutes=None, break_minutes=None):
    if recipe not in RECIPES:
        raise ToolError('Choose ' + ', '.join(RECIPES) + '; recipes lists the supported starters')
    release = re.fullmatch(r'(\d+)\.(\d+)\.(\d+)(?:-[A-Za-z0-9.-]+)?', env.config.get('sdk_version', ''))
    if not release or tuple(map(int, release.groups())) < (0, 10, 0):
        raise ToolError('These recipes require DevKit 0.10.0+. Preserve the selected SDK; use explicit workspace migration if desired')
    if recipe != 'timer' and (minutes is not None or break_minutes is not None):
        raise ToolError('Minute settings apply to the timer recipe only')
    minutes = 25 if minutes is None else minutes
    break_minutes = 5 if break_minutes is None else break_minutes
    if any(type(value) is not int or not 1 <= value <= 1440 for value in (minutes, break_minutes)):
        raise ToolError('Timer minutes must be in 1..1440')
    title = title if title is not None else {'timer': '番茄钟', 'counter': '计数器', 'reaction': '反应挑战'}[recipe]
    if not isinstance(title, str) or not title.strip() or len(title.encode()) > 80 or any(ord(c) < 32 or ord(c) == 127 for c in title):
        raise ToolError('Choose a nonempty, single-line title of at most 80 UTF-8 bytes')
    template = KIT / 'templates/recipes' / recipe / 'main.cpp'
    source = template.read_text().replace('@PXA_MINUTES@', str(minutes)).replace(
        '@PXA_BREAK_MINUTES@', str(break_minutes)).replace('@PXA_TITLE@', json.dumps(title, ensure_ascii=False))
    # initialize performs scope/empty-directory/ID checks before writing anything.
    created = initialize(env, project, app_id, 'cpp')
    path = env.project(project)
    # init belongs to the selected, immutable SDK. Reuse its profile set so
    # a newer Agent Kit cannot emit scenarios referring to unavailable boards.
    basic = path / 'tests/basic.json'
    profiles = tuple(json.loads(basic.read_text()))
    (path / 'main.cpp').write_text(source)
    metadata = json.loads((path / 'package.json').read_text())
    metadata.update(name=title, description=RECIPES[recipe])
    metadata['components'][0]['services'] = (['ui', 'window', 'clock', 'log'] if recipe == 'reaction'
                                             else ['ui', 'window', 'clock', 'storage', 'log'])
    atomic_json(path / 'package.json', metadata)
    tests = path / 'tests'
    tests.mkdir(exist_ok=True)
    # Replace the init template's unrelated counter scenario with this journey.
    basic.unlink()
    if recipe == 'reaction':
        return reaction_files(env.workspace, path, project, profiles, title, created)
    if recipe == 'counter':
        steps = [expect('ready-to-add', '+1', button=True), expect('initial-zero', '0'), tap('add', '+1'), expect('count-one', '1'),
                 expect('saved-one', '已保存', visible=False), {'name': 'reopen', 'action': 'relaunch'},
                 expect('restored-one', '1'), tap('subtract', '−1'), expect('count-zero', '0'),
                 tap('reset', '重置'), expect('reset-zero', '0')]
        complete = None
    else:
        steps = [expect('ready', '就绪 · 专注'), tap('start', '开始'), expect('running', '专注中'),
                 tap('pause', '暂停'), expect('paused', '已暂停'), tap('reset', '重置'),
                 expect('reset-ready', '就绪 · 专注'), tap('rest', '短休'), expect('rest-ready', '就绪 · 短休'),
                 expect('rest-saved', '已保存', visible=False), {'name': 'reopen', 'action': 'relaunch'},
                 expect('rest-restored', '就绪 · 短休'),
                 tap('focus', '专注'), expect('focus-ready', '就绪 · 专注')]
        complete = [expect('ready', '就绪 · 专注'), tap('start', '开始'),
                    {'name': 'complete', 'action': 'wait-for', 'selector': {'type': 'text', 'text': '已完成'},
                     'expected': {'visible': True, 'count': 1}, 'timeout_ms': 15000}, expect('zero', '0:00'),
                    {'name': 'completed-screen', 'action': 'screenshot'}]
    steps.append({'name': 'journey-screen', 'action': 'screenshot'})
    atomic_json(tests / 'journey.json', {profile: steps for profile in profiles})
    if complete:
        atomic_json(tests / 'complete.json', {profile: complete for profile in profiles})
    project_command = shlex.quote(str(project))
    journey_command = shlex.quote(str(Path(project) / 'tests/journey.json'))
    complete_command = shlex.quote(str(Path(project) / 'tests/complete.json'))
    text = f'''# {title}

由 `{recipe}` 配方生成，后续在同一项目内修改，保留 app ID、签名身份和 SDK 锁。
触摸直接点按钮；系统返回由 Host 处理，不绑定 B 来重置数据。
背景铺满，内容使用动态 SafeArea；侧边返回区不会整体推开页面。

```sh
.pxa/pxa run {project_command} --profile pai-touch --watch
.pxa/pxa verify {project_command} --scenario {journey_command} --require-ui
```

先停止 watch，再验收。场景使用真实节点文字，不依赖固定坐标；检查每张原始截图。
验收等待保存成功后重新打开应用，验证同一份隔离测试存档；不清除日常存档。
界面围绕实际数据与操作，不自动添加标语、装饰性副标题或常驻操作说明。
若后续功能需要教学，设置完成/关闭条件、持久化状态和帮助入口，并检查教学结束后的正常界面。
存储读取失败时只保留本次会话数据，不用默认值覆盖旧存档。快速连续修改合并为
一个异步写入任务；正常自动保存不常驻提示，读取或保存失败仍显示必要反馈，不在渲染帧或每秒 tick 上写 Flash。
设备交付前核对分辨率、物理控件、传感器及 PXADB，再运行对应目标和真实设备测试。
'''
    if recipe == 'timer':
        text += f'''
专注 {minutes} 分钟、短休 {break_minutes} 分钟。A 开始/暂停，X 重置，Y 切换模式。
按单调时间计算 deadline，避免按 tick 数量累加误差；暂停关闭周期事件，运行时每秒更新。
收到后台生命周期事件时立即冻结最后显示的剩余时间，恢复前台不会偷偷继续计时。
部分 Host 的 Home 会直接关闭应用；重新打开只恢复最近已保存的检查点，不能保证
保存关闭瞬间的进度。存档是最近一次操作/完成的检查点，中途进度按暂停恢复，满时长
按就绪恢复；不是关机后仍运行的闹钟。后台/退出前最后一个异步写入
尚未完成时，恢复上一个已确认的检查点。改变默认时长时应提供存档迁移；不匹配的
存档保持只读，普通「重置」只重置当前会话，不会覆盖旧数据。

加速完成态验收使用独立存档键，verify 自身也隔离日常存档：

```sh
.pxa/pxa verify {project_command} --define PXA_RECIPE_TIMER_SECONDS=6 --scenario {complete_command} --require-ui
```

最终交付重建不带测试宏的包；该宏不改变 Host 时钟或其他应用。
'''
    else:
        text += '\nA/方向右加一，方向左减一，X 重置。数值范围为 −999999..999999。\n'
    (path / 'README.md').write_text(text)
    scope.enforce(env.workspace)
    return {**created, 'recipe': recipe, 'title': title,
            'scenario': str(tests / 'journey.json'), 'runtime_verified': False,
            'next': 'Run verify with the generated journey; inspect screenshots and test target hardware.'}


def reaction_files(workspace, path, project, profiles, title, created):
    steps = [expect('ready', '准备'), tap('start', '开始'), expect('running', '进行中'),
             tap('first-hit', '点我'), expect('first-point', '1 / 10'), tap('pause', '暂停'),
             expect('paused', '已暂停'), tap('resume', '继续'), expect('resumed', '进行中')]
    for score in range(2, 11):
        steps.extend([tap(f'hit-{score}', '点我'), expect(f'score-{score}', f'{score} / 10')])
    steps.extend([expect('won', '完成'), {'name': 'won-screen', 'action': 'screenshot'},
                  tap('restart', '重开'), expect('reset', '准备'), expect('zero', '0 / 10'),
                  tap('play-again', '开始'), expect('playing-again', '进行中'),
                  {'name': 'playing-screen', 'action': 'screenshot'}])
    timeout = [expect('ready', '准备'), tap('start', '开始'),
               {'name': 'timeout', 'action': 'wait-for', 'selector': {'type': 'text', 'text': '时间到'},
                'expected': {'visible': True, 'count': 1}, 'timeout_ms': 5000},
               expect('zero-time', '0秒'), {'name': 'lost-screen', 'action': 'screenshot'}]
    tests = path / 'tests'
    atomic_json(tests / 'journey.json', {profile: steps for profile in profiles})
    atomic_json(tests / 'timeout.json', {profile: timeout for profile in profiles})
    def controller(name, buttons):
        return {'name': name, 'action': 'controller', 'buttons': buttons, 'ms': 100}
    controls = [expect('ready', '准备'), controller('start', 16), expect('running', '进行中'),
                controller('focus-right', 8), {'name': 'focus-screen', 'action': 'screenshot'},
                controller('pause', 64), expect('paused', '已暂停'), controller('resume', 16),
                expect('resumed', '进行中'), controller('reset', 256), expect('reset-ready', '准备'),
                expect('reset-score', '0 / 10'), {'name': 'reset-screen', 'action': 'screenshot'}]
    atomic_json(tests / 'controller.json', {profile: controls for profile in profiles})
    project_command = shlex.quote(str(project))
    (path / 'README.md').write_text(f'''# {title}

30 秒内点中 10 个「点我」目标即可完成，点错或超时结束。目标不会连续停在同一格。
点击「开始」进入游戏；可暂停、继续或重开。切到后台会暂停，返回后手动继续。
方向键移动「>」光标，A 选择或开始，START 暂停/继续，X 重开；B 保留系统返回。
背景铺满，内容遵守动态 SafeArea。游戏只在操作和时钟事件后更新，没有忙循环；
按单调时间判断超时，每次选择也检查实际时间，避免最后一次 tick 之间错误计分。
本配方不写存档、不申请音频或网络权限；重新启动应用会回到准备画面。

```sh
.pxa/pxa run {project_command} --profile pai-touch --watch
.pxa/pxa verify {project_command} --scenario {shlex.quote(str(Path(project) / 'tests/journey.json'))} --require-ui
```

先停止 watch，再验收。场景实际完成十次目标选择、暂停/继续、胜利和重开；
检查每张原始截图的文字、目标、焦点及安全区域。继续在同一项目扩展玩法，保留
应用 ID、签名身份和 SDK 锁。模拟器不证明实体按键接线、LCD 帧率或触摸延迟。
手柄启动、光标、暂停/继续与重开使用 `--scenario {shlex.quote(str(Path(project) / 'tests/controller.json'))}`。

超时分支使用测试宏和隔离模拟器状态，不修改 Host 时钟：

```sh
.pxa/pxa verify {project_command} --define PXA_RECIPE_REACTION_SECONDS=1 --scenario {shlex.quote(str(Path(project) / 'tests/timeout.json'))} --require-ui
```

交付时重建不带测试宏的 30 秒版本。不要把加速包交付为正常玩法。
''')
    scope.enforce(workspace)
    return {**created, 'recipe': 'reaction', 'title': title, 'scenario': str(tests / 'journey.json'),
            'runtime_verified': False, 'next': 'Verify the real gameplay journey, timeout and controller path; inspect captures.'}
