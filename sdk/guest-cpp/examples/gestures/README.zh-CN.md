# 点击与长按

[main.cpp](main.cpp) 只需组合 `on_click` / `on_long_press`，无需帧回调或协程。
轻点增加第一个计数；按住 500ms 增加第二个计数并更新按钮文字，松手不会再点击。
拖动超过 8 个逻辑像素或取消触摸不会触发动作。清单声明 `ui` 与 `clock`。

源码工作区构建：

```sh
bash tools/app.sh build gestures --source-root deps/pxa-system/sdk/guest-cpp/examples \
  --target simulator --aot-only --output local/gesture-example
```

独立 DevKit 可以直接对本示例目录执行 `pxa run`，须使用包含手势接口的 SDK。
真实模拟器回归脚本：

```sh
python3 deps/pxa-system/sdk/guest-cpp/examples/gestures/tests/simulator.py \
  --simulator build/simulator/pai-touch/pxsys_product_simulator \
  --package local/gesture-example/pxa-gestures \
  --publisher-key deps/pxa-system/apps/pxa/.dev-signing/publisher-public.der \
  --output local/gesture-example/results
```

脚本使用独立临时存储，检查按住期间的画面变化、单次触发、松手不穿透、点击、
拖出再移回仍取消和显式 CANCEL；输出截图与 JSON。其首帧时间包含触摸注入、Host
调度、显示和截图轮询开销，不能作为真机触摸延迟。
