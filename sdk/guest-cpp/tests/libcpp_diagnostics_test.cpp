#include <pxa/libcpp_diagnostics.hpp>
#include <cassert>
#include <cstring>
#include <string>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

static std::string diagnostic(int mode) {
    int pipes[2];
    assert(pipe(pipes) == 0);
    const auto pid = fork();
    assert(pid >= 0);
    if (!pid) {
        const rlimit no_core{0, 0};
        setrlimit(RLIMIT_CORE, &no_core);
        close(pipes[0]);
        assert(dup2(pipes[1], STDERR_FILENO) >= 0);
        close(pipes[1]);
        if (mode == 0)
            pxa::detail::bounded_libcpp_abort("%s:%d: %s %u %zu %td %%", "reader.cpp", -7, "bounds", 42u, std::size_t(65536), std::ptrdiff_t(-9));
        if (mode == 1)
            pxa::detail::bounded_libcpp_abort("value=%p trailing=%s", reinterpret_cast<void*>(1), "unread");
        if (mode == 2) {
            std::string long_message(2048, 'x');
            pxa::detail::bounded_libcpp_abort("%s", long_message.c_str());
        }
        pxa::detail::bounded_libcpp_abort("%s", static_cast<const char*>(nullptr));
    }
    close(pipes[1]);
    std::string text;
    char block[1024];
    for (ssize_t n; (n = read(pipes[0], block, sizeof(block))) > 0;)
        text.append(block, std::size_t(n));
    close(pipes[0]);
    int status;
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
    return text;
}
int main() {
    assert(diagnostic(0) == "libc++ fatal: reader.cpp:-7: bounds 42 65536 -9 %\n");
    assert(diagnostic(1) == "libc++ fatal: value=%p trailing=%s\n");
    const auto long_text = diagnostic(2);
    assert(long_text.size() == 513 && long_text.back() == '\n');
    assert(diagnostic(3) == "libc++ fatal: (null)\n");
    std::puts("Bounded libc++ diagnostics: fatal checks, readable context, truncation and unsupported formats OK");
}
