#pragma once

// 运行时符号解析：读入 bh3_profile.inc（由 sdk/gen_profile.py 生成），
// 启动时按 PE 指纹与函数前导字节校验，只有校验通过的符号才交给功能使用。

#include <cstdint>
#include <cstdio>
#include <cstring>

#include <windows.h>

#include "bh3_profile.inc"

namespace ovp
{

enum class Status : int { Uninit = 0, Ok, Partial, BadBuild };

inline uint8_t (&_ok())[SymCount]
{
    static uint8_t mask[SymCount] = { 0 };
    return mask;
}

inline Status& _status()
{
    static Status s = Status::Uninit;
    return s;
}

inline char (&_error())[200]
{
    static char e[200] = { 0 };
    return e;
}

inline uint32_t Raw(Sym s)
{
    return (s >= 0 && s < SymCount) ? kGenEntries[s].rva : 0;
}

inline const char* Key(Sym s)
{
    return (s >= 0 && s < SymCount) ? kGenEntries[s].key : "";
}

inline bool NeedsReview(Sym s)
{
    return (s >= 0 && s < SymCount) && !kGenEntries[s].auto_resolved;
}

inline bool ReadBytes(uintptr_t at, void* dst, size_t n)
{
    SIZE_T got = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), (LPCVOID)at, dst, n, &got)) return false;
    return got == n;
}

inline int HexDigit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

inline bool PrologueOk(uintptr_t at, const char* hex, uint32_t n)
{
    if (!hex || !hex[0] || !n) return true;
    if (n > 32 || std::strlen(hex) < (size_t)n * 2) return false;
    uint8_t bytes[32] = {};
    if (!ReadBytes(at, bytes, n)) return false;
    for (uint32_t i = 0; i < n; ++i)
    {
        const int hi = HexDigit(hex[i * 2]);
        const int lo = HexDigit(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        if ((uint8_t)((hi << 4) | lo) != bytes[i]) return false;
    }
    return true;
}

// 返回 false 表示 profile 与当前游戏版本完全不匹配，调用方应停用数据功能。
inline bool Init(uintptr_t ua)
{
    if (_status() != Status::Uninit) return _status() != Status::BadBuild;
    if (!ua)
    {
        std::snprintf(_error(), sizeof _error(), "profile: UserAssembly 未加载");
        _status() = Status::BadBuild;
        return false;
    }

    IMAGE_DOS_HEADER dos{};
    if (!ReadBytes(ua, &dos, sizeof dos) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew <= 0 || dos.e_lfanew > 0x1000)
    {
        std::snprintf(_error(), sizeof _error(), "profile: 无法读取 PE 头");
        _status() = Status::BadBuild;
        return false;
    }

    IMAGE_NT_HEADERS64 nt{};
    if (!ReadBytes(ua + (uintptr_t)dos.e_lfanew, &nt, sizeof nt) ||
        nt.Signature != IMAGE_NT_SIGNATURE)
    {
        std::snprintf(_error(), sizeof _error(), "profile: 无法读取 PE 头");
        _status() = Status::BadBuild;
        return false;
    }

    if (nt.FileHeader.TimeDateStamp != kBuildTimestamp ||
        nt.OptionalHeader.SizeOfImage != kBuildImageSize)
    {
        std::snprintf(_error(), sizeof _error(),
                      "profile 与当前游戏版本不匹配，请重跑 sdk/gen_profile.py");
        _status() = Status::BadBuild;
        return false;
    }

    const uint32_t image_size = nt.OptionalHeader.SizeOfImage;
    uint32_t valid = 0;
    for (int i = 0; i < SymCount; ++i)
    {
        const GenEntry& e = kGenEntries[i];
        bool good = e.rva != 0 && e.rva < image_size;
        if (good && e.prologue_len)
            good = PrologueOk(ua + e.rva, e.prologue, e.prologue_len);
        _ok()[i] = good ? 1 : 0;
        if (good) ++valid;
    }

    _status() = (valid == (uint32_t)SymCount) ? Status::Ok : Status::Partial;
    if (valid != (uint32_t)SymCount)
        std::snprintf(_error(), sizeof _error(), "profile 校验通过 %u/%d 个符号",
                      valid, (int)SymCount);
    else
        _error()[0] = 0;
    return true;
}

inline Status CurrentStatus() { return _status(); }
inline const char* Error() { return _error(); }

inline int ValidCount()
{
    int n = 0;
    for (int i = 0; i < SymCount; ++i) if (_ok()[i]) ++n;
    return n;
}

// 只有通过校验的符号才返回非零地址。
inline uint32_t Rva(Sym s)
{
    if (s < 0 || s >= SymCount) return 0;
    return _ok()[s] ? kGenEntries[s].rva : 0;
}

inline bool Valid(Sym s)
{
    return Rva(s) != 0;
}

inline constexpr int UltraCount() { return kUltraCount; }

inline uint32_t UltraRva(int i)
{
    if (i < 0 || i >= kUltraCount) return 0;
    return Rva((Sym)((int)SymUltra0 + i));
}

}
