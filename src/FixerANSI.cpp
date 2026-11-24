/* ============================================================================
 *  AE 2027 中文乱码修复器（产物：!00_AE27CharsetFixer.aex）
 *
 *  AE 2027 会给每个效果模块指定两种文本编码模式之一：
 *
 *    老式模块  ->  AE 把模块给的 ANSI 字节转成 UTF-8
 *    新式模块  ->  AE 把模块给的字节直接当 UTF-8 用
 *
 *  所以一个模块只要"自带文字"和它被分配的模式对不上，显示出来就是乱码。
 *  本插件挂钩白名单模块的 PF 入口，在名字交给 AE 之前按该模块需要的方向转一次：
 *
 *    老式 + UTF-8 文本  ->  UTF-8 转 ANSI   （Trapcode Geo / Form / Particular ...）
 *    新式 + ANSI 文本   ->  ANSI 转 UTF-8   （Trapcode Shine / Starglow / 3DStroke ...）
 *
 *  以 AE 2026 为基准验证：22 个效果、约 6900 个参数名，0 处差异
 *  （做法见 tools/ 与 docs/VERIFICATION.md）。
 *
 *  派生自 AdoreLowen 的 AE-UTF8-Fixer（MIT），静态链接 MinHook（BSD-2）。
 *  本项目按 MIT 许可发布，见 LICENSE 与 NOTICE.md。
 * ==========================================================================*/

#include "AEConfig.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_Macros.h"
#include "Param_Utils.h"
#include "AE_EffectVers.h"
#include "AE_EffectSuites.h"
#include "AE_GeneralPlug.h"

#include <windows.h>
#include <tlhelp32.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "MinHook.h"

namespace {

/* ------------------------------------------------------------- 诊断日志 ---
 * 默认关闭。启动 AE 前设置环境变量 AEUTF8FIXER_LOG=1，
 * 日志写入 %TEMP%\AECharsetFixer.log（有行数上限）。 */

constexpr int kLogLineBudget = 4000;

bool LogEnabled() noexcept {
    static const bool enabled = [] {
        wchar_t buf[8]{};
        return GetEnvironmentVariableW(L"AEUTF8FIXER_LOG", buf, 8) > 0;
    }();
    return enabled;
}

void Log(const char* fmt, ...) noexcept {
    static std::atomic<int> budget{ kLogLineBudget };
    static std::mutex writeMutex;

    if (!LogEnabled() || budget.load(std::memory_order_relaxed) <= 0) return;

    char body[1024];
    va_list args;
    va_start(args, fmt);
    _vsnprintf_s(body, sizeof(body), _TRUNCATE, fmt, args);
    va_end(args);

    std::lock_guard<std::mutex> lock(writeMutex);
    if (budget.fetch_sub(1, std::memory_order_relaxed) <= 0) return;

    wchar_t tempDir[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"TEMP", tempDir, MAX_PATH) == 0) return;

    std::wstring path(tempDir);
    path += L"\\AECharsetFixer.log";

    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"ab") != 0 || !file) return;

    SYSTEMTIME now{};
    GetLocalTime(&now);
    fprintf(file, "[%02d:%02d:%02d.%03d] %s\n",
            now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, body);
    fclose(file);
}

/* --------------------------------------------------------------- 编码方向 ---
 * 只有两种方向，外加"这个模块不碰"。 */

enum class Direction { None, Utf8ToAnsi, GbkToUtf8 };

/* thread_local：只在白名单模块自己的入口函数执行期间有值。
 * 全局参数钩子靠它区分"这次调用该不该管"。 */
thread_local Direction tlsDirection = Direction::None;

bool IsEastAsian(unsigned int cp) noexcept {
    return (cp >= 0x3000 && cp <= 0x303F) ||   // CJK punctuation
           (cp >= 0x3040 && cp <= 0x30FF) ||   // kana
           (cp >= 0x3400 && cp <= 0x4DBF) ||   // CJK ext A
           (cp >= 0x4E00 && cp <= 0x9FFF) ||   // CJK unified
           (cp >= 0xF900 && cp <= 0xFAFF) ||   // compatibility ideographs
           (cp >= 0xFF00 && cp <= 0xFFEF);     // full-width forms
}

/* 严格 UTF-8 校验：遇到任何不合法的字节序列就返回 false。 */
bool IsValidUtf8(const char* text, bool* hasEastAsian = nullptr) noexcept {
    if (hasEastAsian) *hasEastAsian = false;

    const auto* p = reinterpret_cast<const unsigned char*>(text);
    while (*p) {
        const unsigned c = *p;
        int extra = 0;
        unsigned cp = 0;

        if (c < 0x80) {
            ++p;
            continue;
        } else if ((c & 0xE0) == 0xC0) {
            extra = 1;
            cp = c & 0x1Fu;
        } else if ((c & 0xF0) == 0xE0) {
            extra = 2;
            cp = c & 0x0Fu;
        } else if ((c & 0xF8) == 0xF0) {
            extra = 3;
            cp = c & 0x07u;
        } else {
            return false;
        }

        for (int i = 1; i <= extra; ++i) {
            if ((p[i] & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (p[i] & 0x3Fu);
        }
        p += extra + 1;

        if (hasEastAsian && IsEastAsian(cp)) *hasEastAsian = true;
    }
    return true;
}

bool IsUtf8WithEastAsian(const char* text) noexcept {
    if (!text || !text[0]) return false;
    bool hasEastAsian = false;
    return IsValidUtf8(text, &hasEastAsian) && hasEastAsian;
}

/* 判定"新式模块却自带 ANSI 文本"：字节不是合法 UTF-8，
 * 但按 ANSI 代码页能干净解出来，而且里面有东亚字符。 */
bool IsAnsiWithEastAsian(const char* text) noexcept {
    if (!text || !text[0]) return false;
    if (IsValidUtf8(text)) return false;

    wchar_t wide[4096];
    const int len = MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, text, -1, wide, 4096);
    if (len <= 1) return false;

    for (int i = 0; i < len; ++i) {
        if (IsEastAsian(static_cast<unsigned int>(wide[i]))) return true;
    }
    return false;
}

std::string Utf8ToAnsi(const char* utf8) {
    std::string result;
    const int wideLen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, nullptr, 0);
    if (wideLen <= 1) return result;

    std::unique_ptr<wchar_t[]> wide(new (std::nothrow) wchar_t[wideLen]);
    if (!wide) return result;
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, wide.get(), wideLen) != wideLen) {
        return result;
    }

    BOOL usedDefault = FALSE;
    const int ansiLen = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, wide.get(), -1,
                                            nullptr, 0, nullptr, &usedDefault);
    if (ansiLen <= 1 || usedDefault) return result;      // 映射不了就整串放弃，绝不写问号

    result.resize(static_cast<size_t>(ansiLen) - 1);
    if (WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, wide.get(), -1,
                            result.data(), ansiLen, nullptr, &usedDefault) != ansiLen ||
        usedDefault) {
        result.clear();
    }
    return result;
}

std::string AnsiToUtf8(const char* ansi) {
    std::string result;
    const int wideLen = MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, ansi, -1, nullptr, 0);
    if (wideLen <= 1) return result;

    std::unique_ptr<wchar_t[]> wide(new (std::nothrow) wchar_t[wideLen]);
    if (!wide) return result;
    if (MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, ansi, -1, wide.get(), wideLen) != wideLen) {
        return result;
    }

    const int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wide.get(), -1, nullptr, 0, nullptr, nullptr);
    if (utf8Len <= 1) return result;

    result.resize(static_cast<size_t>(utf8Len) - 1);
    if (WideCharToMultiByte(CP_UTF8, 0, wide.get(), -1, result.data(), utf8Len, nullptr, nullptr) != utf8Len) {
        result.clear();
    }
    return result;
}

/* --------------------------------------------------------------- 字符串驻留 ---
 * 交还给 AE 的指针（效果名、下拉项等）会被长期引用，因此地址必须稳定。
 * 用节点式容器实现：不淘汰、不搬移，指针永不失效。 */

constexpr size_t kCacheShards = 4;

struct TransparentHash {
    using is_transparent = void;
    size_t operator()(std::string_view value) const noexcept {
        return std::hash<std::string_view>{}(value);
    }
};

struct TransparentEqual {
    using is_transparent = void;
    bool operator()(std::string_view lhs, std::string_view rhs) const noexcept {
        return lhs == rhs;
    }
};

struct StringCache {
    std::shared_mutex mutex;
    std::unordered_set<std::string, TransparentHash, TransparentEqual> items;
};

StringCache& CacheShard(std::string_view key) noexcept {
    static StringCache shards[kCacheShards];
    return shards[std::hash<std::string_view>{}(key) % kCacheShards];
}

const char* Intern(std::string value) {
    if (value.empty()) return nullptr;

    StringCache& shard = CacheShard(value);
    {
        std::shared_lock<std::shared_mutex> read(shard.mutex);
        if (auto it = shard.items.find(std::string_view(value)); it != shard.items.end()) {
            return it->c_str();
        }
    }
    std::unique_lock<std::shared_mutex> write(shard.mutex);
    auto [it, inserted] = shard.items.insert(std::move(value));
    return it->c_str();
}

/* ----------------------------------------------------------------- 写回 ---
 * 只改可写内存：有些效果给的是只读段里的字符串，硬写会崩。 */

bool IsWritable(const void* address, size_t length) noexcept {
    if (!address || length == 0) return false;

    constexpr DWORD kWritable = PAGE_READWRITE | PAGE_WRITECOPY |
                                PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;

    const auto* cursor = static_cast<const unsigned char*>(address);
    const auto* end = cursor + length;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(cursor, &info, sizeof(info)) == 0) return false;
        if ((info.Protect & kWritable) == 0) return false;

        const auto* regionEnd = static_cast<const unsigned char*>(info.BaseAddress) + info.RegionSize;
        if (regionEnd <= cursor) return false;
        cursor = regionEnd;
    }
    return true;
}

bool FixPointer(const char*& pointer) noexcept {
    if (!pointer || !pointer[0]) return false;

    if (tlsDirection == Direction::GbkToUtf8) {
        if (!IsAnsiWithEastAsian(pointer)) return false;
        const std::string converted = AnsiToUtf8(pointer);
        if (converted.empty()) return false;
        const char* interned = Intern(converted);
        if (!interned) return false;
        pointer = interned;
        return true;
    }

    if (!IsUtf8WithEastAsian(pointer)) return false;
    const std::string converted = Utf8ToAnsi(pointer);
    if (converted.empty()) return false;
    const char* interned = Intern(converted);
    if (!interned) return false;
    pointer = interned;
    return true;
}

template <size_t N>
bool FixArray(char (&buffer)[N]) noexcept {
    if (!buffer[0]) return false;

    if (tlsDirection == Direction::GbkToUtf8) {
        if (!IsAnsiWithEastAsian(buffer)) return false;
        const std::string converted = AnsiToUtf8(buffer);
        if (converted.empty() || converted.size() > N - 1) return false;
        if (!IsWritable(buffer, N)) return false;
        memcpy_s(buffer, N, converted.c_str(), converted.size() + 1);
        return true;
    }

    if (!IsUtf8WithEastAsian(buffer)) return false;
    const std::string converted = Utf8ToAnsi(buffer);
    if (converted.empty() || converted.size() > N - 1) return false;
    if (!IsWritable(buffer, N)) return false;
    memcpy_s(buffer, N, converted.c_str(), converted.size() + 1);
    return true;
}

bool FixParam(PF_ParamDef* param) noexcept {
    if (!param) return false;

    bool changed = FixArray(param->PF_DEF_NAME);

    switch (param->param_type) {
    case PF_Param_POPUP:
        changed |= FixPointer(param->u.pd.u.PF_DEF_NAMESPTR);
        break;
    case PF_Param_CHECKBOX:
        changed |= FixPointer(param->u.bd.u.PF_DEF_NAMEPTR);
        break;
    case PF_Param_BUTTON:
        changed |= FixPointer(param->u.button_d.u.PF_DEF_NAMESPTR);
        break;
    case PF_Param_SLIDER:
        changed |= FixArray(param->u.sd.value_desc);
        break;
    case PF_Param_FIX_SLIDER:
        changed |= FixArray(param->u.fd.value_str);
        changed |= FixArray(param->u.fd.value_desc);
        break;
    case PF_Param_FLOAT_SLIDER:
        changed |= FixArray(param->u.fs_d.value_desc);
        break;
    default:
        break;
    }
    return changed;
}

bool NeedsFixing(const char* text) noexcept {
    return tlsDirection == Direction::GbkToUtf8 ? IsAnsiWithEastAsian(text)
                                                : IsUtf8WithEastAsian(text);
}

/* 模块产出参数后再扫一遍：有些效果会在 UPDATE_PARAMS_UI 里改写自己的名字。 */
int FixParamList(PF_InData* inData, PF_ParamDef* params[]) noexcept {
    if (!inData || !params || inData->num_params <= 0) return 0;

    PF_ParamIndex limit = inData->num_params;
    if (limit > 10000) limit = 10000;

    int changed = 0;
    for (PF_ParamIndex i = 1; i < limit; ++i) {
        PF_ParamDef* param = params[i];
        if (param && NeedsFixing(param->PF_DEF_NAME) && FixParam(param)) {
            ++changed;
        }
    }
    return changed;
}

/* --------------------------------------------------------------- 挂钩 ---*/

constexpr int kWrapperPoolSize = 2048;

std::atomic<void*> g_originalEntryPoints[kWrapperPoolSize] = {};
std::atomic<int> g_wrapperDirections[kWrapperPoolSize] = {};
std::atomic<int> g_wrapperCount{ 0 };
std::mutex g_wrapperMutex;

bool CreateHook(void* target, void* detour, void** original) noexcept {
    const MH_STATUS status = MH_CreateHook(target, detour, original);
    if (status != MH_OK) {
        Log("hook create failed target=%p status=%d", target, static_cast<int>(status));
        return false;
    }
    if (MH_EnableHook(target) != MH_OK) {
        MH_DisableHook(target);
        MH_RemoveHook(target);
        Log("hook enable failed target=%p", target);
        return false;
    }
    return true;
}

/* --- 宿主级参数钩子：由 tlsDirection 控制是否生效 ----------------------- */

std::unordered_set<void*> g_hookedAddParam;
std::mutex g_addParamMutex;
PF_Err(*g_originalAddParam)(PF_ProgPtr, PF_ParamIndex, PF_ParamDef*) = nullptr;
PF_Err(*g_originalUpdateParamUI)(PF_ProgPtr, PF_ParamIndex, const PF_ParamDef*) = nullptr;

PF_Err HookedAddParam(PF_ProgPtr effectRef, PF_ParamIndex index, PF_ParamDef* param) {
    if (param && tlsDirection != Direction::None) FixParam(param);
    return g_originalAddParam(effectRef, index, param);
}

PF_Err HookedUpdateParamUI(PF_ProgPtr effectRef, PF_ParamIndex index, const PF_ParamDef* param) {
    if (!param || tlsDirection == Direction::None) {
        return g_originalUpdateParamUI(effectRef, index, param);
    }
    PF_ParamDef copy = *param;
    FixParam(&copy);
    return g_originalUpdateParamUI(effectRef, index, &copy);
}

void HookAddParamOnce(void* target) noexcept {
    if (!target) return;
    std::lock_guard<std::mutex> lock(g_addParamMutex);
    if (g_hookedAddParam.count(target)) return;
    if (CreateHook(target, reinterpret_cast<void*>(&HookedAddParam),
                   reinterpret_cast<void**>(&g_originalAddParam))) {
        g_hookedAddParam.insert(target);
    }
}

bool HookUpdateParamUI(SPBasicSuite* basic) noexcept {
    if (!basic || g_originalUpdateParamUI) return false;

    __try {
        const void* raw = nullptr;
        if (basic->AcquireSuite(kPFParamUtilsSuite, kPFParamUtilsSuiteVersion3, &raw) != A_Err_NONE ||
            !raw) {
            return false;
        }
        auto* suite = const_cast<PF_ParamUtilsSuite3*>(
            static_cast<const PF_ParamUtilsSuite3*>(raw));

        bool hooked = false;
        if (suite->PF_UpdateParamUI) {
            hooked = CreateHook(reinterpret_cast<void*>(suite->PF_UpdateParamUI),
                                reinterpret_cast<void*>(&HookedUpdateParamUI),
                                reinterpret_cast<void**>(&g_originalUpdateParamUI));
        }
        basic->ReleaseSuite(kPFParamUtilsSuite, kPFParamUtilsSuiteVersion3);
        return hooked;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

/* --- 入口函数包装 ------------------------------------------------------- */

template <int Id>
PF_Err WrappedEntryPoint(ULONG_PTR arg0, ULONG_PTR arg1, ULONG_PTR arg2,
                         ULONG_PTR arg3, ULONG_PTR arg4, ULONG_PTR arg5) {
    void* original = g_originalEntryPoints[Id].load(std::memory_order_acquire);
    if (!original) return PF_Err_NONE;

    /* AEGP 入口的第一个参数是 suite 指针，PF 入口的是 PF_Cmd，靠大小区分。 */
    if (arg0 > 0xFFFF) {
        using AegpEntry = PF_Err (*)(SPBasicSuite*, A_long, A_long, AEGP_PluginID, AEGP_GlobalRefcon*);
        return reinterpret_cast<AegpEntry>(original)(
            reinterpret_cast<SPBasicSuite*>(arg0),
            static_cast<A_long>(arg1), static_cast<A_long>(arg2),
            static_cast<AEGP_PluginID>(arg3),
            reinterpret_cast<AEGP_GlobalRefcon*>(arg4));
    }

    using EffectEntry = PF_Err (*)(PF_Cmd, PF_InData*, PF_OutData*, PF_ParamDef* [], PF_LayerDef*, void*);

    const PF_Cmd cmd = static_cast<PF_Cmd>(arg0);
    auto* inData = reinterpret_cast<PF_InData*>(arg1);
    auto* outData = reinterpret_cast<PF_OutData*>(arg2);
    auto** params = reinterpret_cast<PF_ParamDef**>(arg3);
    auto* output = reinterpret_cast<PF_LayerDef*>(arg4);
    void* extra = reinterpret_cast<void*>(arg5);

    if (cmd == PF_Cmd_PARAMS_SETUP && inData) {
        HookAddParamOnce(reinterpret_cast<void*>(inData->inter.add_param));
        if (inData->pica_basicP) HookUpdateParamUI(inData->pica_basicP);
    }

    const Direction previous = tlsDirection;
    tlsDirection = static_cast<Direction>(g_wrapperDirections[Id].load(std::memory_order_acquire));

    PF_Err result = reinterpret_cast<EffectEntry>(original)(cmd, inData, outData, params, output, extra);

    if (tlsDirection != Direction::None &&
        (cmd == PF_Cmd_UPDATE_PARAMS_UI || cmd == PF_Cmd_USER_CHANGED_PARAM)) {
        if (FixParamList(inData, params) > 0 && outData) {
            outData->out_flags |= PF_OutFlag_REFRESH_UI;
        }
    }

    tlsDirection = previous;
    return result;
}

constexpr int kWrapperCount = kWrapperPoolSize;

using EntryPointFn = PF_Err (*)(ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR);

#define WRAPPER_AT(n) &WrappedEntryPoint<n>
#define WRAPPER_8(n)  WRAPPER_AT(n), WRAPPER_AT(n+1), WRAPPER_AT(n+2), WRAPPER_AT(n+3), \
                      WRAPPER_AT(n+4), WRAPPER_AT(n+5), WRAPPER_AT(n+6), WRAPPER_AT(n+7)
#define WRAPPER_32(n) WRAPPER_8(n), WRAPPER_8(n+8), WRAPPER_8(n+16), WRAPPER_8(n+24)
#define WRAPPER_128(n) WRAPPER_32(n), WRAPPER_32(n+32), WRAPPER_32(n+64), WRAPPER_32(n+96)
#define WRAPPER_512(n) WRAPPER_128(n), WRAPPER_128(n+128), WRAPPER_128(n+256), WRAPPER_128(n+384)
#define WRAPPER_2048(n) WRAPPER_512(n), WRAPPER_512(n+512), WRAPPER_512(n+1024), WRAPPER_512(n+1536)

EntryPointFn g_wrapperPool[kWrapperPoolSize] = { WRAPPER_2048(0) };

/* --- 模块规则表 --------------------------------------------------------- */

struct ModuleRule {
    const wchar_t* pathFragment;   // lower-case, must appear in the module path
    Direction direction;
};

/* 老式模块：AE 会把它们的 ANSI 转成 UTF-8，所以要把它们自带的 UTF-8 转回 ANSI。
 * 这些条目已用 AE 26 实测核对。 */
constexpr ModuleRule kRules[] = {
    { L"\\trapcode\\geo",         Direction::Utf8ToAnsi },
    { L"\\trapcode\\form",        Direction::Utf8ToAnsi },
    { L"\\trapcode\\particular",  Direction::Utf8ToAnsi },
    { L"\\trapcode\\tao",         Direction::Utf8ToAnsi },
    { L"\\trapcode\\mir",         Direction::Utf8ToAnsi },
    { L"\\trapcode\\lux",         Direction::Utf8ToAnsi },
    { L"\\trapcode\\echospace",   Direction::Utf8ToAnsi },
    { L"\\trapcode\\horizon",     Direction::Utf8ToAnsi },

    /* BCC（Boris Continuum Complete）：新版自带 UTF-8 中文，同样是老式模块，
     * AE 27 会再转一次，所以整目录走 UTF-8 → ANSI。
     * 实测 7 个效果、1473 个含中文的属性名，全部可被 CP936 表示。 */
    { L"\\borisfx\\continuum",    Direction::Utf8ToAnsi },

    /* 新式模块却自带 ANSI 文本：AE 直接把字节当 UTF-8，所以要升成 UTF-8。 */
    { L"\\trapcode\\shine",       Direction::GbkToUtf8 },
    { L"\\trapcode\\starglow",    Direction::GbkToUtf8 },
    { L"\\trapcode\\3dstroke",    Direction::GbkToUtf8 },
    { L"\\trapcode\\soundkeys",   Direction::GbkToUtf8 },
};

/* --- 外置规则文件 -------------------------------------------------------
 * 与 .aex 同目录的 AECharsetFixer.rules.txt（可选）用来补充或覆盖上面的表，
 * 这样新增厂商不需要重新编译。格式：
 *
 *   # 注释
 *   U2A \borisfx\continuum      # 老式模块 + UTF-8 文本：UTF-8 -> ANSI
 *   G2U \somevendor\modern      # 新式模块 + ANSI 文本：ANSI -> UTF-8
 *
 * 路径片段会转成小写后做子串匹配；文件规则优先于内置表。 */
struct FileRule {
    std::wstring fragment;
    Direction direction;
};

const std::vector<FileRule>& FileRules() {
    static const std::vector<FileRule> rules = [] {
        std::vector<FileRule> parsed;

        wchar_t modulePath[MAX_PATH]{};
        HMODULE self = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCWSTR>(&FileRules), &self) || !self) {
            return parsed;
        }
        if (GetModuleFileNameW(self, modulePath, MAX_PATH) == 0) return parsed;

        std::wstring path(modulePath);
        const size_t slash = path.find_last_of(L"\\/");
        if (slash == std::wstring::npos) return parsed;
        path.resize(slash + 1);
        path += L"AECharsetFixer.rules.txt";

        FILE* file = nullptr;
        if (_wfopen_s(&file, path.c_str(), L"rb") != 0 || !file) {
            Log("规则文件未找到：%ls", path.c_str());
            return parsed;
        }

        char line[1024];
        while (fgets(line, sizeof(line), file)) {
            char* cursor = line;
            if (cursor[0] == '\xEF' && cursor[1] == '\xBB' && cursor[2] == '\xBF') cursor += 3;
            while (*cursor == ' ' || *cursor == '\t') ++cursor;
            if (*cursor == 0 || *cursor == '#' || *cursor == ';' ||
                *cursor == '\r' || *cursor == '\n') {
                continue;
            }

            char kind[8]{};
            char fragment[512]{};
            if (sscanf_s(cursor, "%7s %511[^\r\n]", kind, static_cast<unsigned int>(sizeof(kind)),
                         fragment, static_cast<unsigned int>(sizeof(fragment))) < 2) {
                continue;
            }

            Direction direction = Direction::None;
            if (_stricmp(kind, "U2A") == 0) {
                direction = Direction::Utf8ToAnsi;
            } else if (_stricmp(kind, "G2U") == 0) {
                direction = Direction::GbkToUtf8;
            }
            if (direction == Direction::None) continue;

            wchar_t wide[512]{};
            if (MultiByteToWideChar(CP_UTF8, 0, fragment, -1, wide, 512) <= 0) continue;
            CharLowerBuffW(wide, static_cast<DWORD>(wcslen(wide)));
            parsed.push_back(FileRule{ wide, direction });
        }
        fclose(file);
        Log("规则文件载入 %d 条：%ls", static_cast<int>(parsed.size()), path.c_str());
        return parsed;
    }();
    return rules;
}

bool MatchesSemicolonList(const std::wstring& path, const wchar_t* list, Direction direction,
                          Direction* out) noexcept {
    if (!list || !*list) return false;

    std::wstring token;
    for (const wchar_t* cursor = list;; ++cursor) {
        if (*cursor == L';' || *cursor == 0) {
            if (!token.empty() && path.find(token) != std::wstring::npos) {
                *out = direction;
                return true;
            }
            token.clear();
            if (*cursor == 0) break;
        } else {
            token.push_back(static_cast<wchar_t>(towlower(*cursor)));
        }
    }
    return false;
}

Direction DirectionForModule(const std::wstring& lowerPath) noexcept {
    Direction result = Direction::None;

    /* 环境变量覆盖，用于表格还没收录的模块：
     *   AEUTF8FIXER_ANSI = ";\路径片段;\另一个"   （UTF-8 转 ANSI）
     *   AEUTF8FIXER_GBK  = ";\路径片段"            （ANSI 转 UTF-8） */
    wchar_t ansiList[1024]{};
    wchar_t gbkList[1024]{};
    GetEnvironmentVariableW(L"AEUTF8FIXER_ANSI", ansiList, 1024);
    GetEnvironmentVariableW(L"AEUTF8FIXER_GBK", gbkList, 1024);

    if (MatchesSemicolonList(lowerPath, gbkList, Direction::GbkToUtf8, &result)) return result;
    if (MatchesSemicolonList(lowerPath, ansiList, Direction::Utf8ToAnsi, &result)) return result;

    for (const FileRule& rule : FileRules()) {
        if (lowerPath.find(rule.fragment) != std::wstring::npos) return rule.direction;
    }

    for (const ModuleRule& rule : kRules) {
        if (lowerPath.find(rule.pathFragment) != std::wstring::npos) return rule.direction;
    }
    return Direction::None;
}

/* --- 模块扫描 ----------------------------------------------------------- */

bool ReadExportDirectory(HMODULE module, IMAGE_EXPORT_DIRECTORY** exports) noexcept {
    if (!module || !exports) return false;

    __try {
        auto* base = reinterpret_cast<unsigned char*>(module);
        auto* dosHeader = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) return false;
        if (dosHeader->e_lfanew < static_cast<LONG>(sizeof(IMAGE_DOS_HEADER)) ||
            dosHeader->e_lfanew > 0x10000) {
            return false;
        }

        auto* ntHeaders = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dosHeader->e_lfanew);
        if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) return false;

        const IMAGE_DATA_DIRECTORY directory =
            ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        if (directory.Size == 0 || directory.VirtualAddress == 0) return false;
        if (directory.VirtualAddress >= ntHeaders->OptionalHeader.SizeOfImage) return false;

        *exports = reinterpret_cast<IMAGE_EXPORT_DIRECTORY*>(base + directory.VirtualAddress);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void HookModule(HMODULE module, const std::wstring& modulePath) noexcept {
    if (!module || modulePath.empty()) return;

    std::wstring lowerPath = modulePath;
    CharLowerBuffW(lowerPath.data(), static_cast<DWORD>(lowerPath.size()));

    if (lowerPath.find(L".aex") == std::wstring::npos &&
        lowerPath.find(L"\\plug-ins\\") == std::wstring::npos) {
        return;
    }

    const Direction direction = DirectionForModule(lowerPath);
    if (direction == Direction::None) return;

    IMAGE_EXPORT_DIRECTORY* exports = nullptr;
    if (!ReadExportDirectory(module, &exports)) return;

    auto* base = reinterpret_cast<unsigned char*>(module);
    auto* names = reinterpret_cast<DWORD*>(base + exports->AddressOfNames);
    auto* functions = reinterpret_cast<DWORD*>(base + exports->AddressOfFunctions);
    auto* ordinals = reinterpret_cast<WORD*>(base + exports->AddressOfNameOrdinals);

    std::lock_guard<std::mutex> lock(g_wrapperMutex);

    for (DWORD i = 0; i < exports->NumberOfNames; ++i) {
        const char* name = reinterpret_cast<const char*>(base + names[i]);
        /* 三种入口名都要认：AE 自己的例子用 EntryPointFunc/EffectMain，
         * BCC 这类第三方用 PluginMain，只认前两个会整片漏钩。 */
        if (strcmp(name, "EntryPointFunc") != 0 && strcmp(name, "EffectMain") != 0 &&
            strcmp(name, "PluginMain") != 0) {
            continue;
        }

        void* target = reinterpret_cast<void*>(base + functions[ordinals[i]]);

        bool alreadyHooked = false;
        const int used = g_wrapperCount.load(std::memory_order_relaxed);
        for (int slot = 0; slot < used; ++slot) {
            if (g_originalEntryPoints[slot].load(std::memory_order_relaxed) == target) {
                alreadyHooked = true;
                break;
            }
        }
        if (alreadyHooked || used >= kWrapperPoolSize) continue;

        void* trampoline = nullptr;
        if (!CreateHook(target, reinterpret_cast<void*>(g_wrapperPool[used]),
                        reinterpret_cast<void**>(&trampoline))) {
            continue;
        }

        g_originalEntryPoints[used].store(trampoline, std::memory_order_release);
        g_wrapperDirections[used].store(static_cast<int>(direction), std::memory_order_release);
        g_wrapperCount.fetch_add(1, std::memory_order_release);
        Log("hooked %ls dir=%d", modulePath.c_str(), static_cast<int>(direction));
    }
}

void HookLoadedModules() noexcept {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
                                               GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE) return;

    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Module32FirstW(snapshot, &entry)) {
        do {
            HookModule(entry.hModule, entry.szExePath);
        } while (Module32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
}

/* --- 加载器钩子：抓住在本插件之后才载入的模块 --------------------------- */

using LoadLibraryWFn = HMODULE(WINAPI*)(LPCWSTR);
using LoadLibraryExWFn = HMODULE(WINAPI*)(LPCWSTR, HANDLE, DWORD);

LoadLibraryWFn g_originalLoadLibraryW = nullptr;
LoadLibraryExWFn g_originalLoadLibraryExW = nullptr;

HMODULE WINAPI HookedLoadLibraryW(LPCWSTR fileName) {
    HMODULE module = g_originalLoadLibraryW(fileName);
    if (module && fileName) HookModule(module, fileName);
    return module;
}

HMODULE WINAPI HookedLoadLibraryExW(LPCWSTR fileName, HANDLE file, DWORD flags) {
    HMODULE module = g_originalLoadLibraryExW(fileName, file, flags);
    if (module && fileName) HookModule(module, fileName);
    return module;
}

void InstallLoaderHooks() noexcept {
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (!kernel32) return;

    if (void* target = reinterpret_cast<void*>(GetProcAddress(kernel32, "LoadLibraryW"))) {
        CreateHook(target, reinterpret_cast<void*>(&HookedLoadLibraryW),
                   reinterpret_cast<void**>(&g_originalLoadLibraryW));
    }
    if (void* target = reinterpret_cast<void*>(GetProcAddress(kernel32, "LoadLibraryExW"))) {
        CreateHook(target, reinterpret_cast<void*>(&HookedLoadLibraryExW),
                   reinterpret_cast<void**>(&g_originalLoadLibraryExW));
    }
}

/* --- PiPL 入口 ---------------------------------------------------------- */

PF_Err Initialize() noexcept {
    const MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
        Log("MinHook init failed status=%d", static_cast<int>(status));
        return PF_Err_NONE;
    }

    InstallLoaderHooks();
    HookLoadedModules();
    Log("ready (%d modules hooked)", g_wrapperCount.load(std::memory_order_relaxed));
    return PF_Err_NONE;
}

}  // namespace

#if defined(_MSC_VER)
#define FIXER_EXPORT extern "C" __declspec(dllexport)
#else
#define FIXER_EXPORT extern "C" __attribute__((visibility("default")))
#endif

FIXER_EXPORT PF_Err EntryPointFunc(SPBasicSuite* basic,
                                   A_long majorVersion,
                                   A_long minorVersion,
                                   AEGP_PluginID pluginId,
                                   AEGP_GlobalRefcon* globalRefcon) {
    (void)basic;
    (void)majorVersion;
    (void)minorVersion;
    (void)pluginId;
    (void)globalRefcon;
    return Initialize();
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
    } else if (reason == DLL_PROCESS_DETACH) {
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
    }
    return TRUE;
}



