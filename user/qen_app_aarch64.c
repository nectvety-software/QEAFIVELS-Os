/* VQEAF .qen demo application (AArch64, position-independent).
 *
 * Runs at EL0 on the independent kernel. create() is the .qen entry point and
 * receives the framework API table pointer; this demo talks to the kernel via
 * SVC syscalls instead, so the pointer is unused. g_msg is a global pointer
 * initialized to a rodata address: under -fPIC it carries an R_AARCH64_RELATIVE
 * relocation that the loader must fix up by the load base, otherwise the app
 * reads a garbage pointer. Loading the same module at two different bases and
 * seeing the correct message both times proves the relocation works. */
typedef unsigned long u64;
typedef long i64;

static inline i64 svc1(i64 n, i64 a0)
{
    register i64 x8 __asm__("x8") = n;
    register i64 x0 __asm__("x0") = a0;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8) : "memory");
    return x0;
}

static void puts(const char *s)
{
    while (*s)
        svc1(1, (i64)(unsigned char)*s++);
}

const char *volatile g_msg = "QEN: relocated arm64 module running at EL0\n";

void *create(const void *api)
{
    (void)api;
    puts(g_msg);
    svc1(3, 0); /* sys_exit */
    return 0;
}