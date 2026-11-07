#include "../ov_native.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

static int checks = 0;
static void check(bool ok, const char* message) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

int main() {
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const SIZE_T page_size = info.dwPageSize;
    auto* page = (unsigned char*)VirtualAlloc(nullptr, page_size * 2,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    check(page != nullptr, "allocate test pages");
    ov::Data data;
    const uintptr_t address = (uintptr_t)page;
    uint64_t expected = 0x123456789ABCDEF0ull, value = 0;
    std::memcpy(page, &expected, sizeof expected);
    check(data.rd(address, value) && value == expected, "read committed memory");
    check(VirtualFree(page, page_size, MEM_DECOMMIT) != 0, "decommit cached page");
    value = expected;
    check(!data.rd(address, value) && value == expected,
          "stale readable cache must not dereference freed memory or overwrite output");
    check(VirtualAlloc(page, page_size, MEM_COMMIT, PAGE_READWRITE) == page, "recommit page");
    DWORD old = 0;
    check(VirtualProtect(page, page_size, PAGE_NOACCESS, &old) != 0, "protect cached page");
    check(!data.rd(address, value), "cached page protection changes fail safely");
    check(VirtualProtect(page, page_size, PAGE_READWRITE, &old) != 0, "restore protection");
    check(VirtualFree(page + page_size, page_size, MEM_DECOMMIT) != 0, "decommit adjacent page");
    check(!data.rd(address + page_size - 4, value), "read across decommitted boundary fails");
    check(!data.rd(0, value), "reject null pointers");
    check(!data.rd(UINTPTR_MAX - 3, value), "reject address overflow");

    const uintptr_t klass = address + 0x100;
    const uintptr_t name = address + 0x200;
    std::memcpy((void*)(klass + 0x20), &name, sizeof name);
    std::strcpy((char*)name, "AvatarManager");
    char copied[64]{};
    check(data.copy_name(klass, copied, sizeof copied) &&
          std::strcmp(copied, "AvatarManager") == 0, "copy bounded class name");
    const uintptr_t invalid_name = 1;
    std::memcpy((void*)(klass + 0x20), &invalid_name, sizeof invalid_name);
    check(!data.copy_name(klass, copied, sizeof copied) && copied[0] == 0,
          "invalid class-name pointers fail safely");
    check(std::strcmp(data.name_of(klass), "?") == 0, "invalid names use fallback");
    std::memcpy((void*)(klass + 0x20), &name, sizeof name);
    std::memset((void*)name, 'A', 64);
    check(!data.copy_name(klass, copied, sizeof copied), "reject unterminated class names");

    const uintptr_t list = address + 0x300;
    const uintptr_t items = address + 0x400;
    const uintptr_t entity = address + 0x500;
    std::memcpy((void*)(list + 0x10), &items, sizeof items);
    int32_t size = 2;
    std::memcpy((void*)(list + 0x18), &size, sizeof size);
    uintptr_t length = 1;
    std::memcpy((void*)(items + 0x18), &length, sizeof length);
    std::vector<uintptr_t> pointers;
    check(!ovn::read_list(data, list, pointers) && pointers.empty(),
          "list length cannot exceed backing-array capacity");
    length = 2;
    std::memcpy((void*)(items + 0x18), &length, sizeof length);
    std::memcpy((void*)(items + 0x20), &entity, sizeof entity);
    check(ovn::read_list(data, list, pointers) && pointers.size() == 1 && pointers[0] == entity,
          "read valid list and skip null entries");
    size = -1;
    std::memcpy((void*)(list + 0x18), &size, sizeof size);
    check(!ovn::read_list(data, list, pointers) && pointers.empty(), "reject negative list size");

    data.ua = address;
    ovn::State state;
    state.camera = address + page_size;
    state.cam_ok = state.vp_valid = true;
    state.w = 1920; state.h = 1080;
    check(!ovn::cam_fast(data, state) && !state.camera && !state.cam_ok && !state.vp_valid,
          "destroyed camera is invalidated before calling engine API");
    state.camera = entity;
    check(!ovn::cam_fast(data, state) && !state.camera, "null Unity native handle invalidates camera");
    state.camera = address + page_size;
    state.cam_ok = state.vp_valid = true;
    state.avatars.push_back(ovn::Ent());
    state.monsters.push_back(ovn::Ent());
    ovn::refresh(data, state);
    check(state.avatars.empty() && state.monsters.empty() && !state.camera &&
          !state.cam_ok && !state.vp_valid && !state.w && !state.h,
          "scene teardown clears entities and camera without engine calls");
    check(!ovn::mgr_from(data, ovp::Raw(ovp::SymAvatarMgrGlobal), 0),
          "unknown manager class cannot enable sampling");

    check(!data.build_index() && data.count == 0, "uninitialized manager globals are retryable");

    // Construct the two verified singleton chains in a sparse synthetic module.
    const SIZE_T module_size = 0x2650000;
    auto* module = (unsigned char*)VirtualAlloc(nullptr, module_size, MEM_RESERVE, PAGE_NOACCESS);
    check(module != nullptr, "reserve synthetic module");
    check(VirtualAlloc(module, page_size, MEM_COMMIT, PAGE_READWRITE) != nullptr, "commit synthetic objects");
    const SIZE_T global_page = ovp::Raw(ovp::SymAvatarMgrGlobal) & ~(page_size - 1);
    const SIZE_T global_end =
        (ovp::Raw(ovp::SymMonsterMgrGlobal) + sizeof(uintptr_t) + page_size - 1) & ~(page_size - 1);
    check(VirtualAlloc(module + global_page, global_end - global_page, MEM_COMMIT, PAGE_READWRITE) != nullptr,
          "commit manager globals");
    auto store_ptr = [](uintptr_t at, uintptr_t ptr) { std::memcpy((void*)at, &ptr, sizeof ptr); };
    const uintptr_t base = (uintptr_t)module;
    const uint32_t globals[] = {
        ovp::Raw(ovp::SymAvatarMgrGlobal),
        ovp::Raw(ovp::SymMonsterMgrGlobal),
    };
    const char* names[] = { "AvatarManager", "MonsterManager" };
    uintptr_t managers[2]{};
    for (int i = 0; i < 2; ++i) {
        const uintptr_t p = base + 0x100 + (uintptr_t)i * 0x400;
        store_ptr(base + globals[i], p);
        store_ptr(p + 8, p + 0x40);
        store_ptr(p + 0x40 + 0x18, p + 0x80);
        store_ptr(p + 0x80 + 0x10, p + 0xC0);
        store_ptr(p + 0xC0 + 0x30, p + 0x100);
        store_ptr(p + 0x100, p + 0x140);
        store_ptr(p + 0x140, p + 0x180);
        store_ptr(p + 0x180 + 0x20, p + 0x200);
        std::strcpy((char*)(p + 0x200), names[i]);
        managers[i] = p + 0x140;
    }
    data = ov::Data();
    data.ua = base;
    ov::Data::clear_seg_cache();
    check(data.build_index() && data.count == 2, "resolve both manager classes without a heap scan");
    check(ovn::mgr_from(data, globals[0], data.find(names[0])) == managers[0] &&
          ovn::mgr_from(data, globals[1], data.find(names[1])) == managers[1], "validate singleton identities");
    store_ptr(base + globals[1], 0);
    check(!data.build_index() && data.count == 1, "partial startup stays pending");
    check(ovn::mgr_from(data, globals[1], data.find(names[1])) == 0, "cleared singleton cannot leak old manager");
    VirtualFree(module, 0, MEM_RELEASE);

    // ---- profile 校验：PE 指纹匹配 + 前导字节，损坏的符号必须降级为 0 ----
    auto* prof_module = (unsigned char*)VirtualAlloc(
        nullptr, (SIZE_T)ovp::kBuildImageSize, MEM_RESERVE, PAGE_NOACCESS);
    check(prof_module != nullptr, "reserve synthetic profile module");
    std::vector<uintptr_t> committed;
    auto commit_page = [&](uintptr_t at) {
        const uintptr_t pg = at & ~((uintptr_t)page_size - 1);
        for (uintptr_t c : committed) if (c == pg) return true;
        if (!VirtualAlloc((void*)pg, page_size, MEM_COMMIT, PAGE_READWRITE)) return false;
        committed.push_back(pg);
        return true;
    };
    check(commit_page((uintptr_t)prof_module), "commit profile header page");
    auto* dos = (IMAGE_DOS_HEADER*)prof_module;
    dos->e_magic = IMAGE_DOS_SIGNATURE;
    dos->e_lfanew = 0x80;
    auto* nt = (IMAGE_NT_HEADERS64*)(prof_module + 0x80);
    nt->Signature = IMAGE_NT_SIGNATURE;
    nt->FileHeader.TimeDateStamp = ovp::kBuildTimestamp;
    nt->OptionalHeader.SizeOfImage = ovp::kBuildImageSize;
    auto hexval = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    for (int i = 0; i < ovp::SymCount; ++i) {
        const ovp::GenEntry& e = ovp::kGenEntries[i];
        if (!e.rva) continue;
        const uintptr_t at = (uintptr_t)prof_module + e.rva;
        check(commit_page(at), "commit profile symbol page");
        if (!e.prologue_len) continue;
        unsigned char* dst = (unsigned char*)at;
        if (i == ovp::SymUltra0) {
            std::memset(dst, 0xFF, e.prologue_len);
            continue;
        }
        for (uint32_t k = 0; k < e.prologue_len; ++k) {
            dst[k] = (unsigned char)((hexval(e.prologue[k * 2]) << 4) |
                                     hexval(e.prologue[k * 2 + 1]));
        }
    }
    check(ovp::Init((uintptr_t)prof_module), "profile init accepts matching PE fingerprint");
    check(ovp::Rva(ovp::SymCameraMain) == ovp::Raw(ovp::SymCameraMain),
          "validated symbol returns generated rva");
    check(ovp::Rva(ovp::SymUltra0) == 0, "corrupted prologue disables the symbol");
    check(ovp::ValidCount() == ovp::SymCount - 1 &&
          ovp::CurrentStatus() == ovp::Status::Partial,
          "partial profile keeps only verified symbols");
    check(ovp::UltraRva(1) == ovp::Raw(ovp::SymUltra1) && ovp::UltraRva(0) == 0,
          "ultra table mirrors per-symbol validation");
    VirtualFree(prof_module, 0, MEM_RELEASE);

    VirtualFree(page, 0, MEM_RELEASE);
    std::printf("PASS: %d memory/lifecycle regression checks\n", checks);
    return 0;
}
