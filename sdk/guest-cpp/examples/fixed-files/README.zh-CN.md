# 固定 SD 资源根

这是默认资源访问示例：调用 `ctx.fs().app_media()`，无需文件权限声明或目录选择。
Host 按已验证的应用身份解析卷内 `/PXA/media/<app-key>`；应用不接收原生路径。
包声明 FS 0.3 的 `app-media-root`、`scoped-roots` 与 `read-at` feature。

在 Host 绑定的应用媒体目录放入与 `scoped-files` 示例相同的 8192 B `fixture.bin`。
启动后执行内容、seek、EOF、只读、作用域及句柄生命周期测试。无卡或缺文件时明确报错，
不弹目录选择器，也不回退到其他卷。GameKit 先行版由板策略明确指定 SD，
整应用安装后由安装位置登记提供目标卷；目前尚未实现后者。

```cpp
auto root = co_await ctx.fs().app_media();
if (!root) co_return std::unexpected(root.error());
auto file = co_await root->open("fixture.bin");
```

0.1.1 还检查同步指定偏移读取、游标保持、EOF 和 cookie 取消。`File::supports_read_at()`
报告文件能力；`read_at(offset, cookie, packet)` 借用调用方的头部加数据缓冲，数据从第 24 字节开始，
单次最多 4096 B。每个文件使用严格递增的非零 cookie；取消会退役该 cookie 及更早请求。
它不分配协程或堆缓冲，也没有正在后台执行的读取。

也可用有类型的缓冲隐藏线格式，返回视图借用缓冲，下一次读取前有效：

```cpp
pxa::FileReadBuffer<4096> buffer;
std::uint64_t cookie = 0;
auto bytes = file->read_at(0, ++cookie, buffer);
if (!bytes) co_return std::unexpected(bytes.error());
// bytes->data() / bytes->size()；空视图表示 EOF。
```

该目录放可由用户导入、独立于卸载保留的资源。私有存档继续使用私有 FS；
包内已签名资源继续使用 Assets。主动导入其他目录才使用可选的 `request_directory()`。
