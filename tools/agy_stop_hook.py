#!/usr/bin/env python3
"""
Universal LLM Cycle Stop-Hook & Live Transcript Watcher for Windhawk LLM Liquid Glass HUD.

Supports:
1. Hook Mode (stdin JSON):
   - Antigravity (`hooks.json` -> `Stop` event)
   - Claude Code / Claude Desktop (`~/.claude/settings.json` -> `Stop` event)
   - Codex (`config.toml` -> `notify`)
2. Live Multi-Agent Watcher Daemon (`--watch`):
   - Automatically tails active transcripts across Antigravity (`~/.gemini/antigravity/brain`),
     Claude Code (`~/.claude/projects`), and Codex (`~/.codex/sessions`) and triggers the
     Apple Liquid Glass HUD the moment any LLM finishes a cycle—even for already-open chats!
"""

import glob
import json
import os
import re
import subprocess
import sys
import time
import ctypes

# Ensure we can import llm_hud_send from the same directory
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from llm_hud_send import send_to_hud


def find_window_by_process_Keywords(keywords: list[str]) -> int:
    user32 = ctypes.windll.user32
    found_hwnd = 0

    WNDENUMPROC = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)

    def enum_cb(hwnd, _lparam):
        nonlocal found_hwnd
        if not user32.IsWindowVisible(hwnd):
            return True
        length = user32.GetWindowTextLengthW(hwnd)
        if length <= 0:
            return True
        buf = ctypes.create_unicode_buffer(length + 1)
        user32.GetWindowTextW(hwnd, buf, length + 1)
        title = buf.value.lower()
        for kw in keywords:
            if kw.lower() in title:
                found_hwnd = hwnd
                return False
        return True

    user32.EnumWindows(WNDENUMPROC(enum_cb), 0)
    return found_hwnd


def extract_choices_and_summary(markdown_text: str) -> tuple[str, str, list[str]]:
    lines = [line.strip() for line in markdown_text.splitlines() if line.strip()]
    if not lines:
        return ("Agent finished turn", "Ready for your next instruction.", [])

    options: list[str] = []
    summary_lines: list[str] = []

    # Match numbered options (1. ..., 1) ...) or lettered options (A. ..., A) ...)
    opt_pattern = re.compile(r"^(?:[1-4]|\[[1-4]\]|[A-D])[.):]\s+(.+)$")

    for line in lines:
        clean = re.sub(r"[*_`#]+", "", line).strip()
        if not clean:
            continue
        m = opt_pattern.match(clean)
        if m and len(options) < 4:
            options.append(m.group(1).strip())
        elif (
            not line.startswith("```")
            and not line.startswith("|")
            and not line.startswith("<")
            and clean != "---"
        ):
            summary_lines.append(clean)

    title = summary_lines[0][:90] if summary_lines else "Agent finished turn"
    summary = " ".join(summary_lines[1:4]) if len(summary_lines) > 1 else title
    return (title, summary, options)


def parse_antigravity_line(line: str) -> tuple[bool, str]:
    """Returns (is_final_turn_response, markdown_text) for an Antigravity transcript line."""
    if '"PLANNER_RESPONSE"' not in line:
        return (False, "")
    try:
        obj = json.loads(line)
        if obj.get("type") != "PLANNER_RESPONSE" or obj.get("status") != "DONE":
            return (False, "")
        # Final response of a turn has no tool_calls!
        if obj.get("tool_calls"):
            return (False, "")
        content = (obj.get("content") or "").strip()
        return (bool(content), content)
    except Exception:
        return (False, "")


def parse_claude_line(line: str) -> tuple[bool, str, list[str]]:
    """Returns (is_final_turn_response, markdown_text, explicit_options) for a Claude transcript line."""
    if '"assistant"' not in line:
        return (False, "", [])
    try:
        obj = json.loads(line)
        msg = obj.get("message") or {}
        if msg.get("role") != "assistant":
            return (False, "", [])
        content_list = msg.get("content")
        if not isinstance(content_list, list):
            return (False, "", [])

        # Check if Claude called AskUserQuestion or finished with end_turn text
        explicit_opts: list[str] = []
        texts: list[str] = []
        has_other_tool = False

        for block in content_list:
            if not isinstance(block, dict):
                continue
            btype = block.get("type")
            if btype == "text":
                t = (block.get("text") or "").strip()
                if t:
                    texts.append(t)
            elif btype == "tool_use":
                if block.get("name") == "AskUserQuestion":
                    inp = block.get("input") or {}
                    qs = inp.get("questions") or []
                    if qs and isinstance(qs[0], dict):
                        q_title = qs[0].get("question", "")
                        if q_title:
                            texts.append(q_title)
                        for o in (qs[0].get("options") or [])[:4]:
                            if isinstance(o, dict):
                                lbl = o.get("label", "")
                                desc = o.get("description", "")
                                explicit_opts.append(f"{lbl} :: {desc}" if desc else lbl)
                else:
                    has_other_tool = True

        stop_reason = msg.get("stop_reason")
        if explicit_opts:
            return (True, "\n".join(texts), explicit_opts)
        if stop_reason == "end_turn" and not has_other_tool and texts:
            return (True, "\n".join(texts), [])
        return (False, "", [])
    except Exception:
        return (False, "", [])


def parse_codex_line(line: str) -> tuple[bool, str]:
    """Returns (is_final_turn_response, markdown_text) for a Codex rollout jsonl line."""
    if '"task_complete"' not in line:
        return (False, "")
    try:
        obj = json.loads(line)
        payload = obj.get("payload") or {}
        if payload.get("type") != "task_complete":
            return (False, "")
        msg = (payload.get("last_agent_message") or "").strip()
        if not msg or msg.startswith("{"):
            return (False, "")
        return (True, msg)
    except Exception:
        return (False, "")


def resolve_transcript_candidates(payload: dict) -> list[tuple[str, str]]:
    """Returns list of (app_label, path)."""
    candidates: list[tuple[str, str]] = []
    # 1. Claude Code snake_case transcript_path
    claude_tp = payload.get("transcript_path", "")
    if claude_tp:
        candidates.append(("Claude Code", claude_tp))

    # 2. Antigravity camelCase transcriptPath / conversationId
    agy_tp = payload.get("transcriptPath", "")
    if agy_tp:
        candidates.append(("Antigravity", agy_tp))
    conv_id = payload.get("conversationId", "")
    if conv_id:
        brain_log = os.path.expanduser(
            os.path.join(
                "~",
                ".gemini",
                "antigravity",
                "brain",
                conv_id,
                ".system_generated",
                "logs",
                "transcript.jsonl",
            )
        )
        candidates.append(("Antigravity", brain_log))
    return candidates


def read_last_response_from_candidates(payload: dict) -> tuple[str, str, list[str]]:
    for app_label, path in resolve_transcript_candidates(payload):
        if not path or not os.path.isfile(path):
            continue
        try:
            last_text = ""
            last_opts: list[str] = []
            with open(path, "r", encoding="utf-8", errors="ignore") as f:
                for line in f:
                    if app_label == "Claude Code":
                        ok, txt, opts = parse_claude_line(line)
                        if ok:
                            last_text, last_opts = txt, opts
                    else:
                        ok, txt = parse_antigravity_line(line)
                        if ok:
                            last_text = txt
                        elif '"PLANNER_RESPONSE"' in line:
                            try:
                                obj = json.loads(line)
                                c = (obj.get("content") or "").strip()
                                if c:
                                    last_text = c
                            except Exception:
                                pass
            if last_text:
                return (app_label, last_text, last_opts)
        except Exception:
            pass
    return ("Antigravity", "", [])


def ensure_watcher_running() -> None:
    """Spawns the background multi-agent transcript watcher if not already running."""
    lock_file = os.path.join(os.environ.get("TEMP", "."), "windhawk_llm_hud_watcher.lock")
    try:
        if os.path.isfile(lock_file):
            mtime = os.path.getmtime(lock_file)
            if time.time() - mtime < 15:
                return
    except Exception:
        pass

    try:
        CREATE_NO_WINDOW = 0x08000000
        DETACHED_PROCESS = 0x00000008
        subprocess.Popen(
            [sys.executable, os.path.abspath(__file__), "--watch"],
            creationflags=CREATE_NO_WINDOW | DETACHED_PROCESS,
            close_fds=True,
        )
    except Exception:
        pass


def run_watcher_daemon() -> int:
    lock_file = os.path.join(os.environ.get("TEMP", "."), "windhawk_llm_hud_watcher.lock")
    home = os.path.expanduser("~")
    file_offsets: dict[str, int] = {}
    last_dispatched_sig = ""

    def scan_patterns() -> list[tuple[str, str, list[str]]]:
        return [
            (
                "Antigravity",
                os.path.join(home, ".gemini", "antigravity", "brain", "*", ".system_generated", "logs", "transcript.jsonl"),
                ["Antigravity"],
            ),
            (
                "Claude Code",
                os.path.join(home, ".claude", "projects", "*", "*.jsonl"),
                ["Claude", "Cursor", "Visual Studio Code", "Windows PowerShell", "Terminal"],
            ),
            (
                "Codex",
                os.path.join(home, ".codex", "sessions", "*", "*", "*", "*.jsonl"),
                ["Codex", "Terminal"],
            ),
        ]

    # Initialize offsets to current end-of-file so we only trigger on NEW completed cycles
    now = time.time()
    for _app, pattern, _kws in scan_patterns():
        for path in glob.glob(pattern):
            try:
                if now - os.path.getmtime(path) < 86400:
                    file_offsets[path] = os.path.getsize(path)
            except Exception:
                pass

    while True:
        try:
            with open(lock_file, "w", encoding="utf-8") as lf:
                lf.write(str(os.getpid()))
        except Exception:
            pass

        now = time.time()
        for app_name, pattern, win_keywords in scan_patterns():
            for path in glob.glob(pattern):
                try:
                    mtime = os.path.getmtime(path)
                    if now - mtime > 3600:
                        continue
                    size = os.path.getsize(path)
                    if path not in file_offsets:
                        # Newly created session file: start from 0
                        file_offsets[path] = 0
                    if size <= file_offsets[path]:
                        continue

                    with open(path, "r", encoding="utf-8", errors="ignore") as f:
                        f.seek(file_offsets[path])
                        new_data = f.read()
                        file_offsets[path] = f.tell()

                    for line in new_data.splitlines():
                        line = line.strip()
                        if not line:
                            continue
                        triggered = False
                        text = ""
                        explicit_opts: list[str] = []

                        if app_name == "Antigravity":
                            triggered, text = parse_antigravity_line(line)
                        elif app_name == "Claude Code":
                            triggered, text, explicit_opts = parse_claude_line(line)
                        elif app_name == "Codex":
                            triggered, text = parse_codex_line(line)

                        if triggered and text:
                            title, summary, parsed_opts = extract_choices_and_summary(text)
                            final_opts = explicit_opts if explicit_opts else parsed_opts
                            sig = f"{app_name}|{title}|{summary}|{';'.join(final_opts)}"
                            if sig != last_dispatched_sig:
                                last_dispatched_sig = sig
                                badge = f"{len(final_opts)} Choices · {app_name}" if final_opts else "Cycle Complete"
                                target_hwnd = find_window_by_process_Keywords(win_keywords)
                                send_to_hud(
                                    app=app_name,
                                    badge=badge,
                                    title=title,
                                    summary=summary,
                                    options=final_opts,
                                    hwnd=target_hwnd if target_hwnd else None,
                                )
                except Exception:
                    pass

        time.sleep(0.35)


def main() -> int:
    if "--watch" in sys.argv:
        return run_watcher_daemon()

    ensure_watcher_running()

    try:
        raw_stdin = sys.stdin.read() if not sys.stdin.isatty() else ""
        payload = json.loads(raw_stdin) if raw_stdin.strip() else {}
    except Exception:
        payload = {}

    app_label, last_response, explicit_opts = read_last_response_from_candidates(payload)

    if last_response:
        title, summary, parsed_opts = extract_choices_and_summary(last_response)
        options = explicit_opts if explicit_opts else parsed_opts
    else:
        last_user = payload.get("lastUserInput") or payload.get("prompt") or ""
        title = "Finished: " + (last_user[:60] if last_user else "LLM Cycle Complete")
        summary = f"{app_label} finished its cycle and is ready for your response."
        options = []

    badge = f"{len(options)} Choices" if options else "Cycle Complete"
    send_to_hud(
        app=app_label,
        badge=badge,
        title=title,
        summary=summary,
        options=options,
    )

    # Output valid Stop hook JSON contract so Antigravity / Claude Code stops normally
    if "transcriptPath" in payload or "conversationId" in payload:
        sys.stdout.write(json.dumps({"decision": "allow"}) + "\n")
        sys.stdout.flush()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
