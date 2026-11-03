// bh3_call.h —— 正确使用 SDK 的方式：调函数，不是猜偏移
// ============================================================================
// SDK 给的是 (类名 / klass_va / 方法 RVA / token / MethodInfo*)。
// 它的用法是把游戏自己的函数当成 API 调，而不是拿偏移去读内存：
//
//   1) 取对象：调游戏自己的静态 getter / 单例 / 管理器方法，不要全内存扫描。
//   2) 取值  ：调 getter（get_name / get_hp / get_position），不要读 +0x90。
//   3) 画 ESP：调 Camera::WorldToScreenPoint(世界坐标)，不要自己乘投影矩阵。
//
// IL2CPP 的 x64 调用约定（本 DLL 必须按这个传参，否则必崩）
// ---------------------------------------------------------------------------
//   实例方法  R fn(void* this /*rcx*/, 参数..., const MethodInfo* mi /*最后*/)
//   静态方法  R fn(参数..., const MethodInfo* mi /*最后*/)
//   返回大结构体(>16B，比如 Vector3) 多一个隐藏首参：
//             R fn(void* retbuf /*rcx*/, void* this /*rdx*/, 参数..., const MethodInfo* mi)
//   -- mi 就是 SDK 里的 methodinfo_va，缺了它游戏内部自己的类型检查会直接崩。
//
// 变量参数数量在 C++ 里没法一次写全，所以这里给出 0~3 个参数的显式重载。
// ============================================================================
#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>

namespace bh3 {

typedef void* (*Fn0)(void* mi);
typedef void* (*FnM0)(void* self, void* mi);
typedef void* (*FnM1)(void* self, void* a1, void* mi);
typedef float (*FnM0f)(void* self, void* mi);
typedef int   (*FnM0i)(void* self, void* mi);
typedef bool  (*FnM0b)(void* self, void* mi);
typedef void  (*FnM0v)(void* self, void* mi);
typedef void  (*FnM1v)(void* self, void* a1, void* mi);
typedef void  (*FnS0v)(void* mi);

// Vector3 按值传递在 x64 上是"按 16 字节内存参数"走（结构体只装 3 个 float，
// 但 ABI 会把它塞进一个栈/寄存器对），所以跨边界调用一律用指针形式最安全。
struct Vec3 { float x, y, z; };

struct Api {
    uintptr_t ua = 0;                  // UserAssembly.dll 模块基址（每次运行都不同）

    template <class T> T fn(uint32_t rva) const { return (T)(ua + rva); }

    // 实例 getter，0 参数
    void*  call_ptr(uint32_t rva, void* self, void* mi)  const { return fn<FnM0>(rva)(self, mi); }
    float  call_f32(uint32_t rva, void* self, void* mi)  const { return fn<FnM0f>(rva)(self, mi); }
    int    call_i32(uint32_t rva, void* self, void* mi)  const { return fn<FnM0i>(rva)(self, mi); }
    bool   call_bool(uint32_t rva, void* self, void* mi) const { return fn<FnM0b>(rva)(self, mi); }

    // 静态 getter，0 参数
    void*  call_static_ptr(uint32_t rva, void* mi) const { return fn<Fn0>(rva)(mi); }

    // 返回 Vector3 的实例方法（隐藏返回缓冲在 rcx，this 在 rdx）
    bool call_vec3(uint32_t rva, void* self, void* mi, Vec3& out) const {
        struct { float x, y, z, pad; } buf = { 0, 0, 0, 0 };
        typedef void (*F)(void*, void*, void*);
        fn<F>(rva)(&buf, self, mi);
        out.x = buf.x; out.y = buf.y; out.z = buf.z;
        bool ok = true;
        if (!(out.x > -1e7f && out.x < 1e7f)) ok = false;
        if (!(out.y > -1e7f && out.y < 1e7f)) ok = false;
        if (!(out.z > -1e7f && out.z < 1e7f)) ok = false;
        return ok;
    }

    // WorldToScreenPoint(Vector3) —— 传值版：xmm2 里放 Vector3（x,y,z,0）
    bool call_w2s(uint32_t rva, void* self, void* mi, const Vec3& w, Vec3& out) const {
        struct { float x, y, z, pad; } buf = { 0, 0, 0, 0 };
        typedef void (*F)(void*, void*, float, float, float, void*);
        fn<F>(rva)(&buf, self, w.x, w.y, w.z, mi);
        out.x = buf.x; out.y = buf.y; out.z = buf.z;
        return out.x > -1e6f && out.x < 1e6f && out.y > -1e6f && out.y < 1e6f;
    }
};

// ---------------------------------------------------------------- 对象读取
// 只做"必须做"的裸读：读出游戏函数返回的指针所指向的内容。
// 注意：这不是用偏移去猜字段，而是读 getter 返回的托管对象/字符串。
inline bool safe_read(uintptr_t addr, void* dst, size_t n) {
    if (!addr) return false;
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), (LPCVOID)addr, dst, n, &got) && got == n;
}

// Il2CppString: length(int32) @ +0x10, UTF-16 chars @ +0x14
inline bool read_string(uintptr_t sp, char* out, size_t cap) {
    int32_t len = 0;
    if (!safe_read(sp + 0x10, &len, 4)) return false;
    if (len <= 0 || len > 4096) return false;
    if (cap < (size_t)len * 3 + 1) return false;
    size_t o = 0;
    for (int i = 0; i < len; ++i) {
        uint16_t w = 0;
        if (!safe_read(sp + 0x14 + i * 2, &w, 2)) return false;
        if (w < 0x80) out[o++] = (char)w;
        else if (w < 0x800) { out[o++] = (char)(0xC0 | (w >> 6)); out[o++] = (char)(0x80 | (w & 63)); }
        else { out[o++] = (char)(0xE0 | (w >> 12)); out[o++] = (char)(0x80 | ((w >> 6) & 63)); out[o++] = (char)(0x80 | (w & 63)); }
    }
    out[o] = 0;
    return true;
}

} // namespace bh3
