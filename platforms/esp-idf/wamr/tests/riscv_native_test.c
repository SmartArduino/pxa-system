/* Exercise the same RV32 ILP32F call bridge used by the ESP-IDF Host.
 * Freestanding Linux entry point so Clang and QEMU need no target sysroot. */
typedef unsigned (*target_fn)(void);
extern unsigned invokeNative(target_fn target, unsigned *args, unsigned spills);

static inline unsigned stack_pointer(void)
{
    unsigned address;
    __asm__ volatile ("mv %0,sp" : "=r"(address));
    return address;
}

static unsigned variadic_host(unsigned count, ...)
{
    _Alignas(16) unsigned char scratch[16];
    unsigned scratch_address;
    __asm__ volatile ("mv %0,%1" : "=r"(scratch_address) : "r"(scratch));
    if ((stack_pointer() | scratch_address) & 15)
        return 1;

    __builtin_va_list args;
    __builtin_va_start(args, count);
    unsigned wrong = 0;
    for (unsigned i = 1; i < count; ++i)
        wrong |= __builtin_va_arg(args, unsigned) != i;
    __builtin_va_end(args);
    return wrong;
}

static unsigned float_host(float a, float b, float c, float d,
                           float e, float f, float g, float h)
{
    return (stack_pointer() & 15) || a != 1 || b != 2 || c != 3 || d != 4
        || e != 5 || f != 6 || g != 7 || h != 8;
}

void _start(void)
{
    /* WAMR ILP32F argument layout: a0..a7, fa0..fa7, then spill words. */
    unsigned args[24];
    unsigned wrong = 0;
    unsigned before = stack_pointer();
    for (unsigned i = 0; i < 8; ++i) {
        union { float value; unsigned bits; } fp = { .value = i + 1 };
        args[i] = i;
        args[8 + i] = fp.bits;
        args[16 + i] = 8 + i;
    }
    for (unsigned spills = 0; spills <= 8; ++spills) {
        args[0] = 8 + spills;
        wrong |= invokeNative((target_fn)variadic_host, args, spills);
        wrong |= stack_pointer() != before;
    }
    wrong |= invokeNative((target_fn)float_host, args, 0);
    wrong |= stack_pointer() != before;

    register unsigned status __asm__("a0") = wrong;
    register unsigned syscall __asm__("a7") = 93; /* Linux exit */
    __asm__ volatile ("ecall" :: "r"(status), "r"(syscall) : "memory");
    __builtin_unreachable();
}
