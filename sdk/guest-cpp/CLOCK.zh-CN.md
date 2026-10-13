# Clock：计时、系统时间和时区

游戏步进使用 `ClockTick::timestamp_us` 或 `clock.now()` 的单调时间，不随校时或时区变化。显示日期/钟表使用 Clock 0.2 的 `wall_time()`：

```cpp
auto sample = co_await ctx.clock().wall_time();
if (!sample) co_return std::unexpected(sample.error());

using namespace std::chrono;
auto local = sample->local(); // local_time<microseconds>
auto date = year_month_day{floor<days>(local)};
auto time = hh_mm_ss{local - floor<days>(local)};
auto offset = sample->zone.utc_offset; // chrono::seconds，东八区为 +28800s
bool summer_time = sample->zone.daylight_saving;
```

只需要当前时区偏移时：

```cpp
auto zone = co_await ctx.clock().time_zone();
if (zone) use_offset(zone->utc_offset, zone->daylight_saving);
```

两者读取同一类固定 24 字节快照。返回值拥有自己的数据；没有字符串生命周期问题、动态容器或额外 Task/协程池槽位，只使用调用者已有的协程。未调用时没有默认缓存或后台定时器。UTC、单调时间锚点、本地偏移和夏令时来自一次 Host 采样，避免跨午夜、切换时区或夏令时边界时拼接不同时间点的数据。

`TimeZone` 表示**采样时刻生效的偏移**，支持半小时、45 分钟等偏移；它不包含 IANA 名称或历史/未来规则。不得用当前偏移推算其他季节的本地时间。`WallTime` 也不保证系统已经通过网络校时。Host 未提供 Clock 0.2、请求取消、资源不足或数据不合法时返回 `Result` 错误。

固件参考实现读取 Host 当前实际生效的 POSIX 时区；目前产品启动默认配置为 `CST-8`，即 UTC+08:00。桌面模拟器继承宿主时区，可通过进程的 `TZ` 测试其他设置。这些配置不写死在 SDK 或 Guest 内。

建议显示时钟的应用在启动、回到前台时同步，再每分钟刷新一次；两次采样之间用单调时间推进。游戏物理、蓄力、音频调度继续使用单调时间。清单需要声明 `{"name":"clock","min_version":[0,2]}`。

验证包括 UTC、正负偏移、半小时/45 分钟、跨年、负 Unix 时间和夏令时切换；SDK 检查协议长度、保留位、偏移范围、时间加法溢出、Host 错误与取消，并确认查询不额外占用协程池槽位。
