// antidet.cpp — ARM64 Android C++20
#include "antidet.h"
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <dlfcn.h>
#include <android/log.h>
#include <fcntl.h>
#include <unistd.h>

#define TAG "dvm"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

static const char* const k_blocked[] = {
    "/data/adb", "ksu", "ksud", "magisk", "supersu",
    "/sbin/su", "/system/bin/su", "/system/xbin/su",
    "/data/local/tmp/lib", "zygisksu", ".magisk", nullptr
};

static bool is_blocked(const char* path) {
    if (!path) return false;
    for (int i = 0; k_blocked[i]; i++)
        if (strstr(path, k_blocked[i])) return true;
    return false;
}

static void mem_hook(void* dst, const void* src, size_t len) {
    int fd = open("/proc/self/mem", O_RDWR);
    if (fd >= 0) { pwrite(fd, src, len, (off_t)(uintptr_t)dst); close(fd); }
    else {
        uintptr_t pg = (uintptr_t)dst & ~0xFFFULL;
        mprotect((void*)pg, 0x2000, PROT_READ|PROT_WRITE|PROT_EXEC);
        memcpy(dst, src, len);
    }
    __builtin___clear_cache((char*)dst, (char*)dst+len);
}

static void* make_tramp(void* orig) {
    void* t = mmap(nullptr, 64, PROT_READ|PROT_WRITE|PROT_EXEC,
                   MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
    if (t == MAP_FAILED) return nullptr;
    memcpy(t, orig, 16);
    uintptr_t b = (uintptr_t)orig + 16;
    uint8_t jb[16]={0x50,0x00,0x00,0x58,0x00,0x02,0x1F,0xD6,
        (uint8_t)b,(uint8_t)(b>>8),(uint8_t)(b>>16),(uint8_t)(b>>24),
        (uint8_t)(b>>32),(uint8_t)(b>>40),(uint8_t)(b>>48),(uint8_t)(b>>56)};
    memcpy((uint8_t*)t+16, jb, 16);
    __builtin___clear_cache((char*)t, (char*)t+32);
    return t;
}

static void hook(void* tgt, void* rep, void** orig) {
    *orig = make_tramp(tgt);
    uintptr_t r = (uintptr_t)rep;
    uint8_t p[16]={0x50,0x00,0x00,0x58,0x00,0x02,0x1F,0xD6,
        (uint8_t)r,(uint8_t)(r>>8),(uint8_t)(r>>16),(uint8_t)(r>>24),
        (uint8_t)(r>>32),(uint8_t)(r>>40),(uint8_t)(r>>48),(uint8_t)(r>>56)};
    mem_hook(tgt, p, 16);
}

using openat_t    = int(*)(int,const char*,int,...);
using faccessat_t = int(*)(int,const char*,int,int);
static openat_t    g_openat    = nullptr;
static faccessat_t g_faccessat = nullptr;

static int h_openat(int dfd, const char* path, int flags, ...) {
    if (is_blocked(path)) { errno = ENOENT; return -1; }
    return g_openat(dfd, path, flags, 0);
}
static int h_faccessat(int dfd, const char* path, int mode, int flags) {
    if (is_blocked(path)) { errno = ENOENT; return -1; }
    return g_faccessat(dfd, path, mode, flags);
}

void antidet::install() {
    void* fn = dlsym(RTLD_DEFAULT, "openat");
    if (fn) { hook(fn,(void*)h_openat,(void**)&g_openat); LOGI("openat hooked"); }
    fn = dlsym(RTLD_DEFAULT, "faccessat");
    if (fn) { hook(fn,(void*)h_faccessat,(void**)&g_faccessat); LOGI("faccessat hooked"); }
    prctl(PR_SET_NAME, "mali_worker_0", 0,0,0);
}
