// ==WindhawkMod==
// @id              llm-liquid-glass-hud
// @name            LLM Liquid Glass HUD (Anime Cat Buddy & Spotlight Bar)
// @description     Cute Anime Cat Buddies (Orange Cat for Claude, Black Cat for Codex, Green Cat for Gemini) that pop up in the very middle of your screen, hover playfully for 2 seconds, then smoothly glide to the top-left corner for 30s. Clicking brings the LLM to the top.
// @version         1.8.0
// @author          kg-sato
// @github          https://github.com/kg-sato/LLM-Liquid-Glass
// @include         explorer.exe
// @include         Antigravity.exe
// @include         Cursor.exe
// @include         Code.exe
// @include         Claude.exe
// @include         Codex.exe
// @include         codex.exe
// @include         WindowsTerminal.exe
// @architecture    x86-64
// @compilerOptions -lgdi32 -luser32 -ldwmapi -lgdiplus -lole32 -lshell32 -lshlwapi -ladvapi32
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# LLM Liquid Glass HUD

An Apple-inspired **Anime Cat Buddy & Liquid Glass HUD** that surfaces when an LLM or coding agent finishes a cycle.

## Key Features
- **3 Unique Anime Cat Popups (One per LLM)**:
  - **Claude (Orange)**: Adorable orange kitten nestled in an orange peel with little leaf and tail (`cat_claude_orange.png`).
  - **Codex (Black)**: Charming fluffy black kitten with big eyes, red bow collar, and white ruff (`cat_codex_black.png`).
  - **Gemini / Antigravity (Green)**: Cute lime-green cat with a lychee hairclip and fresh limes (`cat_gemini_green.png`).
- **Very Middle 2-Second Hover & Top-Left Glide**:
  - Pops up directly in the **very middle** of your active screen/monitor.
  - Hovers playfully with gentle head-tilt and breathing bounce for **2.0 seconds**.
  - Smoothly glides to the **top-left corner** in a zero-gravity hover drift with directional motion blur and optical depth-of-field focus.
  - Perches in the top-left for **30 seconds** with an orbital countdown halo ring before softly blurring and fading away.
  - **Click to Focus**: Clicking the cat at any point brings the finished LLM window and exact chat session to the top!
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- robotBuddyMode: true
  $name: Anime Cat Buddy Icon Mode
  $description: On (default) = Cute anime cat buddy pops up in the very middle for 2s (Orange for Claude, Black for Codex, Green for Gemini), then glides to the top-left for 30s (click to bring LLM to top). Off = Full bottom Spotlight glass bar.
- autoDismissSeconds: 30
  $name: Auto-Dismiss Timeout (seconds)
  $description: How long the Cat Buddy perches in the top-left before fading away (default 30 seconds).
- openWindowMode: false
  $name: Open LLM Window on Click (Spotlight Bar Mode)
  $description: Off (default) = Read full summary/options on the HUD and reply inline without leaving your active window. On = Clicking the notification or an option opens and switches to the LLM window and chat.
- keepOnPreviousSide: true
  $name: Keep Window & HUD on Previous Monitor / Side (Multi-Monitor)
  $description: Keeps the LLM window on the monitor and side-snap position it was already on, and remembers any monitor/side you drag the HUD notification to.
- glassOpacity: 176
  $name: Glass Surface Opacity (0-255)
  $description: Opacity of the frosted Apple Liquid Glass tint (lower = more translucent).
- bottomOffsetPx: 64
  $name: Bottom Offset from Screen Edge (px)
  $description: Distance above the bottom of the monitor in Spotlight Bar Mode.
- hudWidthPx: 540
  $name: HUD Width (px)
  $description: Width of the Spotlight-style floating glass bar in Spotlight Bar Mode.
- enableHotkeys: true
  $name: Enable Alt+1..4 Quick Option Hotkeys
  $description: Press Alt+1, Alt+2, Alt+3, or Alt+4 while the HUD is visible to answer without touching the mouse.
- interceptToasts: true
  $name: Intercept LLM App Notifications & Taskbar Flashes
  $description: Automatically trigger the HUD when Antigravity, Cursor, VS Code, Claude, Codex, or Terminal fires a notification or flashes on cycle completion.
*/
// ==/WindhawkModSettings==

#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <aclapi.h>
#include <sddl.h>
#include <objidl.h>
#include <gdiplus.h>
#include <cmath>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <algorithm>

using namespace Gdiplus;

struct ACCENT_POLICY {
    int AccentState;
    int AccentFlags;
    DWORD GradientColor;
    int AnimationId;
};

struct WINDOWCOMPOSITIONATTRIBDATA {
    DWORD Attrib;
    PVOID pvData;
    SIZE_T cbData;
};

using pfnSetWindowCompositionAttribute = BOOL(WINAPI*)(HWND, WINDOWCOMPOSITIONATTRIBDATA*);

enum ACCENT_STATE {
    ACCENT_DISABLED = 0,
    ACCENT_ENABLE_BLURBEHIND = 3,
    ACCENT_ENABLE_ACRYLICBLURBEHIND = 4,
    ACCENT_ENABLE_HOSTBACKDROP = 5
};

struct ModSettings {
    bool robotBuddyMode = true;
    int autoDismissSeconds = 30;
    bool openWindowMode = false;
    bool keepOnPreviousSide = true;
    int glassOpacity = 176;
    int bottomOffsetPx = 64;
    int hudWidthPx = 540;
    bool enableHotkeys = true;
    bool interceptToasts = true;
} g_settings;

struct OptionEntry {
    std::wstring shortSubtitle;
    std::wstring fullText;
    bool isLarge = false;
};

struct LLMCyclePayload {
    std::wstring sourceApp = L"Antigravity";
    std::wstring modelBadge = L"Cycle Complete";
    std::wstring title = L"Agent finished turn";
    std::wstring summaryShort = L"Ready for your next instruction or option selection.";
    std::wstring summaryFull = L"Ready for your next instruction or option selection.";
    bool summaryIsLarge = false;
    std::vector<OptionEntry> options;
    HWND targetWindow = nullptr;
    std::wstring targetSessionId;
};

static bool g_isExplorerHost = false;
static std::atomic<bool> g_shuttingDown{false};
static HWND g_hudHwnd = nullptr;
static HANDLE g_pipeThread = nullptr;
static ULONG_PTR g_gdiplusToken = 0;
static std::mutex g_payloadMutex;
static LLMCyclePayload g_currentPayload;
static LONGLONG g_lastRichPayloadQpc = 0;

// ============================================================================
// Security, Privacy & Smart Subtitle Extraction Helpers
// ============================================================================

static std::wstring SanitizeUntrustedString(const std::wstring& input, size_t maxLen) {
    std::wstring out;
    out.reserve(std::min(input.size(), maxLen));
    for (wchar_t ch : input) {
        if (out.size() >= maxLen) break;
        if (ch == L'\r' || ch == L'\n' || ch == L'\t') {
            if (!out.empty() && out.back() != L' ') out.push_back(L' ');
            continue;
        }
        if (ch < 0x20 || (ch >= 0x7F && ch <= 0x9F) ||
            (ch >= 0x202A && ch <= 0x202E) || (ch >= 0x2066 && ch <= 0x2069)) {
            continue;
        }
        out.push_back(ch);
    }
    return out;
}

static void SplitSubtitleAndFullText(
    const std::wstring& rawInput,
    size_t maxSubtitleChars,
    size_t maxFullChars,
    std::wstring& outSubtitle,
    std::wstring& outFull,
    bool& outIsLarge
) {
    std::wstring cleaned = SanitizeUntrustedString(rawInput, maxFullChars);
    outFull = cleaned;
    outIsLarge = false;

    size_t dblColon = cleaned.find(L"::");
    if (dblColon != std::wstring::npos && dblColon > 2 && dblColon <= maxSubtitleChars + 8) {
        outSubtitle = cleaned.substr(0, dblColon);
        while (!outSubtitle.empty() && outSubtitle.back() == L' ') outSubtitle.pop_back();
        std::wstring remainder = cleaned.substr(dblColon + 2);
        size_t firstNonSpace = remainder.find_first_not_of(L' ');
        outFull = (firstNonSpace != std::wstring::npos)
            ? (outSubtitle + L" — " + remainder.substr(firstNonSpace))
            : outSubtitle;
        outIsLarge = (outFull.size() > outSubtitle.size());
        return;
    }

    if (cleaned.size() <= maxSubtitleChars) {
        outSubtitle = cleaned;
        outIsLarge = false;
        return;
    }

    outIsLarge = true;

    const wchar_t* delimiters[] = { L": ", L" — ", L" – ", L" - ", L" (" };
    for (const wchar_t* delim : delimiters) {
        size_t pos = cleaned.find(delim);
        if (pos != std::wstring::npos && pos >= 4 && pos <= maxSubtitleChars) {
            outSubtitle = cleaned.substr(0, pos);
            return;
        }
    }

    size_t cut = maxSubtitleChars - 1;
    size_t lastSpace = cleaned.rfind(L' ', cut);
    if (lastSpace != std::wstring::npos && lastSpace >= maxSubtitleChars / 2) {
        cut = lastSpace;
    }
    outSubtitle = cleaned.substr(0, cut) + L"\x2026";
}

static bool CreateOwnerOnlySecurityDescriptor(PSECURITY_DESCRIPTOR* ppSD) {
    *ppSD = nullptr;
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        return false;
    }

    DWORD tokenInfoLen = 0;
    GetTokenInformation(hToken, TokenUser, nullptr, 0, &tokenInfoLen);
    if (tokenInfoLen == 0) {
        CloseHandle(hToken);
        return false;
    }

    std::vector<BYTE> tokenUserBuf(tokenInfoLen);
    if (!GetTokenInformation(hToken, TokenUser, tokenUserBuf.data(), tokenInfoLen, &tokenInfoLen)) {
        CloseHandle(hToken);
        return false;
    }
    CloseHandle(hToken);

    PSID pUserSid = reinterpret_cast<PTOKEN_USER>(tokenUserBuf.data())->User.Sid;
    LPWSTR sidStr = nullptr;
    if (!ConvertSidToStringSidW(pUserSid, &sidStr)) {
        return false;
    }

    std::wstring sddl = L"D:P(A;;GA;;;" + std::wstring(sidStr) + L")";
    LocalFree(sidStr);

    return ConvertStringSecurityDescriptorToSecurityDescriptorW(
        sddl.c_str(), SDDL_REVISION_1, ppSD, nullptr) != FALSE;
}

static bool IsAuthorizedTargetWindow(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return false;

    DWORD targetPid = 0;
    GetWindowThreadProcessId(hwnd, &targetPid);
    if (targetPid == 0) return false;

    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, targetPid);
    if (!hProc) return false;

    wchar_t exePath[MAX_PATH] = {};
    DWORD cchPath = MAX_PATH;
    bool allowedExe = false;
    if (QueryFullProcessImageNameW(hProc, 0, exePath, &cchPath)) {
        const wchar_t* baseName = PathFindFileNameW(exePath);
        static const wchar_t* kAllowedProcesses[] = {
            L"Antigravity.exe", L"Cursor.exe", L"Code.exe", L"Claude.exe",
            L"Codex.exe", L"codex.exe", L"opencode-cli.exe",
            L"WindowsTerminal.exe", L"pwsh.exe", L"powershell.exe", L"cmd.exe",
            L"conhost.exe", L"OpenConsole.exe", L"wezterm-gui.exe", L"alacritty.exe"
        };
        for (const wchar_t* allowed : kAllowedProcesses) {
            if (_wcsicmp(baseName, allowed) == 0) {
                allowedExe = true;
                break;
            }
        }
    }
    CloseHandle(hProc);
    return allowedExe;
}

static void SetEphemeralClipboardText(HWND ownerHwnd, const std::wstring& text) {
    if (!OpenClipboard(ownerHwnd)) return;
    EmptyClipboard();

    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem) {
        void* ptr = GlobalLock(hMem);
        if (ptr) {
            memcpy(ptr, text.c_str(), bytes);
            GlobalUnlock(hMem);
            if (!SetClipboardData(CF_UNICODETEXT, hMem)) {
                GlobalFree(hMem);
            }
        } else {
            GlobalFree(hMem);
        }
    }

    static UINT cfExcludeMonitor = RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing");
    static UINT cfCanIncludeHistory = RegisterClipboardFormatW(L"CanIncludeInClipboardHistory");
    static UINT cfCanUploadCloud = RegisterClipboardFormatW(L"CanUploadToCloudClipboard");

    auto setDwordFormat = [](UINT fmt, DWORD val) {
        if (!fmt) return;
        HGLOBAL hFlag = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
        if (hFlag) {
            DWORD* p = static_cast<DWORD*>(GlobalLock(hFlag));
            if (p) {
                *p = val;
                GlobalUnlock(hFlag);
                if (!SetClipboardData(fmt, hFlag)) GlobalFree(hFlag);
            } else {
                GlobalFree(hFlag);
            }
        }
    };

    setDwordFormat(cfExcludeMonitor, 1);
    setDwordFormat(cfCanIncludeHistory, 0);
    setDwordFormat(cfCanUploadCloud, 0);

    CloseClipboard();
}

struct Spring1D {
    float value = 0.0f;
    float target = 0.0f;
    float velocity = 0.0f;

    void Reset(float initial, float tgt, float vel = 0.0f) {
        value = initial;
        target = tgt;
        velocity = vel;
    }

    bool Step(float dt, float omega = 26.0f, float zeta = 1.0f) {
        float disp = value - target;
        if (std::fabs(disp) < 0.0008f && std::fabs(velocity) < 0.008f) {
            value = target;
            velocity = 0.0f;
            return false;
        }
        int substeps = (dt > 0.007f) ? 2 : 1;
        float subDt = dt / static_cast<float>(substeps);
        const float damping = 2.0f * zeta * omega;
        for (int s = 0; s < substeps; ++s) {
            float d = value - target;
            float accel = -omega * omega * d - damping * velocity;
            velocity += accel * subDt;
            value += velocity * subDt;
        }
        return true;
    }
};

struct PointerSample {
    float y = 0.0f;
    LONGLONG qpcTicks = 0;
};

static const int kPopoverCanvasReservePx = 152;
static const int kBuddyCanvasSize = 220;

struct HudChoreography {
    Spring1D ySpring;
    Spring1D scaleSpring;
    Spring1D opacitySpring;
    Spring1D heightSpring;
    Spring1D contentFadeSpring;
    Spring1D pillScale[4];
    Spring1D pillHover[4];
    Spring1D pillEntrance[4];

    // Futuristic Robot Buddy 120fps springs & flight state
    Spring1D buddyXSpring;
    Spring1D buddyYSpring;
    Spring1D buddyScaleSpring;
    Spring1D buddyOpacitySpring;
    Spring1D buddyTiltSpring;
    Spring1D buddyBlurSpring;
    Spring1D buddyHoverSpring;
    float buddyCenterX = 0.0f;
    float buddyCenterY = 0.0f;
    float buddyCornerX = 24.0f;
    float buddyCornerY = 24.0f;
    float buddyShakeX = 0.0f;
    float buddyHopY = 0.0f;
    bool buddyLaunchedToCorner = false;
    bool buddyHovered = false;
    bool hasCustomBuddyPerch = false;
    float customBuddyPerchX = 24.0f;
    float customBuddyPerchY = 24.0f;

    Spring1D popoverProgress;
    Spring1D popoverAnchorX;
    std::wstring popoverHeading;
    std::wstring popoverBody;
    bool popoverActive = false;

    bool visible = false;
    bool dismissing = false;
    float yZeta = 1.0f;

    bool isDragging = false;
    POINT dragStartScreenPt = {0, 0};
    float dragStartOffsetY = 0.0f;
    float dragOffsetX = 0.0f;
    float dragStartOffsetX = 0.0f;
    bool hasCustomPosition = false;
    int customScreenX = 0;
    HMONITOR customMonitor = nullptr;
    PointerSample history[4] = {};
    int historyCount = 0;

    int hoveredOption = -1;
    bool hoveredSummary = false;
    int pressedOption = -1;
    bool replyBoxFocused = false;
    std::wstring replyDraft;
    LONGLONG lastFrameQpc = 0;
    LONGLONG showQpc = 0;
    LONGLONG qpcFreq = 1;

    void InitSprings() {
        QueryPerformanceFrequency(reinterpret_cast<LARGE_INTEGER*>(&qpcFreq));
        for (int i = 0; i < 4; ++i) {
            pillScale[i].Reset(1.0f, 1.0f);
            pillHover[i].Reset(0.0f, 0.0f);
            pillEntrance[i].Reset(1.0f, 1.0f);
        }
        contentFadeSpring.Reset(1.0f, 1.0f);
        popoverProgress.Reset(0.0f, 0.0f);
        popoverAnchorX.Reset(270.0f, 270.0f);
    }

    bool IsWithinClickjackGraceWindow() const {
        if (showQpc == 0 || qpcFreq == 0) return false;
        LONGLONG now = 0;
        QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&now));
        double elapsedMs = (static_cast<double>(now - showQpc) * 1000.0) / static_cast<double>(qpcFreq);
        return elapsedMs < 220.0;
    }

    void PushPointerSample(float screenY) {
        LONGLONG now = 0;
        QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&now));
        for (int i = 3; i > 0; --i) {
            history[i] = history[i - 1];
        }
        history[0] = {screenY, now};
        if (historyCount < 4) historyCount++;
    }

    float ComputeReleaseVelocityPxPerSec() const {
        if (historyCount < 2) return 0.0f;
        int oldest = historyCount - 1;
        double dtSec = static_cast<double>(history[0].qpcTicks - history[oldest].qpcTicks) /
                       static_cast<double>(qpcFreq);
        if (dtSec <= 0.0005 || dtSec > 0.25) return 0.0f;
        return static_cast<float>((history[0].y - history[oldest].y) / dtSec);
    }
} g_anim;

static const UINT_PTR TIMER_SPRING_FRAME = 1001;
static const UINT_PTR TIMER_AUTO_DISMISS = 1002;
static const UINT WM_APP_SHOW_LLM_HUD = WM_APP + 0x42;
static const wchar_t* kPipeName = L"\\\\.\\pipe\\WindhawkLLMGlassHUD";
static const wchar_t* kHudClassName = L"Windhawk_AppleLiquidGlassHUD";

static bool IsReducedMotionEnabled() {
    BOOL clientAnim = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &clientAnim, 0)) {
        return !clientAnim;
    }
    return false;
}

static float RubberBand(float overshoot, float dimension, float constant = 0.55f) {
    return (overshoot * dimension * constant) / (dimension + constant * std::fabs(overshoot));
}

static float ProjectMomentum(float initialVelocityPxPerSec, float decelerationRate = 0.998f) {
    return (initialVelocityPxPerSec / 1000.0f) * decelerationRate / (1.0f - decelerationRate);
}

static OptionEntry MakeOptionEntry(const std::wstring& rawOpt) {
    OptionEntry entry;
    SplitSubtitleAndFullText(rawOpt, 26, 650, entry.shortSubtitle, entry.fullText, entry.isLarge);
    return entry;
}

static bool IsValidClaudeLocalSessionId(const std::wstring& s) {
    if (s.size() < 10 || s.size() > 70) return false;
    if (s.compare(0, 6, L"local_") != 0) return false;
    for (size_t i = 6; i < s.size(); ++i) {
        wchar_t ch = s[i];
        bool ok = (ch >= L'0' && ch <= L'9') ||
                  (ch >= L'a' && ch <= L'z') ||
                  (ch >= L'A' && ch <= L'Z') ||
                  ch == L'-' || ch == L'_';
        if (!ok) return false;
    }
    return true;
}

static std::wstring ReadLastFocusedSessionFromLogFile(const std::wstring& logPath) {
    HANDLE hFile = CreateFileW(
        logPath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (hFile == INVALID_HANDLE_VALUE) {
        return L"";
    }

    LARGE_INTEGER fileSize = {};
    if (!GetFileSizeEx(hFile, &fileSize) || fileSize.QuadPart <= 0) {
        CloseHandle(hFile);
        return L"";
    }

    const LONGLONG kMaxTailBytes = 65536;
    LARGE_INTEGER seekPos = {};
    if (fileSize.QuadPart > kMaxTailBytes) {
        seekPos.QuadPart = fileSize.QuadPart - kMaxTailBytes;
    }
    SetFilePointerEx(hFile, seekPos, nullptr, FILE_BEGIN);

    DWORD toRead = static_cast<DWORD>(std::min<LONGLONG>(fileSize.QuadPart - seekPos.QuadPart, kMaxTailBytes));
    std::string buf(toRead, '\0');
    DWORD bytesRead = 0;
    BOOL ok = ReadFile(hFile, buf.data(), toRead, &bytesRead, nullptr);
    CloseHandle(hFile);
    if (!ok || bytesRead == 0) {
        return L"";
    }
    buf.resize(bytesRead);

    const char* marker = "LocalSessions.setFocusedSession: sessionId=local_";
    size_t pos = buf.rfind(marker);
    if (pos == std::string::npos) {
        return L"";
    }

    size_t idStart = pos + strlen("LocalSessions.setFocusedSession: sessionId=");
    size_t idEnd = idStart;
    while (idEnd < buf.size()) {
        char c = buf[idEnd];
        bool validChar = (c >= '0' && c <= '9') ||
                         (c >= 'a' && c <= 'z') ||
                         (c >= 'A' && c <= 'Z') ||
                         c == '-' || c == '_';
        if (!validChar) break;
        idEnd++;
    }

    std::string narrowId = buf.substr(idStart, idEnd - idStart);
    std::wstring wideId(narrowId.begin(), narrowId.end());
    return IsValidClaudeLocalSessionId(wideId) ? wideId : L"";
}

static std::wstring GetClaudeDesktopActiveSessionId() {
    wchar_t localAppData[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH) == 0) {
        return L"";
    }
    std::wstring baseDir = std::wstring(localAppData) + L"\\Claude\\logs\\";
    std::wstring id = ReadLastFocusedSessionFromLogFile(baseDir + L"main.log");
    if (!id.empty()) {
        return id;
    }
    return ReadLastFocusedSessionFromLogFile(baseDir + L"main1.log");
}

static LLMCyclePayload ParsePipeMessage(const std::wstring& raw) {
    LLMCyclePayload p;
    std::vector<std::wstring> parts;
    size_t start = 0;
    while (true) {
        size_t pos = raw.find(L'|', start);
        if (pos == std::wstring::npos) {
            parts.push_back(raw.substr(start));
            break;
        }
        parts.push_back(raw.substr(start, pos - start));
        start = pos + 1;
    }

    if (parts.size() >= 1 && !parts[0].empty()) p.sourceApp = SanitizeUntrustedString(parts[0], 40);
    if (parts.size() >= 2 && !parts[1].empty()) p.modelBadge = SanitizeUntrustedString(parts[1], 36);
    if (parts.size() >= 3 && !parts[2].empty()) p.title = SanitizeUntrustedString(parts[2], 96);
    if (parts.size() >= 4 && !parts[3].empty()) {
        SplitSubtitleAndFullText(parts[3], 68, 900, p.summaryShort, p.summaryFull, p.summaryIsLarge);
    }
    if (parts.size() >= 5 && !parts[4].empty()) {
        std::wstring targetField = parts[4];
        std::wstring hwndPart = targetField;
        size_t atPos = targetField.find(L'@');
        if (atPos != std::wstring::npos) {
            hwndPart = targetField.substr(0, atPos);
            std::wstring candidateSession = targetField.substr(atPos + 1);
            if (IsValidClaudeLocalSessionId(candidateSession)) {
                p.targetSessionId = candidateSession;
            }
        }
        ULONG_PTR hwndVal = wcstoull(hwndPart.c_str(), nullptr, 16);
        HWND candidate = reinterpret_cast<HWND>(hwndVal);
        if (IsAuthorizedTargetWindow(candidate)) {
            p.targetWindow = candidate;
        }
    }
    if (parts.size() >= 6 && !parts[5].empty()) {
        size_t oStart = 0;
        const std::wstring& optsStr = parts[5];
        while (p.options.size() < 4) {
            size_t semi = optsStr.find(L';', oStart);
            std::wstring item = (semi == std::wstring::npos)
                ? optsStr.substr(oStart)
                : optsStr.substr(oStart, semi - oStart);
            OptionEntry entry = MakeOptionEntry(item);
            if (!entry.shortSubtitle.empty()) {
                p.options.push_back(entry);
            }
            if (semi == std::wstring::npos) break;
            oStart = semi + 1;
        }
    }
    return p;
}

static void ExtractOptionsFromText(
    const std::wstring& text,
    std::wstring& outSummaryShort,
    std::wstring& outSummaryFull,
    bool& outSummaryIsLarge,
    std::vector<OptionEntry>& outOptions
) {
    outOptions.clear();
    std::vector<std::wstring> lines;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find_first_of(L"\r\n", start);
        if (end == std::wstring::npos) {
            lines.push_back(text.substr(start));
            break;
        }
        if (end > start) {
            lines.push_back(text.substr(start, end - start));
        }
        start = end + 1;
    }

    std::wstring summaryAccum;
    std::wstring lastNonOptLine;
    std::vector<OptionEntry> candidateOpts;
    std::vector<int> candidateNums;

    auto flushCandidateBlock = [&]() {
        if (candidateOpts.size() >= 2 && candidateOpts.size() <= 4) {
            bool sequential = true;
            for (size_t i = 0; i < candidateNums.size(); ++i) {
                if (candidateNums[i] != static_cast<int>(i + 1)) {
                    sequential = false;
                    break;
                }
            }
            bool hasChoiceCue =
                (lastNonOptLine.find(L'?') != std::wstring::npos) ||
                (StrStrIW(lastNonOptLine.c_str(), L"pick") != nullptr) ||
                (StrStrIW(lastNonOptLine.c_str(), L"choose") != nullptr) ||
                (StrStrIW(lastNonOptLine.c_str(), L"select") != nullptr) ||
                (StrStrIW(lastNonOptLine.c_str(), L"option") != nullptr) ||
                (StrStrIW(lastNonOptLine.c_str(), L"which") != nullptr) ||
                (StrStrIW(lastNonOptLine.c_str(), L"prefer") != nullptr) ||
                (StrStrIW(lastNonOptLine.c_str(), L"ways") != nullptr);
            bool isInfoHeader =
                (StrStrIW(lastNonOptLine.c_str(), L"next") == lastNonOptLine.c_str()) ||
                (StrStrIW(lastNonOptLine.c_str(), L"step") == lastNonOptLine.c_str()) ||
                (StrStrIW(lastNonOptLine.c_str(), L"how ") == lastNonOptLine.c_str()) ||
                (StrStrIW(lastNonOptLine.c_str(), L"what ") == lastNonOptLine.c_str());
            if (sequential && hasChoiceCue && !isInfoHeader) {
                outOptions = candidateOpts;
            }
        }
        candidateOpts.clear();
        candidateNums.clear();
    };

    for (const auto& line : lines) {
        size_t firstNonSpace = line.find_first_not_of(L" \t");
        if (firstNonSpace == std::wstring::npos) continue;
        std::wstring trimmed = line.substr(firstNonSpace);

        if (firstNonSpace <= 1 && candidateOpts.size() < 4 && trimmed.size() > 3 &&
            ((trimmed[0] >= L'1' && trimmed[0] <= L'4') || (trimmed[0] >= L'A' && trimmed[0] <= L'D')) &&
            (trimmed[1] == L')' || trimmed[1] == L'.' || trimmed[1] == L':')) {
            size_t optStart = trimmed.find_first_not_of(L" \t", 2);
            if (optStart != std::wstring::npos) {
                int num = (trimmed[0] >= L'1' && trimmed[0] <= L'4')
                    ? (trimmed[0] - L'0')
                    : (trimmed[0] - L'A' + 1);
                OptionEntry entry = MakeOptionEntry(trimmed.substr(optStart));
                if (!entry.shortSubtitle.empty()) {
                    candidateOpts.push_back(entry);
                    candidateNums.push_back(num);
                }
                continue;
            }
        }
        flushCandidateBlock();
        lastNonOptLine = trimmed;
        if (!summaryAccum.empty()) summaryAccum += L" ";
        summaryAccum += trimmed;
    }
    flushCandidateBlock();

    std::wstring rawSummary = summaryAccum.empty() ? text : summaryAccum;
    SplitSubtitleAndFullText(rawSummary, 68, 900, outSummaryShort, outSummaryFull, outSummaryIsLarge);
}

static void DispatchToGlassHUD(const LLMCyclePayload& payload) {
    std::wstring serialized = payload.sourceApp + L"|" +
                              payload.modelBadge + L"|" +
                              payload.title + L"|" +
                              payload.summaryFull + L"|";
    wchar_t hwndBuf[32] = {};
    swprintf_s(hwndBuf, L"%llX", reinterpret_cast<unsigned long long>(payload.targetWindow));
    serialized += hwndBuf;
    if (IsValidClaudeLocalSessionId(payload.targetSessionId)) {
        serialized += L"@";
        serialized += payload.targetSessionId;
    }
    serialized += L"|";
    for (size_t i = 0; i < payload.options.size(); ++i) {
        if (i > 0) serialized += L";";
        serialized += payload.options[i].fullText;
    }

    if (g_isExplorerHost && g_hudHwnd) {
        {
            std::lock_guard<std::mutex> lock(g_payloadMutex);
            g_currentPayload = payload;
        }
        PostMessageW(g_hudHwnd, WM_APP_SHOW_LLM_HUD, 0, 0);
        return;
    }

    HANDLE hPipe = CreateFileW(
        kPipeName,
        GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION,
        nullptr
    );
    if (hPipe != INVALID_HANDLE_VALUE) {
        DWORD bytesWritten = 0;
        WriteFile(hPipe, serialized.c_str(), static_cast<DWORD>(serialized.size() * sizeof(wchar_t)), &bytesWritten, nullptr);
        CloseHandle(hPipe);
    }
}

static void ApplyAppleGlassBackdrop(HWND hwnd) {
    BOOL darkMode = TRUE;
    DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &darkMode, sizeof(darkMode));
}

static int ComputeTargetHudHeight(const LLMCyclePayload& p) {
    if (!p.options.empty()) {
        int rows = static_cast<int>((p.options.size() + 1) / 2);
        return 104 + rows * 44;
    }
    return 132;
}

static void SendAnswerToLLM(const std::wstring& rawAnswerText, bool focusWindowOnly) {
    std::wstring answerText = SanitizeUntrustedString(rawAnswerText, 650);
    if (answerText.empty() && !focusWindowOnly) return;

    HWND target = nullptr;
    std::wstring targetSessionId;
    {
        std::lock_guard<std::mutex> lock(g_payloadMutex);
        target = g_currentPayload.targetWindow;
        targetSessionId = g_currentPayload.targetSessionId;
    }

    std::wstring savedClipboard;
    bool hadSavedClipboard = false;
    if (!focusWindowOnly && OpenClipboard(g_hudHwnd)) {
        HANDLE hExisting = GetClipboardData(CF_UNICODETEXT);
        if (hExisting) {
            const wchar_t* pStr = static_cast<const wchar_t*>(GlobalLock(hExisting));
            if (pStr) {
                savedClipboard = pStr;
                hadSavedClipboard = true;
                GlobalUnlock(hExisting);
            }
        }
        CloseClipboard();
    }

    if (!answerText.empty()) {
        SetEphemeralClipboardText(g_hudHwnd, answerText);
    }

    if (!IsAuthorizedTargetWindow(target)) {
        return;
    }

    bool hasClaudeSession = IsValidClaudeLocalSessionId(targetSessionId);
    bool wasMinimized = IsIconic(target) != FALSE;
    RECT rcBefore = {};
    BOOL hadRect = GetWindowRect(target, &rcBefore);

    // Preserve exact monitor & side-snap position on multi-monitor setups
    auto restoreSideIfMoved = [&]() {
        if (!g_settings.keepOnPreviousSide || wasMinimized || !hadRect) return;
        RECT rcAfter = {};
        if (GetWindowRect(target, &rcAfter)) {
            if (rcAfter.left != rcBefore.left || rcAfter.top != rcBefore.top ||
                rcAfter.right != rcBefore.right || rcAfter.bottom != rcBefore.bottom) {
                SetWindowPos(
                    target,
                    nullptr,
                    rcBefore.left,
                    rcBefore.top,
                    rcBefore.right - rcBefore.left,
                    rcBefore.bottom - rcBefore.top,
                    SWP_NOZORDER | SWP_NOACTIVATE
                );
            }
        }
    };

    HWND previousForeground = GetForegroundWindow();
    DWORD curThread = GetCurrentThreadId();
    DWORD fgThread = previousForeground ? GetWindowThreadProcessId(previousForeground, nullptr) : 0;
    DWORD targetThread = GetWindowThreadProcessId(target, nullptr);

    BOOL attachedFg = (fgThread && fgThread != curThread) ? AttachThreadInput(curThread, fgThread, TRUE) : FALSE;
    BOOL attachedTarget = (targetThread && targetThread != curThread) ? AttachThreadInput(curThread, targetThread, TRUE) : FALSE;

    if (wasMinimized) ShowWindow(target, SW_RESTORE);
    SetForegroundWindow(target);
    restoreSideIfMoved();

    if (focusWindowOnly) {
        if (hasClaudeSession) {
            std::wstring activeSession = GetClaudeDesktopActiveSessionId();
            if (activeSession != targetSessionId) {
                std::wstring deepLink = L"claude://code/continue?session=" + targetSessionId + L"&source=desktop_action";
                ShellExecuteW(nullptr, L"open", deepLink.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                for (int attempt = 0; attempt < 40; ++attempt) {
                    Sleep(20);
                    restoreSideIfMoved();
                    if (GetClaudeDesktopActiveSessionId() == targetSessionId) break;
                }
                SetForegroundWindow(target);
                restoreSideIfMoved();
            }
        }
        if (attachedTarget) AttachThreadInput(curThread, targetThread, FALSE);
        if (attachedFg) AttachThreadInput(curThread, fgThread, FALSE);
        return;
    }

    if (hasClaudeSession) {
        std::wstring activeSession = GetClaudeDesktopActiveSessionId();
        if (activeSession != targetSessionId) {
            std::wstring deepLink = L"claude://code/continue?session=" + targetSessionId + L"&source=desktop_action";
            ShellExecuteW(nullptr, L"open", deepLink.c_str(), nullptr, nullptr, SW_SHOWNORMAL);

            // Wait for Claude Desktop to route to the target session tab (up to 1.5s)
            for (int attempt = 0; attempt < 60; ++attempt) {
                Sleep(25);
                restoreSideIfMoved();
                if (GetClaudeDesktopActiveSessionId() == targetSessionId) {
                    break;
                }
            }
            // Allow React + requestAnimationFrame (dK) to focus ChatComposerEditor in the newly mounted chat
            Sleep(110);
            SetForegroundWindow(target);
            restoreSideIfMoved();
            Sleep(20);
        } else {
            Sleep(22);
        }
    } else {
        Sleep(18);
    }

    if (GetForegroundWindow() == target) {
        INPUT inputs[6] = {};
        inputs[0].type = INPUT_KEYBOARD; inputs[0].ki.wVk = VK_CONTROL;
        inputs[1].type = INPUT_KEYBOARD; inputs[1].ki.wVk = 'V';
        inputs[2].type = INPUT_KEYBOARD; inputs[2].ki.wVk = 'V'; inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
        inputs[3].type = INPUT_KEYBOARD; inputs[3].ki.wVk = VK_CONTROL; inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
        inputs[4].type = INPUT_KEYBOARD; inputs[4].ki.wVk = VK_RETURN;
        inputs[5].type = INPUT_KEYBOARD; inputs[5].ki.wVk = VK_RETURN; inputs[5].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(6, inputs, sizeof(INPUT));
        Sleep(55);
    }

    restoreSideIfMoved();

    if (!g_settings.openWindowMode && previousForeground &&
        IsWindow(previousForeground) && previousForeground != g_hudHwnd) {
        SetForegroundWindow(previousForeground);
    }

    if (attachedTarget) AttachThreadInput(curThread, targetThread, FALSE);
    if (attachedFg) AttachThreadInput(curThread, fgThread, FALSE);

    if (hadSavedClipboard) {
        SetEphemeralClipboardText(g_hudHwnd, savedClipboard);
    }
}

static void AddRoundRect(GraphicsPath& path, RectF rect, float radius) {
    float d = radius * 2.0f;
    path.AddArc(rect.X, rect.Y, d, d, 180.0f, 90.0f);
    path.AddArc(rect.GetRight() - d, rect.Y, d, d, 270.0f, 90.0f);
    path.AddArc(rect.GetRight() - d, rect.GetBottom() - d, d, d, 0.0f, 90.0f);
    path.AddArc(rect.X, rect.GetBottom() - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

static BYTE LerpByte(int a, int b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return static_cast<BYTE>(std::clamp(static_cast<int>(std::round(a + (b - a) * t)), 0, 255));
}

// Fast 2-pass separable Gaussian-approximation blur on 32-bit premultiplied ARGB DIB buffer
static void ApplySeparableGaussianBlur(uint32_t* pixels, int w, int h, int radius) {
    if (!pixels || radius <= 0 || w <= 0 || h <= 0) return;
    radius = std::clamp(radius, 1, 14);
    std::vector<uint32_t> temp(static_cast<size_t>(w * h));

    // Pass 1..2: two-iteration separable box blur approximates a smooth Gaussian kernel
    for (int iter = 0; iter < 2; ++iter) {
        int r = (iter == 0) ? radius : std::max(1, (radius * 2) / 3);
        int div = 2 * r + 1;

        // Horizontal pass
        for (int y = 0; y < h; ++y) {
            const uint32_t* rowIn = pixels + y * w;
            uint32_t* rowOut = temp.data() + y * w;
            for (int x = 0; x < w; ++x) {
                int sumB = 0, sumG = 0, sumR = 0, sumA = 0;
                for (int k = -r; k <= r; ++k) {
                    int sx = std::clamp(x + k, 0, w - 1);
                    uint32_t px = rowIn[sx];
                    sumB += (px & 0xFF);
                    sumG += ((px >> 8) & 0xFF);
                    sumR += ((px >> 16) & 0xFF);
                    sumA += ((px >> 24) & 0xFF);
                }
                rowOut[x] = static_cast<uint32_t>((sumB / div) |
                            ((sumG / div) << 8) |
                            ((sumR / div) << 16) |
                            ((sumA / div) << 24));
            }
        }

        // Vertical pass
        for (int y = 0; y < h; ++y) {
            uint32_t* rowOut = pixels + y * w;
            for (int x = 0; x < w; ++x) {
                int sumB = 0, sumG = 0, sumR = 0, sumA = 0;
                for (int k = -r; k <= r; ++k) {
                    int sy = std::clamp(y + k, 0, h - 1);
                    uint32_t px = temp[sy * w + x];
                    sumB += (px & 0xFF);
                    sumG += ((px >> 8) & 0xFF);
                    sumR += ((px >> 16) & 0xFF);
                    sumA += ((px >> 24) & 0xFF);
                }
                rowOut[x] = static_cast<uint32_t>((sumB / div) |
                            ((sumG / div) << 8) |
                            ((sumR / div) << 16) |
                            ((sumA / div) << 24));
            }
        }
    }
}

// Velocity-vector directional motion blur for high-speed flight from center screen to top-left
static void ApplyDirectionalMotionBlur(uint32_t* pixels, int w, int h, float vx, float vy) {
    float speed = std::sqrt(vx * vx + vy * vy);
    if (!pixels || speed < 140.0f) return;

    float ux = -vx / speed;
    float uy = -vy / speed;
    float maxStreakPx = std::clamp((speed - 140.0f) * 0.011f, 0.0f, 15.0f);
    if (maxStreakPx < 1.2f) return;

    const int kSamples = 7;
    static const int kWeights[kSamples] = { 34, 24, 16, 11, 7, 5, 3 }; // sums to 100
    std::vector<uint32_t> src(pixels, pixels + w * h);

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int sumB = 0, sumG = 0, sumR = 0, sumA = 0;
            for (int s = 0; s < kSamples; ++s) {
                float t = static_cast<float>(s) / static_cast<float>(kSamples - 1);
                int sx = std::clamp(static_cast<int>(std::round(x + ux * maxStreakPx * t)), 0, w - 1);
                int sy = std::clamp(static_cast<int>(std::round(y + uy * maxStreakPx * t)), 0, h - 1);
                uint32_t px = src[sy * w + sx];
                int wgt = kWeights[s];
                sumB += static_cast<int>(px & 0xFF) * wgt;
                sumG += static_cast<int>((px >> 8) & 0xFF) * wgt;
                sumR += static_cast<int>((px >> 16) & 0xFF) * wgt;
                sumA += static_cast<int>((px >> 24) & 0xFF) * wgt;
            }
            pixels[y * w + x] = static_cast<uint32_t>((sumB / 100) |
                                ((sumG / 100) << 8) |
                                ((sumR / 100) << 16) |
                                ((sumA / 100) << 24));
        }
    }
}

enum class LLMBrand {
    Claude_Orange,
    Codex_Black,
    Gemini_Green
};

static LLMBrand DetectLLMBrand(const LLMCyclePayload& payload) {
    std::wstring combined = payload.sourceApp + L" " + payload.title + L" " + payload.modelBadge;
    if (StrStrIW(combined.c_str(), L"Claude")) {
        return LLMBrand::Claude_Orange;
    }
    if (StrStrIW(combined.c_str(), L"Codex") || StrStrIW(combined.c_str(), L"codex")) {
        return LLMBrand::Codex_Black;
    }
    return LLMBrand::Gemini_Green;
}

static Bitmap* g_catClaudeOrange = nullptr;
static Bitmap* g_catCodexBlack = nullptr;
static Bitmap* g_catGeminiGreen = nullptr;
static std::mutex g_catBitmapMutex;

static Bitmap* LoadCatBitmap(const std::wstring& filename) {
    wchar_t userProfile[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"USERPROFILE", userProfile, MAX_PATH) == 0) {
        return nullptr;
    }
    std::wstring fullPath = std::wstring(userProfile) +
        L"\\.gemini\\antigravity\\scratch\\windhawk-llm-glass-hud\\assets\\" + filename;
    Bitmap* bmp = Bitmap::FromFile(fullPath.c_str());
    if (bmp && bmp->GetLastStatus() == Ok) {
        return bmp;
    }
    if (bmp) delete bmp;
    return nullptr;
}

static Bitmap* GetCatBitmapForBrand(LLMBrand brand) {
    std::lock_guard<std::mutex> lock(g_catBitmapMutex);
    switch (brand) {
        case LLMBrand::Claude_Orange:
            if (!g_catClaudeOrange) g_catClaudeOrange = LoadCatBitmap(L"cat_claude_orange.png");
            return g_catClaudeOrange;
        case LLMBrand::Codex_Black:
            if (!g_catCodexBlack) g_catCodexBlack = LoadCatBitmap(L"cat_codex_black.png");
            return g_catCodexBlack;
        case LLMBrand::Gemini_Green:
        default:
            if (!g_catGeminiGreen) g_catGeminiGreen = LoadCatBitmap(L"cat_gemini_green.png");
            return g_catGeminiGreen;
    }
}

static void FreeCatBitmaps() {
    std::lock_guard<std::mutex> lock(g_catBitmapMutex);
    if (g_catClaudeOrange) { delete g_catClaudeOrange; g_catClaudeOrange = nullptr; }
    if (g_catCodexBlack) { delete g_catCodexBlack; g_catCodexBlack = nullptr; }
    if (g_catGeminiGreen) { delete g_catGeminiGreen; g_catGeminiGreen = nullptr; }
}

static void RenderRobotBuddyHud(HWND hwnd, const LLMCyclePayload& payload) {
    const int size = kBuddyCanvasSize;
    LONGLONG nowQpc = 0;
    QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&nowQpc));
    float elapsedSec = (g_anim.showQpc > 0 && g_anim.qpcFreq > 0)
        ? static_cast<float>(nowQpc - g_anim.showQpc) / static_cast<float>(g_anim.qpcFreq)
        : 0.0f;

    float totalDuration = static_cast<float>(std::clamp(g_settings.autoDismissSeconds > 0 ? g_settings.autoDismissSeconds : 30, 5, 300));
    float alphaScale = std::clamp(g_anim.buddyOpacitySpring.value, 0.0f, 1.0f);
    float s = std::clamp(g_anim.buddyScaleSpring.value, 0.55f, 1.28f);
    float hoverT = std::clamp(g_anim.buddyHoverSpring.value, 0.0f, 1.0f);

    LLMBrand brand = DetectLLMBrand(payload);

    // Three distinct branded palettes per LLM:
    // Claude: Warm sunny orange & peach glow
    // Codex: Sleek crimson red ribbon & cyberpunk violet aura
    // Gemini: Fresh vibrant lime-green & mint glow
    Color neonPrimary(255, 255, 142, 58);
    Color neonSecondary(255, 255, 195, 120);
    std::wstring brandName = L"Claude";
    if (brand == LLMBrand::Codex_Black) {
        neonPrimary = Color(255, 255, 62, 84);
        neonSecondary = Color(255, 192, 142, 255);
        brandName = L"Codex";
    } else if (brand == LLMBrand::Gemini_Green) {
        neonPrimary = Color(255, 92, 235, 130);
        neonSecondary = Color(255, 175, 255, 192);
        brandName = StrStrIW(payload.sourceApp.c_str(), L"Antigravity") ? L"Antigravity" : L"Gemini";
    }

    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = size;
    bmi.bmiHeader.biHeight = -size;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pvBits = nullptr;
    HBITMAP hbm = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pvBits, nullptr, 0);
    HBITMAP hbmOld = static_cast<HBITMAP>(SelectObject(hdcMem, hbm));

    {
        Graphics g(hdcMem);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
        g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

        auto A = [alphaScale](int a) -> BYTE {
            return static_cast<BYTE>(std::clamp(static_cast<int>(std::round(a * alphaScale)), 0, 255));
        };

        float cx = size * 0.5f + g_anim.buddyShakeX;
        float cy = size * 0.5f - 8.0f + g_anim.buddyHopY;

        g.TranslateTransform(cx, cy);
        g.ScaleTransform(s, s);

        // 1. Soft multi-ring ambient bloom aura in the brand color
        for (int ring = 4; ring >= 1; --ring) {
            float r = 44.0f + ring * 9.0f + hoverT * 5.0f;
            int ringAlpha = (5 - ring) * (hoverT > 0.1f ? 14 : 9);
            SolidBrush auraBrush(Color(A(ringAlpha), neonPrimary.GetR(), neonPrimary.GetG(), neonPrimary.GetB()));
            g.FillEllipse(&auraBrush, -r, -r, r * 2.0f, r * 2.0f);
        }

        // Expanding sonar pulse ring on initial middle pop-in (0.04s .. 0.95s)
        if (elapsedSec > 0.04f && elapsedSec < 0.98f) {
            float pulseT = std::clamp((elapsedSec - 0.04f) / 0.88f, 0.0f, 1.0f);
            float pulseEase = 1.0f - std::pow(1.0f - pulseT, 3.0f);
            float pulseR = 40.0f + pulseEase * 50.0f;
            BYTE pulseAlpha = A(static_cast<int>((1.0f - pulseT) * 160.0f));
            Pen sonarPen(Color(pulseAlpha, neonPrimary.GetR(), neonPrimary.GetG(), neonPrimary.GetB()), 2.2f * (1.0f - pulseT * 0.5f));
            g.DrawEllipse(&sonarPen, -pulseR, -pulseR, pulseR * 2.0f, pulseR * 2.0f);
        }

        // 2. Frosted Apple Liquid Glass Orbital Disc Backing
        float discR = 64.0f;
        LinearGradientBrush discFill(
            PointF(0.0f, -discR),
            PointF(0.0f, discR),
            Color(A(175), 48, 54, 76),
            Color(A(205), 24, 28, 42)
        );
        g.FillEllipse(&discFill, -discR, -discR, discR * 2.0f, discR * 2.0f);

        // Inner top frost sheen
        LinearGradientBrush discSheen(
            PointF(0.0f, -discR),
            PointF(0.0f, 6.0f),
            Color(A(55), 240, 248, 255),
            Color(A(0), 240, 248, 255)
        );
        g.FillEllipse(&discSheen, -(discR - 1.5f), -(discR - 1.5f), (discR - 1.5f) * 2.0f, (discR - 1.5f) * 1.35f);

        // Crisp specular glass rim
        Pen discRimPen(Color(A(185), 255, 255, 255), 1.25f);
        g.DrawEllipse(&discRimPen, -discR, -discR, discR * 2.0f, discR * 2.0f);

        // Subtle perimeter track ring + 30-second countdown halo arc
        Pen trackPen(Color(A(50), 220, 235, 255), 2.4f);
        g.DrawEllipse(&trackPen, -discR, -discR, discR * 2.0f, discR * 2.0f);

        float remainFrac = 1.0f;
        if (elapsedSec > 2.0f) {
            remainFrac = std::clamp(1.0f - (elapsedSec - 2.0f) / totalDuration, 0.0f, 1.0f);
        }
        Color arcColor = (remainFrac < 0.18f)
            ? Color(A(240), 255, 160, 75)
            : Color(A(235), neonPrimary.GetR(), neonPrimary.GetG(), neonPrimary.GetB());
        Pen countdownPen(arcColor, 2.8f);
        countdownPen.SetStartCap(LineCapRound);
        countdownPen.SetEndCap(LineCapRound);
        g.DrawArc(&countdownPen, -discR, -discR, discR * 2.0f, discR * 2.0f, -90.0f, remainFrac * 360.0f);

        // 3. Playful Head-Tilt & Hover Rotation of the Main Cat
        GraphicsState catState = g.Save();
        float tiltDeg = g_anim.buddyTiltSpring.value;
        g.RotateTransform(tiltDeg);

        // Render the transparent segmented Cat Bitmap
        Bitmap* pCatBmp = GetCatBitmapForBrand(brand);
        if (pCatBmp) {
            g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            float bw = static_cast<float>(pCatBmp->GetWidth());
            float bh = static_cast<float>(pCatBmp->GetHeight());
            float targetSize = 126.0f;
            float aspect = (bh > 0.0f) ? (bw / bh) : 1.0f;
            float drawW = (aspect >= 1.0f) ? targetSize : targetSize * aspect;
            float drawH = (aspect >= 1.0f) ? (targetSize / aspect) : targetSize;
            RectF catRect(-drawW * 0.5f, -drawH * 0.5f - 3.0f, drawW, drawH);
            g.DrawImage(pCatBmp, catRect, 0, 0, bw, bh, UnitPixel);
        }

        g.Restore(catState);

        // 4. Sleek Floating Micro-Title Pill below the Cat Buddy
        GraphicsState pillState = g.Save();
        g.RotateTransform(tiltDeg * 0.35f);

        FontFamily fontFamily(L"Segoe UI Variable Display");
        FontFamily fallbackFamily(L"Segoe UI");
        const FontFamily* fam = fontFamily.IsAvailable() ? &fontFamily : &fallbackFamily;
        Font labelFont(fam, 11.0f, FontStyleBold, UnitPixel);

        std::wstring badgeLabel = (hoverT > 0.35f)
            ? (L"Open " + brandName + L" \x2192")
            : (brandName + L" \x00B7 Ready");
        if (badgeLabel.size() > 20) {
            badgeLabel = badgeLabel.substr(0, 19) + L"\x2026";
        }

        RectF labelPillRect(-58.0f, 45.0f, 116.0f, 21.0f);
        GraphicsPath labelPillPath;
        AddRoundRect(labelPillPath, labelPillRect, 10.5f);
        SolidBrush labelFill(Color(A(215), 26, 32, 48));
        Pen labelPen(Color(A(175), neonPrimary.GetR(), neonPrimary.GetG(), neonPrimary.GetB()), 1.15f);
        g.FillPath(&labelFill, &labelPillPath);
        g.DrawPath(&labelPen, &labelPillPath);

        StringFormat centerFmt;
        centerFmt.SetAlignment(StringAlignmentCenter);
        centerFmt.SetLineAlignment(StringAlignmentCenter);
        centerFmt.SetFormatFlags(StringFormatFlagsNoWrap);
        SolidBrush labelTextBrush(Color(A(254), 248, 252, 255));
        g.DrawString(badgeLabel.c_str(), -1, &labelFont, labelPillRect, &centerFmt, &labelTextBrush);

        g.Restore(pillState);
    }

    // Apply real pixel-level Directional Motion Blur (during flight) and Optical Gaussian Blur (on entrance/exit)
    uint32_t* rawPixels = static_cast<uint32_t*>(pvBits);
    if (rawPixels) {
        ApplyDirectionalMotionBlur(rawPixels, size, size, g_anim.buddyXSpring.velocity, g_anim.buddyYSpring.velocity);
        int dofRadius = static_cast<int>(std::round(g_anim.buddyBlurSpring.value));
        if (dofRadius >= 1) {
            ApplySeparableGaussianBlur(rawPixels, size, size, dofRadius);
        }
    }

    int screenX = static_cast<int>(std::round(g_anim.buddyXSpring.value));
    int screenY = static_cast<int>(std::round(g_anim.buddyYSpring.value));

    POINT ptSrc = {0, 0};
    POINT ptDst = {screenX, screenY};
    SIZE wndSize = {size, size};
    BLENDFUNCTION blend = {};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    UpdateLayeredWindow(hwnd, hdcScreen, &ptDst, &wndSize, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);

    SelectObject(hdcMem, hbmOld);
    DeleteObject(hbm);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
}

static void RenderGlassHud(HWND hwnd) {
    LLMCyclePayload payload;
    {
        std::lock_guard<std::mutex> lock(g_payloadMutex);
        payload = g_currentPayload;
    }

    if (g_settings.robotBuddyMode) {
        RenderRobotBuddyHud(hwnd, payload);
        return;
    }

    int width = std::clamp(g_settings.hudWidthPx, 360, 960);
    int cardHeight = std::clamp(static_cast<int>(std::round(g_anim.heightSpring.value)), 96, 360);
    int totalHeight = cardHeight + kPopoverCanvasReservePx;
    float cardTopY = static_cast<float>(kPopoverCanvasReservePx);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int bottomOffset = std::clamp(g_settings.bottomOffsetPx, 16, 400);
    int dragX = static_cast<int>(std::round(g_anim.dragOffsetX));

    int baseX = (screenW - width) / 2 + dragX;
    int baseY = screenH - totalHeight - bottomOffset;

    if (g_settings.keepOnPreviousSide) {
        if (g_anim.hasCustomPosition) {
            baseX = g_anim.customScreenX + dragX;
            POINT probePt = { baseX + width / 2, screenH / 2 };
            HMONITOR hMon = MonitorFromPoint(probePt, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi = {};
            mi.cbSize = sizeof(mi);
            if (hMon && GetMonitorInfoW(hMon, &mi)) {
                baseY = mi.rcMonitor.bottom - totalHeight - bottomOffset;
            }
        } else {
            if (payload.targetWindow && IsWindow(payload.targetWindow)) {
                g_anim.customMonitor = MonitorFromWindow(payload.targetWindow, MONITOR_DEFAULTTONEAREST);
            }
            if (g_anim.customMonitor) {
                MONITORINFO mi = {};
                mi.cbSize = sizeof(mi);
                if (GetMonitorInfoW(g_anim.customMonitor, &mi)) {
                    int monW = mi.rcMonitor.right - mi.rcMonitor.left;
                    baseX = mi.rcMonitor.left + (monW - width) / 2 + dragX;
                    baseY = mi.rcMonitor.bottom - totalHeight - bottomOffset;
                }
            }
        }
    }

    int actualY = baseY + static_cast<int>(std::round(g_anim.ySpring.value));

    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -totalHeight;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pvBits = nullptr;
    HBITMAP hbm = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pvBits, nullptr, 0);
    HBITMAP hbmOld = static_cast<HBITMAP>(SelectObject(hdcMem, hbm));

    {
        Graphics g(hdcMem);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

        float s = std::clamp(g_anim.scaleSpring.value, 0.85f, 1.08f);
        float pivotX = width * 0.5f;
        float pivotY = static_cast<float>(totalHeight);
        g.TranslateTransform(pivotX, pivotY);
        g.ScaleTransform(s, s);
        g.TranslateTransform(-pivotX, -pivotY);

        float alphaScale = std::clamp(g_anim.opacitySpring.value, 0.0f, 1.0f);
        float contentAlpha = std::clamp(g_anim.contentFadeSpring.value, 0.0f, 1.0f) * alphaScale;

        auto A = [alphaScale](int a) -> BYTE {
            return static_cast<BYTE>(std::clamp(static_cast<int>(a * alphaScale), 0, 255));
        };
        auto CA = [contentAlpha](int a) -> BYTE {
            return static_cast<BYTE>(std::clamp(static_cast<int>(a * contentAlpha), 0, 255));
        };

        FontFamily fontFamily(L"Segoe UI Variable Display");
        FontFamily fallbackFamily(L"Segoe UI");
        const FontFamily* fam = fontFamily.IsAvailable() ? &fontFamily : &fallbackFamily;

        Font badgeFont(fam, 11.0f, FontStyleBold, UnitPixel);
        Font titleFont(fam, 14.5f, FontStyleBold, UnitPixel);
        Font bodyFont(fam, 13.0f, FontStyleRegular, UnitPixel);
        Font optFont(fam, 12.5f, FontStyleBold, UnitPixel);
        Font keycapFont(fam, 11.0f, FontStyleBold, UnitPixel);
        Font popTitleFont(fam, 12.0f, FontStyleBold, UnitPixel);
        Font popBodyFont(fam, 12.5f, FontStyleRegular, UnitPixel);

        // Soft outer ambient shadow ring for floating Apple elevation
        RectF shadowRect(1.0f, cardTopY + 2.5f, static_cast<float>(width - 2), static_cast<float>(cardHeight - 3));
        GraphicsPath shadowPath;
        AddRoundRect(shadowPath, shadowRect, 23.0f);
        Pen shadowPen(Color(A(45), 8, 12, 24), 2.0f);
        g.DrawPath(&shadowPen, &shadowPath);

        RectF cardRect(2.5f, cardTopY + 2.0f, static_cast<float>(width - 5), static_cast<float>(cardHeight - 4));
        GraphicsPath cardPath;
        AddRoundRect(cardPath, cardRect, 22.0f);

        // Luminous Apple Liquid Glass surface (brighter, cleaner, frosted visionOS / macOS Tahoe palette)
        int baseGlassAlpha = std::clamp(g_settings.glassOpacity, 110, 235);
        LinearGradientBrush glassBrush(
            PointF(0.0f, cardTopY),
            PointF(0.0f, cardTopY + static_cast<float>(cardHeight)),
            Color(A(baseGlassAlpha - 14), 68, 76, 98),
            Color(A(baseGlassAlpha + 4), 40, 46, 64)
        );
        g.FillPath(&glassBrush, &cardPath);

        // Top-half frosted light-catch sheen (gives the luminous Apple glass feel)
        RectF sheenRect(cardRect.X + 1.0f, cardRect.Y + 1.0f, cardRect.Width - 2.0f, cardRect.Height * 0.48f);
        GraphicsPath sheenPath;
        AddRoundRect(sheenPath, sheenRect, 21.0f);
        LinearGradientBrush sheenBrush(
            PointF(0.0f, cardRect.Y),
            PointF(0.0f, cardRect.Y + cardRect.Height * 0.50f),
            Color(A(44), 240, 246, 255),
            Color(A(0), 240, 246, 255)
        );
        g.FillPath(&sheenBrush, &sheenPath);

        // Crisp 2-layer specular rim (outer icy highlight + inner subtle glass bevel)
        LinearGradientBrush rimBrush(
            PointF(0.0f, cardTopY),
            PointF(0.0f, cardTopY + static_cast<float>(cardHeight)),
            Color(A(215), 255, 255, 255),
            Color(A(62), 200, 220, 255)
        );
        Pen rimPen(&rimBrush, 1.35f);
        g.DrawPath(&rimPen, &cardPath);

        GraphicsPath pillPath;
        AddRoundRect(pillPath, RectF(width / 2.0f - 19.0f, cardTopY + 7.5f, 38.0f, 4.0f), 2.0f);
        SolidBrush handleBrush(Color(A(115), 255, 255, 255));
        g.FillPath(&handleBrush, &pillPath);

        SolidBrush orbGlowBrush(Color(CA(72), 64, 224, 120));
        g.FillEllipse(&orbGlowBrush, 18.0f, cardTopY + 18.5f, 16.0f, 16.0f);
        SolidBrush orbCoreBrush(Color(CA(255), 52, 215, 98));
        g.FillEllipse(&orbCoreBrush, 22.0f, cardTopY + 22.5f, 8.0f, 8.0f);

        SolidBrush titleBrush(Color(CA(254), 252, 253, 255));
        SolidBrush mutedBrush(Color(CA(218), 228, 234, 248));
        SolidBrush accentBrush(Color(CA(250), 150, 212, 255));

        std::wstring headerLine = payload.sourceApp + L"  \x00B7  " + payload.title;
        StringFormat headerFmt;
        headerFmt.SetTrimming(StringTrimmingEllipsisCharacter);
        headerFmt.SetFormatFlags(StringFormatFlagsNoWrap);
        g.DrawString(headerLine.c_str(), -1, &titleFont,
                     RectF(42.0f, cardTopY + 16.5f, static_cast<float>(width - 210), 22.0f),
                     &headerFmt, &titleBrush);

        // Luminous pill badge on top-right
        StringFormat centerFmt;
        centerFmt.SetAlignment(StringAlignmentCenter);
        centerFmt.SetLineAlignment(StringAlignmentCenter);
        RectF badgeRect(static_cast<float>(width - 158), cardTopY + 15.5f, 138.0f, 21.0f);
        GraphicsPath badgePath;
        AddRoundRect(badgePath, badgeRect, 10.5f);
        SolidBrush badgeFill(Color(CA(48), 140, 205, 255));
        Pen badgePen(Color(CA(110), 175, 222, 255), 1.0f);
        g.FillPath(&badgeFill, &badgePath);
        g.DrawPath(&badgePen, &badgePath);
        g.DrawString(payload.modelBadge.c_str(), -1, &badgeFont, badgeRect, &centerFmt, &accentBrush);

        std::wstring shownSummary = payload.summaryShort;
        if (payload.summaryIsLarge) {
            shownSummary += L"   \x22EF";
        }
        SolidBrush summaryHoverBrush(Color(CA(252), 185, 228, 255));
        StringFormat summaryFmt;
        summaryFmt.SetTrimming(StringTrimmingEllipsisWord);
        g.DrawString(
            shownSummary.c_str(),
            -1,
            &bodyFont,
            RectF(20.0f, cardTopY + 43.0f, static_cast<float>(width - 40), payload.options.empty() ? 36.0f : 23.0f),
            &summaryFmt,
            g_anim.hoveredSummary ? &summaryHoverBrush : &mutedBrush
        );

        if (!payload.options.empty()) {
            int cols = 2;
            float padX = 18.0f;
            float gap = 10.0f;
            float cellW = (width - padX * 2.0f - gap) / 2.0f;
            float cellH = 35.0f;
            float startY = cardTopY + 71.0f;

            for (size_t i = 0; i < payload.options.size() && i < 4; ++i) {
                int r = static_cast<int>(i) / cols;
                int c = static_cast<int>(i) % cols;
                float baseCellX = padX + c * (cellW + gap);
                float baseCellY = startY + r * (cellH + 8.0f);

                float pScale = g_anim.pillScale[i].value;
                float hBlend = std::clamp(g_anim.pillHover[i].value, 0.0f, 1.0f);
                float entranceT = std::clamp(g_anim.pillEntrance[i].value, 0.0f, 1.0f);
                float yEntranceOffset = (1.0f - entranceT) * 9.0f;

                auto PA_PILL = [contentAlpha, entranceT](int a) -> BYTE {
                    return static_cast<BYTE>(std::clamp(static_cast<int>(a * contentAlpha * entranceT), 0, 255));
                };

                float w = cellW * pScale;
                float h = cellH * pScale;
                float x = baseCellX + (cellW - w) * 0.5f;
                float y = baseCellY + (cellH - h) * 0.5f + yEntranceOffset;

                RectF pillRect(x, y, w, h);
                GraphicsPath optPath;
                AddRoundRect(optPath, pillRect, 11.5f);

                LinearGradientBrush optFill(
                    PointF(x, y),
                    PointF(x, y + h),
                    Color(
                        PA_PILL(LerpByte(72, 155, hBlend)),
                        LerpByte(245, 98, hBlend),
                        LerpByte(248, 168, hBlend),
                        255
                    ),
                    Color(
                        PA_PILL(LerpByte(46, 122, hBlend)),
                        LerpByte(225, 72, hBlend),
                        LerpByte(234, 140, hBlend),
                        255
                    )
                );
                Color borderCol(
                    PA_PILL(LerpByte(96, 230, hBlend)),
                    LerpByte(255, 160, hBlend),
                    LerpByte(255, 215, hBlend),
                    255
                );

                Pen optPen(borderCol, 1.15f);
                g.FillPath(&optFill, &optPath);
                g.DrawPath(&optPen, &optPath);

                // Frosted Apple keycap badge for option number (1..4)
                RectF keycapRect(x + 8.0f, y + (h - 20.0f) * 0.5f, 20.0f, 20.0f);
                GraphicsPath keycapPath;
                AddRoundRect(keycapPath, keycapRect, 6.0f);
                SolidBrush keycapFill(Color(PA_PILL(LerpByte(52, 95, hBlend)), 255, 255, 255));
                Pen keycapPen(Color(PA_PILL(LerpByte(85, 165, hBlend)), 255, 255, 255), 0.9f);
                g.FillPath(&keycapFill, &keycapPath);
                g.DrawPath(&keycapPen, &keycapPath);

                std::wstring numStr = std::to_wstring(i + 1);
                SolidBrush pillTextBrush(Color(PA_PILL(254), 252, 253, 255));
                g.DrawString(numStr.c_str(), -1, &keycapFont, keycapRect, &centerFmt, &pillTextBrush);

                StringFormat vCenter;
                vCenter.SetLineAlignment(StringAlignmentCenter);
                vCenter.SetTrimming(StringTrimmingEllipsisCharacter);
                vCenter.SetFormatFlags(StringFormatFlagsNoWrap);

                float rightPad = payload.options[i].isLarge ? 56.0f : 38.0f;
                g.DrawString(payload.options[i].shortSubtitle.c_str(), -1, &optFont,
                             RectF(x + 34.0f, y, w - rightPad, h), &vCenter, &pillTextBrush);

                if (payload.options[i].isLarge) {
                    SolidBrush expandHintBrush(Color(PA_PILL(LerpByte(165, 250, hBlend)), 180, 225, 255));
                    g.DrawString(L"\x22EF", -1, &optFont,
                                 RectF(x + w - 23.0f, y, 19.0f, h), &vCenter, &expandHintBrush);
                }
            }
        } else if (g_settings.openWindowMode) {
            RectF openBtnRect(18.0f, cardTopY + 83.0f, static_cast<float>(width - 36), 35.0f);
            GraphicsPath openBtnPath;
            AddRoundRect(openBtnPath, openBtnRect, 11.5f);

            LinearGradientBrush openBtnFill(
                PointF(18.0f, cardTopY + 83.0f),
                PointF(18.0f, cardTopY + 118.0f),
                Color(CA(96), 115, 182, 255),
                Color(CA(68), 82, 148, 255)
            );
            Pen openBtnPen(Color(CA(175), 165, 218, 255), 1.15f);
            g.FillPath(&openBtnFill, &openBtnPath);
            g.DrawPath(&openBtnPen, &openBtnPath);

            std::wstring openPrompt = L"Click to open " + payload.sourceApp + L" chat window   \x2192";
            g.DrawString(openPrompt.c_str(), -1, &optFont, openBtnRect, &centerFmt, &titleBrush);
        } else {
            RectF inputRect(18.0f, cardTopY + 83.0f, static_cast<float>(width - 36), 35.0f);
            GraphicsPath inputPath;
            AddRoundRect(inputPath, inputRect, 11.5f);

            SolidBrush inputFill(Color(CA(64), 245, 248, 255));
            Pen inputPen(g_anim.replyBoxFocused
                         ? Color(CA(220), 130, 198, 255)
                         : Color(CA(92), 255, 255, 255), 1.15f);
            g.FillPath(&inputFill, &inputPath);
            g.DrawPath(&inputPen, &inputPath);

            std::wstring displayReply = g_anim.replyDraft.empty()
                ? L"Reply inline without switching windows (Enter to send, Esc to dismiss)..."
                : (g_anim.replyDraft + L"|");
            SolidBrush& textBrush = g_anim.replyDraft.empty() ? mutedBrush : titleBrush;

            StringFormat vCenter;
            vCenter.SetLineAlignment(StringAlignmentCenter);
            g.DrawString(displayReply.c_str(), -1, &bodyFont,
                         RectF(30.0f, cardTopY + 83.0f, static_cast<float>(width - 60), 35.0f), &vCenter, &textBrush);
        }

        float popT = std::clamp(g_anim.popoverProgress.value, 0.0f, 1.0f);
        if (popT > 0.01f && !g_anim.popoverBody.empty()) {
            GraphicsState popState = g.Save();

            float popW = static_cast<float>(width - 24);
            float popH = static_cast<float>(kPopoverCanvasReservePx - 14);
            float popX = 12.0f;
            float popY = 6.0f + (1.0f - popT) * 8.0f;

            float originX = std::clamp(g_anim.popoverAnchorX.value, 60.0f, static_cast<float>(width - 60));
            float originY = popY + popH;
            float popScale = 0.96f + 0.04f * popT;

            g.TranslateTransform(originX, originY);
            g.ScaleTransform(popScale, popScale);
            g.TranslateTransform(-originX, -originY);

            auto PA = [popT, alphaScale](int a) -> BYTE {
                return static_cast<BYTE>(std::clamp(static_cast<int>(a * popT * alphaScale), 0, 255));
            };

            RectF popRect(popX, popY, popW, popH);
            GraphicsPath popPath;
            AddRoundRect(popPath, popRect, 18.0f);

            LinearGradientBrush popGlass(
                PointF(0.0f, popY),
                PointF(0.0f, popY + popH),
                Color(PA(228), 64, 72, 94),
                Color(PA(238), 38, 44, 62)
            );
            g.FillPath(&popGlass, &popPath);

            LinearGradientBrush popRim(
                PointF(0.0f, popY),
                PointF(0.0f, popY + popH),
                Color(PA(215), 255, 255, 255),
                Color(PA(75), 185, 220, 255)
            );
            Pen popRimPen(&popRim, 1.3f);
            g.DrawPath(&popRimPen, &popPath);

            SolidBrush popHeadingBrush(Color(PA(252), 155, 215, 255));
            SolidBrush popBodyBrush(Color(PA(252), 250, 252, 255));

            g.DrawString(g_anim.popoverHeading.c_str(), -1, &popTitleFont,
                         PointF(popX + 15.0f, popY + 10.0f), &popHeadingBrush);

            StringFormat wrapFmt;
            wrapFmt.SetTrimming(StringTrimmingEllipsisWord);
            g.DrawString(g_anim.popoverBody.c_str(), -1, &popBodyFont,
                         RectF(popX + 15.0f, popY + 30.0f, popW - 30.0f, popH - 38.0f),
                         &wrapFmt, &popBodyBrush);

            g.Restore(popState);
        }
    }

    POINT ptSrc = {0, 0};
    POINT ptDst = {baseX, actualY};
    SIZE wndSize = {width, totalHeight};
    BLENDFUNCTION blend = {};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    UpdateLayeredWindow(hwnd, hdcScreen, &ptDst, &wndSize, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);

    SelectObject(hdcMem, hbmOld);
    DeleteObject(hbm);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
}

static int HitTestOption(int clientX, int clientY, float* outCenterX = nullptr) {
    LLMCyclePayload payload;
    {
        std::lock_guard<std::mutex> lock(g_payloadMutex);
        payload = g_currentPayload;
    }
    if (payload.options.empty()) return -1;

    int width = std::clamp(g_settings.hudWidthPx, 360, 960);
    int cols = 2;
    float padX = 18.0f;
    float gap = 10.0f;
    float cellW = (width - padX * 2.0f - gap) / 2.0f;
    float cellH = 34.0f;
    float startY = static_cast<float>(kPopoverCanvasReservePx) + 72.0f;

    for (size_t i = 0; i < payload.options.size() && i < 4; ++i) {
        int r = static_cast<int>(i) / cols;
        int c = static_cast<int>(i) % cols;
        float x = padX + c * (cellW + gap);
        float y = startY + r * (cellH + 8.0f);
        if (clientX >= (x - 4.0f) && clientX <= (x + cellW + 4.0f) &&
            clientY >= (y - 3.0f) && clientY <= (y + cellH + 3.0f)) {
            if (outCenterX) *outCenterX = x + cellW * 0.5f;
            return static_cast<int>(i);
        }
    }
    return -1;
}

static bool HitTestSummaryRow(int clientX, int clientY) {
    int width = std::clamp(g_settings.hudWidthPx, 360, 960);
    float topY = static_cast<float>(kPopoverCanvasReservePx) + 42.0f;
    return (clientX >= 18 && clientX <= width - 18 && clientY >= topY && clientY <= topY + 28.0f);
}

static void EnsureAnimationLoopRunning(HWND hwnd) {
    LONGLONG now = 0;
    QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&now));
    if (g_anim.lastFrameQpc == 0) {
        g_anim.lastFrameQpc = now;
    }
    SetTimer(hwnd, TIMER_SPRING_FRAME, 8, nullptr);
}

static void StartDismissAnimation(HWND hwnd, float releaseVelocityY = 0.0f) {
    g_anim.dismissing = true;
    g_anim.yZeta = 1.0f;
    g_anim.popoverActive = false;
    g_anim.popoverProgress.target = 0.0f;

    if (g_settings.robotBuddyMode) {
        g_anim.buddyOpacitySpring.target = 0.0f;
        g_anim.buddyScaleSpring.target = IsReducedMotionEnabled() ? 0.96f : 0.86f;
        g_anim.buddyBlurSpring.target = IsReducedMotionEnabled() ? 0.0f : 8.5f;
        EnsureAnimationLoopRunning(hwnd);
        return;
    }

    if (IsReducedMotionEnabled()) {
        g_anim.ySpring.target = 0.0f;
        g_anim.scaleSpring.target = 1.0f;
        g_anim.opacitySpring.target = 0.0f;
    } else {
        g_anim.ySpring.target = 86.0f;
        g_anim.scaleSpring.target = 0.95f;
        g_anim.opacitySpring.target = 0.0f;
        if (std::fabs(releaseVelocityY) > 10.0f) {
            g_anim.ySpring.velocity = releaseVelocityY;
        }
    }
    EnsureAnimationLoopRunning(hwnd);
}

static void StepSpringAnimation(HWND hwnd) {
    LONGLONG now = 0;
    QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&now));
    float dt = static_cast<float>(now - g_anim.lastFrameQpc) / static_cast<float>(g_anim.qpcFreq);
    g_anim.lastFrameQpc = now;
    dt = std::clamp(dt, 0.001f, 0.033f);

    if (g_settings.robotBuddyMode) {
        float elapsedSec = (g_anim.showQpc > 0 && g_anim.qpcFreq > 0)
            ? static_cast<float>(now - g_anim.showQpc) / static_cast<float>(g_anim.qpcFreq)
            : 0.0f;
        bool reduced = IsReducedMotionEnabled();

        if (!g_anim.dismissing) {
            if (!g_anim.buddyLaunchedToCorner) {
                // Phase 1 (0.0s .. 2.0s): Stay in the Very Middle + Playful Floating Hover & Gentle Head-Tilt
                if (!reduced && elapsedSec < 2.0f) {
                    float bobSpeed = 4.8f;
                    g_anim.buddyTiltSpring.target = std::sin(elapsedSec * bobSpeed) * 5.8f;
                    g_anim.buddyShakeX = 0.0f;
                    g_anim.buddyHopY = -std::fabs(std::sin(elapsedSec * 5.2f)) * 4.2f;
                } else {
                    g_anim.buddyTiltSpring.target = 0.0f;
                    g_anim.buddyShakeX = 0.0f;
                    g_anim.buddyHopY = 0.0f;
                }

                // Settle scale after initial pop
                if (elapsedSec > 0.22f) {
                    g_anim.buddyScaleSpring.target = 1.0f;
                }

                // Phase 2 (t >= 2.0s): Smoothly glide to the Top-Left Corner in a graceful hover move
                if (elapsedSec >= (reduced ? 0.6f : 2.0f)) {
                    g_anim.buddyLaunchedToCorner = true;
                    g_anim.buddyXSpring.target = g_anim.buddyCornerX;
                    g_anim.buddyYSpring.target = g_anim.buddyCornerY;
                    if (!reduced) {
                        // Gentle buoyant hover lift impulse
                        g_anim.buddyYSpring.velocity -= 130.0f;
                        g_anim.buddyXSpring.velocity -= 80.0f;
                    }
                    g_anim.buddyScaleSpring.target = 0.90f;
                    g_anim.buddyShakeX = 0.0f;
                    g_anim.buddyHopY = 0.0f;
                }
            } else {
                // In-flight gentle hover banking & top-left perch breathing
                float vx = g_anim.buddyXSpring.velocity;
                float vy = g_anim.buddyYSpring.velocity;
                float speed = std::sqrt(vx * vx + vy * vy);
                if (speed > 35.0f && !reduced) {
                    g_anim.buddyTiltSpring.target = std::clamp(vx * 0.012f, -14.0f, 14.0f);
                    // Floating undulation during hover glide
                    g_anim.buddyHopY = std::sin((elapsedSec - 2.0f) * 6.2f) * 3.5f;
                } else {
                    float idleBob = (!reduced && elapsedSec > 2.8f)
                        ? std::sin((elapsedSec - 2.8f) * 2.2f) * 3.5f
                        : 0.0f;
                    g_anim.buddyHopY = idleBob;
                    float idleTilt = (!reduced && elapsedSec > 2.8f)
                        ? std::sin((elapsedSec - 2.8f) * 1.5f) * 2.2f
                        : 0.0f;
                    g_anim.buddyTiltSpring.target = g_anim.buddyHovered ? 6.5f : idleTilt;
                }
                g_anim.buddyScaleSpring.target = g_anim.buddyHovered ? 1.04f : 0.90f;
            }
        }

        if (!g_anim.isDragging) {
            g_anim.buddyXSpring.Step(dt, 6.2f, 0.92f);
            g_anim.buddyYSpring.Step(dt, 6.2f, 0.92f);
        }
        g_anim.buddyScaleSpring.Step(dt, 26.0f, 0.78f);
        bool opMoving = g_anim.buddyOpacitySpring.Step(dt, 24.0f, 1.0f);
        g_anim.buddyTiltSpring.Step(dt, 32.0f, 0.82f);
        g_anim.buddyBlurSpring.Step(dt, 26.0f, 1.0f);
        g_anim.buddyHoverSpring.Step(dt, 30.0f, 1.0f);

        RenderGlassHud(hwnd);

        // Keep animation loop alive while visible in Robot Buddy mode (for 30s countdown ring & hover bob)
        if (g_anim.dismissing && !opMoving && g_anim.buddyOpacitySpring.value <= 0.01f) {
            KillTimer(hwnd, TIMER_SPRING_FRAME);
            g_anim.lastFrameQpc = 0;
            g_anim.visible = false;
            g_anim.dismissing = false;
            ShowWindow(hwnd, SW_HIDE);
        }
        return;
    }

    bool anyMoving = false;

    if (!g_anim.isDragging) {
        if (g_anim.ySpring.Step(dt, 25.0f, g_anim.yZeta)) anyMoving = true;
        if (g_anim.scaleSpring.Step(dt, 27.0f, 1.0f)) anyMoving = true;
    } else {
        anyMoving = true;
    }

    if (g_anim.opacitySpring.Step(dt, 28.0f, 1.0f)) anyMoving = true;
    if (g_anim.heightSpring.Step(dt, 25.0f, 1.0f)) anyMoving = true;
    if (g_anim.contentFadeSpring.Step(dt, 28.0f, 1.0f)) anyMoving = true;
    if (g_anim.popoverProgress.Step(dt, 34.0f, 1.0f)) anyMoving = true;
    if (g_anim.popoverAnchorX.Step(dt, 36.0f, 1.0f)) anyMoving = true;

    for (int i = 0; i < 4; ++i) {
        if (g_anim.pillScale[i].Step(dt, 36.0f, 0.94f)) anyMoving = true;
        if (g_anim.pillHover[i].Step(dt, 30.0f, 1.0f)) anyMoving = true;
        if (g_anim.pillEntrance[i].Step(dt, 26.0f, 1.0f)) anyMoving = true;
    }

    RenderGlassHud(hwnd);

    if (!anyMoving && !g_anim.isDragging) {
        KillTimer(hwnd, TIMER_SPRING_FRAME);
        g_anim.lastFrameQpc = 0;

        if (g_anim.dismissing) {
            g_anim.visible = false;
            g_anim.dismissing = false;
            SecureZeroMemory(g_anim.replyDraft.data(), g_anim.replyDraft.size() * sizeof(wchar_t));
            g_anim.replyDraft.clear();
            if (g_settings.enableHotkeys) {
                for (int k = 1; k <= 4; ++k) UnregisterHotKey(hwnd, 2000 + k);
            }
            ShowWindow(hwnd, SW_HIDE);
        }
    }
}

static LRESULT CALLBACK GlassHudWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_APP_SHOW_LLM_HUD: {
            float targetH = 132.0f;
            HWND payloadTargetWnd = nullptr;
            {
                std::lock_guard<std::mutex> lock(g_payloadMutex);
                targetH = static_cast<float>(ComputeTargetHudHeight(g_currentPayload));
                payloadTargetWnd = g_currentPayload.targetWindow;
            }

            bool reduced = IsReducedMotionEnabled();

            // Determine active monitor bounds for Center Pop-In -> Top-Left Perch
            RECT monRc = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
            HMONITOR hMon = nullptr;
            if (g_settings.keepOnPreviousSide && payloadTargetWnd && IsWindow(payloadTargetWnd)) {
                hMon = MonitorFromWindow(payloadTargetWnd, MONITOR_DEFAULTTONEAREST);
                g_anim.customMonitor = hMon;
            } else if (g_settings.keepOnPreviousSide && g_anim.customMonitor) {
                hMon = g_anim.customMonitor;
            } else {
                POINT cursorPt = {};
                GetCursorPos(&cursorPt);
                hMon = MonitorFromPoint(cursorPt, MONITOR_DEFAULTTONEAREST);
            }
            if (hMon) {
                MONITORINFO mi = {};
                mi.cbSize = sizeof(mi);
                if (GetMonitorInfoW(hMon, &mi)) {
                    monRc = mi.rcWork;
                }
            }

            float monW = static_cast<float>(monRc.right - monRc.left);
            float monH = static_cast<float>(monRc.bottom - monRc.top);
            g_anim.buddyCenterX = static_cast<float>(monRc.left) + (monW - kBuddyCanvasSize) * 0.5f;
            g_anim.buddyCenterY = static_cast<float>(monRc.top) + (monH - kBuddyCanvasSize) * 0.5f;
            if (g_anim.hasCustomBuddyPerch) {
                g_anim.buddyCornerX = g_anim.customBuddyPerchX;
                g_anim.buddyCornerY = g_anim.customBuddyPerchY;
            } else {
                g_anim.buddyCornerX = static_cast<float>(monRc.left) + 24.0f;
                g_anim.buddyCornerY = static_cast<float>(monRc.top) + 24.0f;
            }

            g_anim.buddyXSpring.Reset(g_anim.buddyCenterX, g_anim.buddyCenterX, 0.0f);
            g_anim.buddyYSpring.Reset(g_anim.buddyCenterY, g_anim.buddyCenterY, 0.0f);
            g_anim.buddyScaleSpring.Reset(reduced ? 1.0f : 0.82f, reduced ? 1.0f : 1.06f, reduced ? 0.0f : 3.8f);
            g_anim.buddyOpacitySpring.Reset(0.0f, 1.0f, 0.0f);
            g_anim.buddyBlurSpring.Reset(reduced ? 0.0f : 8.0f, 0.0f, 0.0f);
            g_anim.buddyTiltSpring.Reset(0.0f, 0.0f, 0.0f);
            g_anim.buddyHoverSpring.Reset(0.0f, 0.0f, 0.0f);
            g_anim.buddyShakeX = 0.0f;
            g_anim.buddyHopY = 0.0f;
            g_anim.buddyLaunchedToCorner = false;
            g_anim.buddyHovered = false;

            if (!g_anim.visible) {
                g_anim.ySpring.Reset(reduced ? 0.0f : 56.0f, 0.0f, 0.0f);
                g_anim.scaleSpring.Reset(reduced ? 1.0f : 0.96f, 1.0f, 0.0f);
                g_anim.opacitySpring.Reset(0.0f, 1.0f, 0.0f);
                g_anim.heightSpring.Reset(targetH, targetH, 0.0f);
                g_anim.contentFadeSpring.Reset(1.0f, 1.0f, 0.0f);
            } else {
                g_anim.ySpring.target = 0.0f;
                g_anim.scaleSpring.target = 1.0f;
                g_anim.opacitySpring.target = 1.0f;
                g_anim.heightSpring.target = targetH;
                g_anim.contentFadeSpring.Reset(0.75f, 1.0f, 0.0f);
            }

            QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&g_anim.showQpc));
            g_anim.yZeta = 1.0f;
            g_anim.visible = true;
            g_anim.dismissing = false;
            g_anim.hoveredOption = -1;
            g_anim.hoveredSummary = false;
            g_anim.popoverActive = false;
            g_anim.popoverProgress.Reset(0.0f, 0.0f);
            g_anim.pressedOption = -1;
            g_anim.replyDraft.clear();
            for (int i = 0; i < 4; ++i) {
                g_anim.pillScale[i].target = 1.0f;
                g_anim.pillHover[i].target = 0.0f;
                if (reduced) {
                    g_anim.pillEntrance[i].Reset(1.0f, 1.0f, 0.0f);
                } else {
                    g_anim.pillEntrance[i].Reset(0.0f, 1.0f, -static_cast<float>(i) * 4.2f);
                }
            }

            ShowWindow(hwnd, SW_SHOWNOACTIVATE);
            ApplyAppleGlassBackdrop(hwnd);

            if (g_settings.enableHotkeys && !g_settings.robotBuddyMode) {
                for (int k = 1; k <= 4; ++k) {
                    RegisterHotKey(hwnd, 2000 + k, MOD_ALT | MOD_NOREPEAT, '0' + k);
                }
            }

            g_anim.lastFrameQpc = 0;
            EnsureAnimationLoopRunning(hwnd);
            int dismissSec = g_settings.autoDismissSeconds;
            if (g_settings.robotBuddyMode && dismissSec <= 0) {
                dismissSec = 30;
            }
            if (dismissSec > 0) {
                UINT timeoutMs = static_cast<UINT>(std::clamp(dismissSec, 3, 300) * 1000) +
                                 (g_settings.robotBuddyMode ? 2000u : 0u);
                SetTimer(hwnd, TIMER_AUTO_DISMISS, timeoutMs, nullptr);
            }
            return 0;
        }

        case WM_TIMER: {
            if (wParam == TIMER_SPRING_FRAME) {
                StepSpringAnimation(hwnd);
            } else if (wParam == TIMER_AUTO_DISMISS) {
                KillTimer(hwnd, TIMER_AUTO_DISMISS);
                if (!g_anim.isDragging && !g_anim.replyBoxFocused && !g_anim.popoverActive) {
                    StartDismissAnimation(hwnd);
                }
            }
            return 0;
        }

        case WM_HOTKEY: {
            if (g_anim.IsWithinClickjackGraceWindow()) return 0;
            int idx = static_cast<int>(wParam) - 2001;
            std::wstring chosen;
            {
                std::lock_guard<std::mutex> lock(g_payloadMutex);
                if (idx >= 0 && idx < static_cast<int>(g_currentPayload.options.size())) {
                    chosen = g_currentPayload.options[idx].fullText;
                }
            }
            if (!chosen.empty()) {
                SendAnswerToLLM(chosen, false);
                StartDismissAnimation(hwnd);
            }
            return 0;
        }

        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;

        case WM_LBUTTONDOWN: {
            if (g_anim.isDragging || g_anim.IsWithinClickjackGraceWindow()) return 0;
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);

            if (g_settings.robotBuddyMode) {
                SetCapture(hwnd);
                g_anim.isDragging = true;
                GetCursorPos(&g_anim.dragStartScreenPt);
                g_anim.dragStartOffsetX = g_anim.buddyXSpring.value;
                g_anim.dragStartOffsetY = g_anim.buddyYSpring.value;
                g_anim.buddyScaleSpring.target = 0.80f;
                EnsureAnimationLoopRunning(hwnd);
                return 0;
            }

            if (y < kPopoverCanvasReservePx) return 0;

            int optIdx = HitTestOption(x, y);
            if (optIdx >= 0) {
                g_anim.pressedOption = optIdx;
                g_anim.pillScale[optIdx].target = 0.965f;
                EnsureAnimationLoopRunning(hwnd);
                return 0;
            }

            bool hasOptions = false;
            {
                std::lock_guard<std::mutex> lock(g_payloadMutex);
                hasOptions = !g_currentPayload.options.empty();
            }
            int cardRelativeY = y - kPopoverCanvasReservePx;
            if (!g_settings.openWindowMode && !hasOptions && cardRelativeY >= 80 && cardRelativeY <= 122) {
                g_anim.replyBoxFocused = true;
                SetForegroundWindow(hwnd);
                SetFocus(hwnd);
                RenderGlassHud(hwnd);
                return 0;
            }

            SetCapture(hwnd);
            g_anim.isDragging = true;
            g_anim.popoverActive = false;
            g_anim.popoverProgress.target = 0.0f;
            GetCursorPos(&g_anim.dragStartScreenPt);
            g_anim.dragStartOffsetX = g_anim.dragOffsetX;
            g_anim.dragStartOffsetY = g_anim.ySpring.value;
            g_anim.ySpring.velocity = 0.0f;
            g_anim.historyCount = 0;
            g_anim.PushPointerSample(static_cast<float>(g_anim.dragStartScreenPt.y));
            EnsureAnimationLoopRunning(hwnd);
            return 0;
        }

        case WM_MOUSEMOVE: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);

            if (g_settings.robotBuddyMode) {
                if (g_anim.isDragging) {
                    POINT pt;
                    GetCursorPos(&pt);
                    float deltaX = static_cast<float>(pt.x - g_anim.dragStartScreenPt.x);
                    float deltaY = static_cast<float>(pt.y - g_anim.dragStartScreenPt.y);
                    if (std::fabs(deltaX) >= 6.0f || std::fabs(deltaY) >= 6.0f) {
                        g_anim.buddyLaunchedToCorner = true;
                        g_anim.buddyXSpring.value = g_anim.dragStartOffsetX + deltaX;
                        g_anim.buddyYSpring.value = g_anim.dragStartOffsetY + deltaY;
                        g_anim.buddyXSpring.target = g_anim.buddyXSpring.value;
                        g_anim.buddyYSpring.target = g_anim.buddyYSpring.value;
                    }
                    RenderGlassHud(hwnd);
                    return 0;
                }
                if (!g_anim.buddyHovered) {
                    g_anim.buddyHovered = true;
                    g_anim.buddyHoverSpring.target = 1.0f;
                    EnsureAnimationLoopRunning(hwnd);
                }
                TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&tme);
                return 0;
            }

            if (g_anim.isDragging) {
                POINT pt;
                GetCursorPos(&pt);
                g_anim.PushPointerSample(static_cast<float>(pt.y));

                float deltaX = static_cast<float>(pt.x - g_anim.dragStartScreenPt.x);
                float deltaY = static_cast<float>(pt.y - g_anim.dragStartScreenPt.y);
                g_anim.dragOffsetX = g_anim.dragStartOffsetX + deltaX;
                float rawOffset = g_anim.dragStartOffsetY + deltaY;
                if (rawOffset < 0.0f) {
                    g_anim.ySpring.value = -RubberBand(-rawOffset, 120.0f);
                } else {
                    g_anim.ySpring.value = rawOffset;
                }
                RenderGlassHud(hwnd);
                return 0;
            }

            float optCenterX = 270.0f;
            int hoveredOpt = HitTestOption(x, y, &optCenterX);
            bool onSummary = (hoveredOpt < 0) && HitTestSummaryRow(x, y);

            if (hoveredOpt != g_anim.hoveredOption || onSummary != g_anim.hoveredSummary) {
                g_anim.hoveredOption = hoveredOpt;
                g_anim.hoveredSummary = onSummary;

                for (int i = 0; i < 4; ++i) {
                    g_anim.pillHover[i].target = (i == hoveredOpt) ? 1.0f : 0.0f;
                }

                LLMCyclePayload payload;
                {
                    std::lock_guard<std::mutex> lock(g_payloadMutex);
                    payload = g_currentPayload;
                }

                if (hoveredOpt >= 0 && hoveredOpt < static_cast<int>(payload.options.size()) &&
                    payload.options[hoveredOpt].isLarge) {
                    if (!g_anim.popoverActive) {
                        g_anim.popoverAnchorX.Reset(optCenterX, optCenterX);
                    } else {
                        g_anim.popoverAnchorX.target = optCenterX;
                    }
                    g_anim.popoverActive = true;
                    g_anim.popoverHeading = L"Option " + std::to_wstring(hoveredOpt + 1) + L" \x00B7 " +
                                            payload.options[hoveredOpt].shortSubtitle;
                    g_anim.popoverBody = payload.options[hoveredOpt].fullText;
                    g_anim.popoverProgress.target = 1.0f;
                } else if (onSummary && payload.summaryIsLarge) {
                    float centerX = std::clamp(g_settings.hudWidthPx, 360, 960) * 0.5f;
                    if (!g_anim.popoverActive) {
                        g_anim.popoverAnchorX.Reset(centerX, centerX);
                    } else {
                        g_anim.popoverAnchorX.target = centerX;
                    }
                    g_anim.popoverActive = true;
                    g_anim.popoverHeading = payload.sourceApp + L" \x00B7 Full Cycle Description";
                    g_anim.popoverBody = payload.summaryFull;
                    g_anim.popoverProgress.target = 1.0f;
                } else {
                    g_anim.popoverActive = false;
                    g_anim.popoverProgress.target = 0.0f;
                }

                EnsureAnimationLoopRunning(hwnd);
            }

            TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            return 0;
        }

        case WM_MOUSELEAVE: {
            if (g_settings.robotBuddyMode) {
                if (g_anim.buddyHovered) {
                    g_anim.buddyHovered = false;
                    g_anim.buddyHoverSpring.target = 0.0f;
                    EnsureAnimationLoopRunning(hwnd);
                }
                return 0;
            }
            if (g_anim.hoveredOption != -1 || g_anim.pressedOption != -1 ||
                g_anim.hoveredSummary || g_anim.popoverActive) {
                g_anim.hoveredOption = -1;
                g_anim.hoveredSummary = false;
                g_anim.pressedOption = -1;
                g_anim.popoverActive = false;
                g_anim.popoverProgress.target = 0.0f;
                for (int i = 0; i < 4; ++i) {
                    g_anim.pillHover[i].target = 0.0f;
                    g_anim.pillScale[i].target = 1.0f;
                }
                EnsureAnimationLoopRunning(hwnd);
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);

            if (g_settings.robotBuddyMode) {
                if (g_anim.isDragging) {
                    ReleaseCapture();
                    g_anim.isDragging = false;
                    POINT pt;
                    GetCursorPos(&pt);
                    float totalDeltaX = static_cast<float>(pt.x - g_anim.dragStartScreenPt.x);
                    float totalDeltaY = static_cast<float>(pt.y - g_anim.dragStartScreenPt.y);
                    if (std::fabs(totalDeltaX) < 8.0f && std::fabs(totalDeltaY) < 8.0f) {
                        // Clicked the Robot Buddy before timer ran out -> bring the finished LLM to the top!
                        g_anim.buddyScaleSpring.velocity = 5.5f;
                        SendAnswerToLLM(L"", true);
                        StartDismissAnimation(hwnd);
                    } else {
                        // Dragged Robot Buddy to a custom perch spot
                        g_anim.hasCustomBuddyPerch = true;
                        g_anim.customBuddyPerchX = g_anim.buddyXSpring.value;
                        g_anim.customBuddyPerchY = g_anim.buddyYSpring.value;
                        g_anim.buddyCornerX = g_anim.customBuddyPerchX;
                        g_anim.buddyCornerY = g_anim.customBuddyPerchY;
                        g_anim.buddyScaleSpring.target = 0.88f;
                        EnsureAnimationLoopRunning(hwnd);
                    }
                }
                return 0;
            }

            if (g_anim.pressedOption >= 0) {
                int releasedIdx = HitTestOption(x, y);
                int chosenIdx = g_anim.pressedOption;
                g_anim.pressedOption = -1;
                g_anim.pillScale[chosenIdx].target = 1.0f;

                if (releasedIdx == chosenIdx) {
                    std::wstring chosen;
                    {
                        std::lock_guard<std::mutex> lock(g_payloadMutex);
                        if (chosenIdx < static_cast<int>(g_currentPayload.options.size())) {
                            chosen = g_currentPayload.options[chosenIdx].fullText;
                        }
                    }
                    if (!chosen.empty()) {
                        SendAnswerToLLM(chosen, false);
                        StartDismissAnimation(hwnd);
                        return 0;
                    }
                }
                EnsureAnimationLoopRunning(hwnd);
                return 0;
            }

            if (g_anim.isDragging) {
                ReleaseCapture();
                g_anim.isDragging = false;

                POINT pt;
                GetCursorPos(&pt);
                float totalDeltaX = static_cast<float>(pt.x - g_anim.dragStartScreenPt.x);
                float totalDeltaY = static_cast<float>(pt.y - g_anim.dragStartScreenPt.y);
                int cardRelativeY = y - kPopoverCanvasReservePx;

                // Click without dragging: in Open Window mode (or when clicking the top title bar), open the LLM chat window
                if (std::fabs(totalDeltaX) < 7.0f && std::fabs(totalDeltaY) < 7.0f) {
                    g_anim.dragOffsetX = 0.0f;
                    g_anim.ySpring.target = 0.0f;
                    if (g_settings.openWindowMode || (cardRelativeY >= 10 && cardRelativeY <= 40)) {
                        SendAnswerToLLM(L"", true);
                        StartDismissAnimation(hwnd);
                        return 0;
                    }
                }

                float releaseVel = g_anim.ComputeReleaseVelocityPxPerSec();
                g_anim.ySpring.velocity = releaseVel;

                float projectedY = g_anim.ySpring.value + ProjectMomentum(releaseVel, 0.994f);
                if ((projectedY > 42.0f || releaseVel > 180.0f) && releaseVel >= -20.0f && std::fabs(totalDeltaX) < 60.0f) {
                    g_anim.dragOffsetX = 0.0f;
                    StartDismissAnimation(hwnd, releaseVel);
                } else {
                    if (g_settings.keepOnPreviousSide && std::fabs(totalDeltaX) >= 12.0f) {
                        RECT wndRc = {};
                        if (GetWindowRect(hwnd, &wndRc)) {
                            g_anim.hasCustomPosition = true;
                            g_anim.customScreenX = wndRc.left;
                            g_anim.customMonitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
                        }
                    }
                    g_anim.dragOffsetX = 0.0f;
                    g_anim.yZeta = IsReducedMotionEnabled() ? 1.0f : 0.82f;
                    g_anim.ySpring.target = 0.0f;
                    EnsureAnimationLoopRunning(hwnd);
                }
            }
            return 0;
        }

        case WM_RBUTTONUP: {
            if (g_settings.robotBuddyMode) {
                StartDismissAnimation(hwnd);
                return 0;
            }
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            int optIdx = HitTestOption(x, y);
            if (optIdx >= 0) {
                std::wstring chosen;
                {
                    std::lock_guard<std::mutex> lock(g_payloadMutex);
                    if (optIdx < static_cast<int>(g_currentPayload.options.size())) {
                        chosen = g_currentPayload.options[optIdx].fullText;
                    }
                }
                if (!chosen.empty()) {
                    SendAnswerToLLM(chosen, true);
                    StartDismissAnimation(hwnd);
                }
            } else if (y >= kPopoverCanvasReservePx) {
                StartDismissAnimation(hwnd);
            }
            return 0;
        }

        case WM_CHAR: {
            if (!g_anim.replyBoxFocused) break;
            wchar_t ch = static_cast<wchar_t>(wParam);
            if (ch == VK_BACK) {
                if (!g_anim.replyDraft.empty()) g_anim.replyDraft.pop_back();
                RenderGlassHud(hwnd);
            } else if (ch == VK_RETURN) {
                if (!g_anim.replyDraft.empty()) {
                    std::wstring toSend = g_anim.replyDraft;
                    g_anim.replyBoxFocused = false;
                    SendAnswerToLLM(toSend, false);
                    StartDismissAnimation(hwnd);
                }
            } else if (ch == VK_ESCAPE) {
                g_anim.replyBoxFocused = false;
                StartDismissAnimation(hwnd);
            } else if (ch >= 32 && g_anim.replyDraft.size() < 512) {
                g_anim.replyDraft.push_back(ch);
                RenderGlassHud(hwnd);
            }
            return 0;
        }

        case WM_CLOSE: {
            DestroyWindow(hwnd);
            return 0;
        }

        case WM_DESTROY: {
            KillTimer(hwnd, TIMER_SPRING_FRAME);
            KillTimer(hwnd, TIMER_AUTO_DISMISS);
            for (int k = 1; k <= 4; ++k) {
                UnregisterHotKey(hwnd, 2000 + k);
            }
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static DWORD WINAPI HudHostThreadProc(LPVOID) {
    GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, nullptr);
    g_anim.InitSprings();

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = GlassHudWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kHudClassName;
    RegisterClassExW(&wc);

    g_hudHwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kHudClassName,
        L"LLM Liquid Glass HUD",
        WS_POPUP,
        0, 0, std::clamp(g_settings.hudWidthPx, 360, 960), 140 + kPopoverCanvasReservePx,
        nullptr, nullptr, wc.hInstance, nullptr
    );

    // Automatically launch the background multi-agent transcript watcher (Antigravity, Claude Code, Codex)
    {
        wchar_t userProfile[MAX_PATH] = {};
        if (GetEnvironmentVariableW(L"USERPROFILE", userProfile, MAX_PATH) > 0) {
            std::wstring scriptPath = std::wstring(userProfile) +
                L"\\.gemini\\antigravity\\scratch\\windhawk-llm-glass-hud\\tools\\agy_stop_hook.py";
            if (GetFileAttributesW(scriptPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
                std::wstring cmdLine = L"pythonw.exe \"" + scriptPath + L"\" --watch";
                STARTUPINFOW si = {};
                si.cb = sizeof(si);
                si.dwFlags = STARTF_USESHOWWINDOW;
                si.wShowWindow = SW_HIDE;
                PROCESS_INFORMATION pi = {};
                std::vector<wchar_t> mutableCmd(cmdLine.begin(), cmdLine.end());
                mutableCmd.push_back(L'\0');
                if (CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE,
                                   CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, nullptr, &si, &pi)) {
                    CloseHandle(pi.hThread);
                    CloseHandle(pi.hProcess);
                }
            }
        }
    }

    std::thread pipeListener([]() {
        PSECURITY_DESCRIPTOR pSD = nullptr;
        bool hasSD = CreateOwnerOnlySecurityDescriptor(&pSD);
        SECURITY_ATTRIBUTES sa = {};
        sa.nLength = sizeof(sa);
        sa.lpSecurityDescriptor = hasSD ? pSD : nullptr;
        sa.bInheritHandle = FALSE;

        while (!g_shuttingDown.load()) {
            HANDLE hPipe = CreateNamedPipeW(
                kPipeName,
                PIPE_ACCESS_INBOUND,
                PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                4,
                8192, 8192, 500,
                hasSD ? &sa : nullptr
            );
            if (hPipe == INVALID_HANDLE_VALUE) {
                if (g_shuttingDown.load()) break;
                Sleep(200);
                continue;
            }
            BOOL connected = ConnectNamedPipe(hPipe, nullptr)
                ? TRUE
                : (GetLastError() == ERROR_PIPE_CONNECTED);
            if (connected && !g_shuttingDown.load()) {
                wchar_t buffer[4096] = {};
                DWORD bytesRead = 0;
                if (ReadFile(hPipe, buffer, sizeof(buffer) - sizeof(wchar_t), &bytesRead, nullptr) &&
                    bytesRead >= sizeof(wchar_t) && !g_shuttingDown.load()) {
                    size_t charCount = std::min<size_t>(bytesRead / sizeof(wchar_t), 4095);
                    buffer[charCount] = L'\0';
                    LLMCyclePayload parsed = ParsePipeMessage(std::wstring(buffer, charCount));
                    bool isGenericFlash =
                        (parsed.summaryFull == L"The agent finished its cycle and is waiting for your input.") &&
                        parsed.targetSessionId.empty() &&
                        parsed.options.empty();
                    LONGLONG nowQpc = 0;
                    QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&nowQpc));
                    bool skipGenericOverwrite = false;
                    if (isGenericFlash && g_anim.qpcFreq > 0 && g_lastRichPayloadQpc > 0) {
                        double elapsedSec = static_cast<double>(nowQpc - g_lastRichPayloadQpc) /
                                            static_cast<double>(g_anim.qpcFreq);
                        if (elapsedSec < 6.0 || (g_anim.visible && !g_anim.dismissing)) {
                            skipGenericOverwrite = true;
                        }
                    }
                    if (!skipGenericOverwrite) {
                        if (!isGenericFlash) {
                            g_lastRichPayloadQpc = nowQpc;
                        }
                        {
                            std::lock_guard<std::mutex> lock(g_payloadMutex);
                            g_currentPayload = parsed;
                        }
                        HWND hwnd = g_hudHwnd;
                        if (hwnd && !g_shuttingDown.load()) {
                            PostMessageW(hwnd, WM_APP_SHOW_LLM_HUD, 0, 0);
                        }
                    }
                }
            }
            DisconnectNamedPipe(hPipe);
            CloseHandle(hPipe);
        }

        if (pSD) LocalFree(pSD);
    });

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (g_shuttingDown.load()) break;
    }

    g_shuttingDown.store(true);
    if (g_hudHwnd) {
        DestroyWindow(g_hudHwnd);
        g_hudHwnd = nullptr;
    }

    // Wake pipeListener if it is still blocked in ConnectNamedPipe
    if (pipeListener.joinable()) {
        HANDLE hWake = CreateFileW(
            kPipeName,
            GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION,
            nullptr
        );
        if (hWake != INVALID_HANDLE_VALUE) {
            CloseHandle(hWake);
        }
        pipeListener.join();
    }

    UnregisterClassW(kHudClassName, wc.hInstance);
    FreeCatBitmaps();
    GdiplusShutdown(g_gdiplusToken);
    return 0;
}

using Shell_NotifyIconW_t = BOOL(WINAPI*)(DWORD dwMessage, PNOTIFYICONDATAW lpData);
static Shell_NotifyIconW_t g_origShellNotifyIconW = nullptr;

static std::wstring GetCurrentExeLabel() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    const wchar_t* filename = PathFindFileNameW(path);
    if (StrStrIW(filename, L"Antigravity")) return L"Antigravity";
    if (StrStrIW(filename, L"Cursor")) return L"Cursor";
    if (StrStrIW(filename, L"Code")) return L"VS Code";
    if (StrStrIW(filename, L"Claude")) return L"Claude";
    if (StrStrIW(filename, L"Codex")) return L"Codex";
    if (StrStrIW(filename, L"WindowsTerminal")) return L"Terminal Agent";
    return filename;
}

BOOL WINAPI Hooked_Shell_NotifyIconW(DWORD dwMessage, PNOTIFYICONDATAW lpData) {
    if (g_settings.interceptToasts && lpData && (dwMessage == NIM_ADD || dwMessage == NIM_MODIFY)) {
        if ((lpData->uFlags & NIF_INFO) && lpData->szInfo[0] != L'\0') {
            LLMCyclePayload payload;
            payload.sourceApp = GetCurrentExeLabel();
            payload.modelBadge = L"Agent Ready";
            payload.title = (lpData->szInfoTitle[0] != L'\0')
                ? SanitizeUntrustedString(lpData->szInfoTitle, 96)
                : L"Cycle Finished";
            ExtractOptionsFromText(
                lpData->szInfo,
                payload.summaryShort,
                payload.summaryFull,
                payload.summaryIsLarge,
                payload.options
            );
            payload.targetWindow = lpData->hWnd;
            DispatchToGlassHUD(payload);
        }
    }
    return g_origShellNotifyIconW(dwMessage, lpData);
}

using FlashWindowEx_t = BOOL(WINAPI*)(PFLASHWINFO pfwi);
static FlashWindowEx_t g_origFlashWindowEx = nullptr;

BOOL WINAPI Hooked_FlashWindowEx(PFLASHWINFO pfwi) {
    if (g_settings.interceptToasts && pfwi && pfwi->hwnd && (pfwi->dwFlags & (FLASHW_TRAY | FLASHW_ALL))) {
        if (GetForegroundWindow() != pfwi->hwnd && !g_isExplorerHost) {
            wchar_t winTitle[256] = {};
            GetWindowTextW(pfwi->hwnd, winTitle, 255);

            LLMCyclePayload payload;
            payload.sourceApp = GetCurrentExeLabel();
            payload.modelBadge = L"Cycle Complete";
            payload.title = winTitle[0] ? SanitizeUntrustedString(winTitle, 96) : L"LLM Task Complete";
            payload.summaryShort = L"The agent finished its cycle and is waiting for your input.";
            payload.summaryFull = payload.summaryShort;
            payload.summaryIsLarge = false;
            payload.targetWindow = pfwi->hwnd;
            DispatchToGlassHUD(payload);
        }
    }
    return g_origFlashWindowEx(pfwi);
}

static void LoadSettings() {
    g_settings.robotBuddyMode = Wh_GetIntSetting(L"robotBuddyMode") != 0;
    g_settings.autoDismissSeconds = Wh_GetIntSetting(L"autoDismissSeconds");
    g_settings.openWindowMode = Wh_GetIntSetting(L"openWindowMode") != 0;
    g_settings.keepOnPreviousSide = Wh_GetIntSetting(L"keepOnPreviousSide") != 0;
    g_settings.glassOpacity = Wh_GetIntSetting(L"glassOpacity");
    g_settings.bottomOffsetPx = Wh_GetIntSetting(L"bottomOffsetPx");
    g_settings.hudWidthPx = Wh_GetIntSetting(L"hudWidthPx");
    g_settings.enableHotkeys = Wh_GetIntSetting(L"enableHotkeys") != 0;
    g_settings.interceptToasts = Wh_GetIntSetting(L"interceptToasts") != 0;
}

BOOL Wh_ModInit() {
    LoadSettings();

    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    if (StrStrIW(PathFindFileNameW(exePath), L"explorer.exe")) {
        g_isExplorerHost = true;
        g_pipeThread = CreateThread(nullptr, 0, HudHostThreadProc, nullptr, 0, nullptr);
        return TRUE;
    }

    HMODULE hShell32 = LoadLibraryW(L"shell32.dll");
    if (hShell32) {
        auto pShellNotify = reinterpret_cast<void*>(GetProcAddress(hShell32, "Shell_NotifyIconW"));
        if (pShellNotify) {
            Wh_SetFunctionHook(pShellNotify, reinterpret_cast<void*>(Hooked_Shell_NotifyIconW),
                               reinterpret_cast<void**>(&g_origShellNotifyIconW));
        }
    }

    HMODULE hUser32 = LoadLibraryW(L"user32.dll");
    if (hUser32) {
        auto pFlashWindowEx = reinterpret_cast<void*>(GetProcAddress(hUser32, "FlashWindowEx"));
        if (pFlashWindowEx) {
            Wh_SetFunctionHook(pFlashWindowEx, reinterpret_cast<void*>(Hooked_FlashWindowEx),
                               reinterpret_cast<void**>(&g_origFlashWindowEx));
        }
    }

    return TRUE;
}

void Wh_ModSettingsChanged() {
    LoadSettings();
    if (g_isExplorerHost && g_hudHwnd && g_anim.visible) {
        ApplyAppleGlassBackdrop(g_hudHwnd);
        RenderGlassHud(g_hudHwnd);
    }
}

void Wh_ModUninit() {
    g_shuttingDown.store(true);
    HWND hwnd = g_hudHwnd;
    if (hwnd) {
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }
    HANDLE hWake = CreateFileW(
        kPipeName,
        GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION,
        nullptr
    );
    if (hWake != INVALID_HANDLE_VALUE) {
        CloseHandle(hWake);
    }
    if (g_pipeThread) {
        WaitForSingleObject(g_pipeThread, 5000);
        CloseHandle(g_pipeThread);
        g_pipeThread = nullptr;
    }
}
