"""Lifetime and lazy-work mistakes fail before linking or running on a device."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
SDK = Path(__file__).resolve().parents[1]

class ServiceDiagnostics(unittest.TestCase):
    def compile(self, body):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'probe.cpp'
            path.write_text('#include <pxa/app.hpp>\nusing namespace pxa;\n' + body)
            return subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++2c',
                '-fsyntax-only', '-fno-exceptions', '-fno-rtti', '-Wno-attributes',
                '-Wall', '-Wextra', '-Werror', '-I', str(SDK / 'include'), str(path)],
                capture_output=True, text=True)
    def test_transport_cannot_change_address(self):
        for expression in ('original', 'std::move(original)'):
            result = self.compile('void test(){Transport original;Transport copied(' + expression + ');}')
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('stable address', result.stderr)
    def test_lazy_task_requires_consumer(self):
        result = self.compile('void test(Context& ctx){ctx.clock().now();}')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('co_awaited', result.stderr)
    def test_task_is_consumed_explicitly(self):
        result = self.compile('Task<void> test(Context& ctx){auto task=ctx.clock().now();auto result=co_await task;co_return Result<void>{};}')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('co_await std::move(task)', result.stderr)
    def test_yield_requires_await(self):
        result = self.compile('void test(Context& ctx){ctx.clock().yield();}')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('co_awaited', result.stderr)
    def test_renderer_must_outlive_frame(self):
        result = self.compile('void test(Transport& tx,game::DrawBuffer<256>& buffer){auto frame=game::Renderer(tx,1,0).frame(buffer);}')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("lvalue", result.stderr)
    def test_pixel_lease_must_outlive_borrow(self):
        for method in ('pixels', 'rgb565'):
            result = self.compile('void test(SurfaceFrame&& frame){auto pixels=std::move(frame).' + method + '();}')
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('Keep the SurfaceFrame alive', result.stderr)
    def test_valid_consumers(self):
        result = self.compile('''
Task<void> test(Context& ctx){
    auto now=co_await ctx.clock().now();
    if(!now)co_return std::unexpected(now.error());
    co_return co_await ctx.clock().yield();
}
static_assert(std::same_as<decltype(std::declval<AudioSession&>().format()), const AudioFormat&>);
static_assert(std::same_as<decltype(std::declval<AudioSession&&>().format()), AudioFormat>);
static_assert(std::same_as<decltype(std::declval<Asset&&>().descriptor()), AssetDescriptor>);
static_assert(std::same_as<decltype(std::declval<game::Renderer&&>().info()), game::RenderInfo>);
static_assert(!std::is_move_constructible_v<Transport>);
static_assert(!std::is_copy_constructible_v<Transport>);
''')
        self.assertEqual(result.returncode, 0, result.stderr)
if __name__ == '__main__': unittest.main()
