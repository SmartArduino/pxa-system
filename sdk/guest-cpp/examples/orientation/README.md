# 应用方向切换

这个示例在运行中切换横竖屏偏好、四个角度，以及跟随系统和锁定当前方向。
指标行括号中的尺寸为原生尺寸，前面的尺寸为当前逻辑视口。
使用 Window 0.5 Host；构建、运行方式与同目录其他 C++ 示例相同。

```cpp
// 可在应用启动或设置按钮中调用，不需要重启应用。
auto result = ctx.window().request_orientation(pxa::OrientationPreference::landscape);
// 显式角度相对于设备原生方向，以顺时针图形旋转表示。
auto rotated = ctx.window().set_rotation(pxa::DisplayRotation::deg90);
auto metrics = co_await ctx.window().snapshot(true);
```

检查返回值以及实际的 `on_window_changed(const WindowMetrics&)` 回调。
`supported_rotations` 的第 N 位代表是否支持 N×90°；`rotation` 为 0…3，
`native_width/native_height` 为原生尺寸。原生方向可以横也可以竖，不能把
0° 当成竖屏。当前高度大于宽度是竖屏，宽度大于高度是横屏，相等是方屏。
方屏请求横竖屏偏好时保留角度；用显式角度改变其阅读方向。

保留 `Environment` 的初始化和事件更新，并使用 `SafeArea`、dp 与弹性布局，
让尺寸、圆角和安全区域变化自动生效。Home 恢复系统方向，回到应用时重新
应用它的请求；退出后不影响下一应用。`locked` 捕获请求时的角度。
当前设备 Host 的系统方向为原生方向，没有由重力自动旋转。

陀螺仪、加速度和磁力计使用原生 Android 坐标（X 向右、Y 向上、Z 朝外），
不随 UI 改轴。需要重力操作跟随画面时，将三个分量放入 `float device[3]`，
调用 `pxa_display_motion_to_screen(device, metrics.rotation, screen)`；只做一次。
该函数转换向量，不能直接转换欧拉角。

`tests/simulator.py` 在原生竖屏、横屏和方屏的真实模拟器里点击控件、读取
窗口事件并截图，接受 `--simulator --package --publisher-key --output` 参数。
运行输出放在仓库忽略的本地产物目录。
