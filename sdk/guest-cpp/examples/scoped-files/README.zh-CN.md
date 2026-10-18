# 授权目录与 SD 数据读取

这是 FS 0.3 的签名应用示例，使用普通 PXA Guest ABI，不依赖原生挂载路径。
适用于已登记只读数据卷并提供系统目录选择器的 Host；DOIT GameKit 已接入。

先在 SD 上建立 `PXA-tests/fixture.bin`，内容为下面的 8192 B。使用 PXADB 时路径相对系统存储根，不带开头的 `/`：

```sh
python3 -c 'from pathlib import Path; Path("fixture.bin").write_bytes(bytes((i*37+11)&255 for i in range(8192)))'
pxadb fs mkdir sdcard/PXA-tests
pxadb fs push fixture.bin sdcard/PXA-tests/fixture.bin
```

启动后允许“用户选择的目录”读取权限，选择 SD 卡的 `PXA-tests` 文件夹，然后确认。
测试逐字节读取、seek、EOF、写入拒绝、路径越界拒绝、父目录关闭后的文件生命周期和失效句柄。
成功时显示 PASS，并写入 `SD TEST PASS bytes=8192 ...` 日志。
GrantId 存在应用私有文件中，下次启动使用 `open_grant()`。
原卡离线或权限失效时返回错误；本示例在重开失败后提供重新选择流程。

SDK 用法：

```cpp
auto permission = co_await ctx.permissions().acquire("fs.user-directory", "read");
if (!permission) co_return std::unexpected(permission.error());
auto grant = co_await ctx.fs().request_directory(*permission, "选择游戏数据目录");
if (!grant) co_return std::unexpected(grant.error());
auto file = co_await grant->root.open("id1/pak0.pak");
if (!file) co_return std::unexpected(file.error());
std::array<std::byte, 512> bytes;
auto read = file->read(bytes);
// permission 必须活到最后一个派生目录、文件和请求结束。
```

`Directory::open()` 的内置请求缓冲支持 255 B 路径，以适配默认 1 KiB 协程帧。
长路径可以传入调用者保持存活且不修改的缓冲，使用
`directory.open(path, mode, packet)`；协议上限为 1024 B，仍受实际根报告的限制。

当前 Host 数据后端只读，使用已有 FILE read 和 FS seek；没有宣称异步 READ_AT、目录分页、热插拔或 FAT 包安装已经可用。
