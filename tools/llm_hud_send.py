#!/usr/bin/env python3
"""
Trigger the Windhawk LLM Liquid Glass HUD from any CLI agent (Claude Code, Codex,
Antigravity hooks, or custom Python scripts).

Supports large LLM choices and descriptions:
- Pass long options as "Short Subtitle :: Full detailed explanation..." or natural
  "Short Subtitle: Full detailed explanation..." — the HUD button displays the concise
  subtitle and hovering springs open an Apple Liquid Glass popover with the full text.

Security Hardening (cybersecurity-skills/appsec & identity-access):
- Uses SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION when opening the named pipe
  to prevent named-pipe impersonation attacks.
- Sanitizes C0/C1 control characters and Unicode Bidi overrides, and enforces strict
  field length bounds before writing to the pipe.
"""

import argparse
import ctypes
import re
import sys

PIPE_NAME = r"\\.\pipe\WindhawkLLMGlassHUD"
SESSION_ID_RE = re.compile(r"^local_[A-Za-z0-9_-]{1,64}$")


def sanitize_field(value: str, max_len: int) -> str:
    cleaned: list[str] = []
    for ch in value:
        if len(cleaned) >= max_len:
            break
        code = ord(ch)
        if ch in ("\r", "\n", "\t", "|", ";"):
            if cleaned and cleaned[-1] != " ":
                cleaned.append(" ")
            continue
        # Strip C0/C1 control chars and Unicode Bidi override chars
        if code < 0x20 or (0x7F <= code <= 0x9F) or (0x202A <= code <= 0x202E) or (0x2066 <= code <= 0x2069):
            continue
        cleaned.append(ch)
    return "".join(cleaned).strip()


def get_foreground_or_console_hwnd() -> int:
    user32 = ctypes.windll.user32
    kernel32 = ctypes.windll.kernel32
    hwnd = kernel32.GetConsoleWindow()
    if not hwnd:
        hwnd = user32.GetForegroundWindow()
    return hwnd or 0


def send_to_hud(
    app: str,
    badge: str,
    title: str,
    summary: str,
    options: list[str] | None = None,
    hwnd: int | None = None,
    session_id: str | None = None,
) -> bool:
    target_hwnd = hwnd if hwnd is not None else get_foreground_or_console_hwnd()
    safe_app = sanitize_field(app, 40) or "Antigravity"
    safe_badge = sanitize_field(badge, 36) or "Cycle Complete"
    safe_title = sanitize_field(title, 96) or "Agent finished cycle"
    safe_summary = sanitize_field(summary, 900) or "Ready for your next instruction."
    safe_opts = [sanitize_field(opt, 650) for opt in (options or [])[:4]]
    safe_opts = [opt for opt in safe_opts if opt]

    clean_session = (session_id or "").strip()
    if clean_session and SESSION_ID_RE.match(clean_session):
        target_field = f"{target_hwnd:X}@{clean_session}"
    else:
        target_field = f"{target_hwnd:X}"

    opts_str = ";".join(safe_opts)
    payload = f"{safe_app}|{safe_badge}|{safe_title}|{safe_summary}|{target_field}|{opts_str}"
    encoded = payload.encode("utf-16-le")

    GENERIC_WRITE = 0x40000000
    OPEN_EXISTING = 3
    FLAGS_SQOS_IDENTIFICATION = 0x00100000 | 0x00010000
    kernel32 = ctypes.windll.kernel32

    handle = kernel32.CreateFileW(
        PIPE_NAME,
        GENERIC_WRITE,
        0,
        None,
        OPEN_EXISTING,
        FLAGS_SQOS_IDENTIFICATION,
        None,
    )
    if handle == -1 or handle == 0xFFFFFFFFFFFFFFFF:
        print(
            "Windhawk LLM Glass HUD pipe not running yet. Ensure the mod is enabled in Windhawk.",
            file=sys.stderr,
        )
        return False

    bytes_written = ctypes.c_ulong(0)
    ok = kernel32.WriteFile(
        handle,
        encoded,
        len(encoded),
        ctypes.byref(bytes_written),
        None,
    )
    kernel32.CloseHandle(handle)
    return bool(ok)


def main() -> int:
    parser = argparse.ArgumentParser(description="Send an LLM cycle notification to Windhawk Glass HUD")
    parser.add_argument("--app", default="Antigravity", help="Source LLM app name")
    parser.add_argument("--badge", default="Cycle Complete", help="Right-side status badge")
    parser.add_argument("--title", default="Agent finished cycle", help="Primary heading")
    parser.add_argument(
        "--summary",
        default="Ready for your next instruction or option selection.",
        help="Supporting text under heading (hoverable if long)",
    )
    parser.add_argument(
        "--options",
        nargs="*",
        default=[],
        help="Optional multiple-choice pills (1-4). Use 'Subtitle :: Full text' or 'Subtitle: Full text' for large choices.",
    )
    parser.add_argument("--hwnd", type=lambda x: int(x, 0), default=None, help="Target window HWND (hex or int)")
    parser.add_argument(
        "--session-id",
        default=None,
        help="Optional Claude Desktop hostSessionId (local_<uuid>) to route replies to the exact chat tab",
    )

    args = parser.parse_args()
    success = send_to_hud(
        app=args.app,
        badge=args.badge,
        title=args.title,
        summary=args.summary,
        options=args.options,
        hwnd=args.hwnd,
        session_id=args.session_id,
    )
    return 0 if success else 1


if __name__ == "__main__":
    raise SystemExit(main())
