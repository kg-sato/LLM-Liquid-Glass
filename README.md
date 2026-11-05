# LLM Liquid Glass HUD — Windhawk Mod

An Apple-inspired **Liquid Glass floating HUD** positioned near the bottom taskbar (macOS Spotlight / visionOS glass bar style) for Windows 11. Whenever an LLM or coding agent finishes a cycle—whether in **Antigravity, Cursor, VS Code, Claude Desktop, Windows Terminal**, or a background CLI like **Claude Code / Codex**—the HUD smoothly springs up so you can answer multiple-choice prompts or send a quick reply **without tabbing away from your current work**.

---

## ✨ Design, Animation & Security Highlights

Built using principles from Apple's *Designing Fluid Interfaces* (WWDC), Emil Kowalski's Design Engineering guidelines, and `skilldb.dev/skills/cybersecurity-skills`:

1. **Smart Subtitle Extraction & Origin-Aware Hover Popover for Large LLM Outputs**:
   - When an LLM provides a long option or multi-sentence description, the button automatically displays the concise **subtitle/heading** (before `::`, `:`, `—`, `-`, or smart word-boundary truncation) with a subtle `⋯` indicator.
   - Hovering any truncated option pill or long summary description springs open an **origin-aware Apple Liquid Glass Popover** above the HUD showing the full multi-line text.
   - Implements Emil Kowalski's **instant neighbour tooltip rule**: once the hover popover is open, moving across adjacent options updates and slides its anchor origin immediately with zero re-entry delay.
2. **True Apple Liquid Glass Material**:
   - High-DPI 32-bit per-pixel ARGB layered rendering with a top-edge 1.25px specular light catch-rim over a warm obsidian translucent gradient so foreground text retains crisp ≥4.5:1 vibrancy over any desktop wallpaper or code editor.
3. **Adaptive Dual-State Layout**:
   - **Options Mode**: When the LLM provides choices (`1)`, `2)`, `A)`, or passed via CLI/hook), renders a 2×2 pill grid with keyboard shortcuts (`Alt+1` .. `Alt+4`).
     - **Left-Click (or `Alt+1..4`)**: Dispatches your full choice to the LLM window in the background and immediately restores focus to your active app (<35ms seamless handoff).
     - **Right-Click**: Copies the option and brings the LLM window to the foreground so you can review before sending.
   - **Summary + Spotlight Reply Mode**: When no options are present, renders the cycle completion summary directly underneath the header along with an inline frosted glass reply field (`Enter` to send to the LLM without switching windows, `Esc` to dismiss).
4. **120Hz QPC Fluid Spring Physics & 1:1 Gesture Dismissal**:
   - Uses `QueryPerformanceCounter` (QPC) delta-time integration at up to 120fps (`8ms` frame step).
   - Never animates from `scale(0)`: enters from `scale(0.95)` + `translateY` anchored at bottom-center (`50% 100%`).
   - 1:1 pointer-captured drag with 4-sample ring-buffer velocity handoff, progressive rubber-banding (`c = 0.55`), and exponential decay momentum projection (`decelerationRate = 0.994`).
5. **Security & Privacy Hardened (`cybersecurity-skills` Verified)**:
   - **IAM / Least-Privilege Named Pipe DACL**: Locked exclusively to the current logon user SID (`TOKEN_USER`) + `PIPE_REJECT_REMOTE_CLIENTS`.
   - **Confused-Deputy Target Window Verification**: Verifies `targetWindow` belongs to an allowlisted LLM/IDE/Terminal executable before dispatching keystrokes.
   - **Privacy-by-Design Clipboard Preservation**: Saves and restores your previous clipboard contents and tags temporary writes with `ExcludeClipboardContentFromMonitorProcessing = 1` and `CanIncludeInClipboardHistory = 0`.
   - **Anti-Clickjacking Grace Window**: Enforces a 220ms grace window after popup appearance.

---

## 🚀 Installation

### 1. Install in Windhawk
1. Open **Windhawk** → click **Create a new mod** (or **Mods** → **New Mod** in upper right).
2. Replace the template code with the contents of [`llm-liquid-glass-hud.wh.cpp`](./llm-liquid-glass-hud.wh.cpp).
3. Click **Compile Mod**.

### 2. Trigger from CLI / Claude Code / Codex / Python Hooks
Use the included zero-dependency script [`tools/llm_hud_send.py`](./tools/llm_hud_send.py) to pop up the HUD at the end of any script or agent turn:

```powershell
# Trigger Options Mode with large multi-sentence options (automatically extracts subtitles + hover popover)
python tools/llm_hud_send.py `
  --app "Antigravity" `
  --badge "4 Detailed Choices" `
  --title "Architecture decision required" `
  --summary "Schema diff affects users and sessions across 3 production shards (1.4M rows) with active foreign key constraints and zero-downtime SLA requirements." `
  --options `
    "Zero-Downtime Dual-Write :: Creates shadow columns with dual-write triggers, backfills 1.4M rows in 10k batches, and swaps views atomically with zero lock contention." `
    "Blue-Green Replica Swap :: Applies DDL on read-replica, verifies checksums against primary WAL stream, and promotes replica via connection pooler cutover." `
    "Maintenance Lock Migration :: Acquires ACCESS EXCLUSIVE table lock inside a single transaction and runs ALTER TABLE directly (~9s)." `
    "Dry-Run Explain Plan Only :: Generates the full SQL migration script and runs EXPLAIN ANALYZE inside a rolled-back transaction on staging."

# Trigger Summary Mode (text under header + inline Spotlight reply input)
python tools/llm_hud_send.py `
  --app "Claude Code" `
  --badge "Cycle Complete · 1.4s" `
  --title "Compiled Windhawk C++ Mod & Hooks" `
  --summary "All hooks verified in explorer.exe and IDE processes. Type below to send a follow-up."
```

### 3. Automatic Triggering After Every Antigravity / Claude Code Turn (`Stop` Hook)
Why doesn't Windows automatically know when an LLM finishes a turn? Because Electron/IDE agents (like Antigravity and Cursor) do not emit legacy Win32 `Shell_NotifyIconW` tray balloons when you are actively looking at the window. Instead, Antigravity and Claude Code provide a native **`Stop` lifecycle hook** that fires the exact millisecond an agent finishes its turn (`NO_TOOL_CALL`) and passes the conversation transcript path on `stdin`.

We included [`tools/agy_stop_hook.py`](./tools/agy_stop_hook.py), which automatically reads the last assistant message from the transcript, extracts any numbered/lettered options (or falls back to Summary Mode), and triggers the Liquid Glass HUD.

Add this to your `hooks.json` (e.g. `~/.gemini/antigravity/hooks/hooks.json` or `.gemini/hooks/hooks.json`):

```json
{
  "hooks": {
    "Stop": [
      {
        "matcher": "*",
        "hooks": [
          {
            "type": "command",
            "command": "python \"C:/Users/kgsat/.gemini/antigravity/scratch/windhawk-llm-glass-hud/tools/agy_stop_hook.py\"",
            "timeout": 5
          }
        ]
      }
    ]
  }
}
```

---

## ⚙️ Mod Settings (Configurable in Windhawk UI)

| Setting | Default | Description |
| :--- | :--- | :--- |
| `glassOpacity` | `215` | Opacity (`0-255`) of the smoky Apple glass tint |
| `bottomOffsetPx` | `64` | Distance in pixels above the bottom screen edge (sits cleanly above the taskbar) |
| `hudWidthPx` | `540` | Width of the floating Spotlight-style glass bar |
| `autoDismissSeconds` | `18` | Seconds before auto-dismissing (`0` = stay until answered or swiped away) |
| `enableHotkeys` | `true` | Enables global `Alt+1` .. `Alt+4` hotkeys while the HUD is visible |
| `interceptToasts` | `true` | Automatically hooks `Shell_NotifyIconW` & `FlashWindowEx` in Antigravity, Cursor, VS Code, Claude, and Windows Terminal |
| `showDemoOnStartup` | `true` | Immediately pops up a welcome Liquid Glass HUD when the mod loads or settings are saved in Windhawk |

---

## 📄 License

MIT License.
