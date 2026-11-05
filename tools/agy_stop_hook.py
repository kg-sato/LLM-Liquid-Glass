#!/usr/bin/env python3
"""
Universal LLM Cycle Stop-Hook & Live Transcript Watcher for Windhawk LLM Liquid Glass HUD.

Supports:
1. Hook Mode (stdin JSON / CLI args):
   - Antigravity (`hooks.json` -> `Stop` event)
   - Claude Code / Claude Desktop (`~/.claude/settings.json` -> `Stop` event)
   - Codex (`config.toml` -> `notify`)
2. Live Multi-Agent Watcher Daemon (`--watch`):
   - Automatically tails active transcripts across Antigravity (`~/.gemini/antigravity/brain`),
     Claude Code (`~/.claude/projects`), and Codex (`~/.codex/sessions` + `state_5.sqlite`) and
     triggers the Apple Liquid Glass HUD the moment any LLM finishes a cycle—even for already-open chats!
"""

import ctypes
import ctypes.wintypes
import glob
import json
import os
import re
import sqlite3
import subprocess
import sys
import time

# Ensure we can import llm_hud_send from the same directory
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from llm_hud_send import send_to_hud


CHOICE_INTRO_CUE_RE = re.compile(
    r"(?:\?|"
    r"\b(?:pick|choose|select|prefer|which|option|options|approach|approaches|"
    r"two ways|three ways|four ways|ways to|paths|strategies|variants|directions|"
    r"routes|modes|would you like|do you want|let me know which|tell me which|"
    r"reply with|respond with|choices)\b)",
    re.IGNORECASE,
)

INFORMATIONAL_HEADER_RE = re.compile(
    r"^(?:next\b|steps?\b|how (?:it|to)\b|what (?:i|it|we)\b|things that\b|"
    r"tested\b|not tested\b|summary\b|changes\b|files\b|notes\b|todo\b|to do\b|"
    r"deliverables?\b|plan\b|rules?\b|constraints?\b|limits?\b|evidence\b|"
    r"audit\b|findings?\b|issues?\b|blockers?\b|reasons?\b|prerequisites?\b)",
    re.IGNORECASE,
)


def strip_markdown_links(text: str) -> str:
    """Converts `[label](url)` or `[label](<url>)` into `label` for clean HUD display."""
    return re.sub(r"\[([^\]]+)\]\([^)]+\)", r"\1", text)


def find_window_by_process_or_title(exe_names: list[str], title_keywords: list[str]) -> int:
    """Finds a visible top-level window matching either the process executable name or window title."""
    user32 = ctypes.windll.user32
    kernel32 = ctypes.windll.kernel32
    found_by_exe = 0
    found_by_title = 0

    exe_lower = {e.lower() for e in exe_names}
    kw_lower = [k.lower() for k in title_keywords]

    WNDENUMPROC = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)

    def enum_cb(hwnd, _lparam):
        nonlocal found_by_exe, found_by_title
        if not user32.IsWindowVisible(hwnd):
            return True
        length = user32.GetWindowTextLengthW(hwnd)
        if length <= 0:
            return True
        buf = ctypes.create_unicode_buffer(length + 1)
        user32.GetWindowTextW(hwnd, buf, length + 1)
        title = buf.value.lower()

        pid = ctypes.wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        if pid.value:
            hproc = kernel32.OpenProcess(0x1000, False, pid.value)  # PROCESS_QUERY_LIMITED_INFORMATION
            if hproc:
                ebuf = ctypes.create_unicode_buffer(260)
                sz = ctypes.wintypes.DWORD(260)
                if kernel32.QueryFullProcessImageNameW(hproc, 0, ebuf, ctypes.byref(sz)):
                    base_exe = os.path.basename(ebuf.value).lower()
                    if base_exe in exe_lower and not found_by_exe:
                        found_by_exe = hwnd
                kernel32.CloseHandle(hproc)

        if not found_by_title:
            for kw in kw_lower:
                if kw in title:
                    found_by_title = hwnd
                    break

        return False if found_by_exe else True

    user32.EnumWindows(WNDENUMPROC(enum_cb), 0)
    return found_by_exe or found_by_title


def extract_choices_and_summary(markdown_text: str) -> tuple[str, str, list[str]]:
    """
    Context-aware choice extractor that distinguishes genuine user-selectable choices
    from informational numbered lists, status reports, or next-step summaries.
    """
    raw_lines = markdown_text.splitlines()
    if not raw_lines:
        return ("Agent finished turn", "Ready for your next instruction.", [])

    outside_code_lines: list[tuple[int, str, str]] = []
    in_fence = False
    for raw in raw_lines:
        stripped = raw.strip()
        if stripped.startswith("```"):
            in_fence = not in_fence
            continue
        if not in_fence and stripped:
            indent = len(raw) - len(raw.lstrip(" \t"))
            no_links = strip_markdown_links(stripped)
            clean = re.sub(r"[*_`#]+", "", no_links).strip()
            if clean:
                outside_code_lines.append((indent, no_links, clean))

    if not outside_code_lines:
        return ("Agent finished turn", "Ready for your next instruction.", [])

    raw_opt_re = re.compile(r"^(?:[1-4]|\[[1-4]\]|[A-D])[.):]\s+(.+)$")
    clean_opt_re = re.compile(r"^([1-4]|\[[1-4]\]|[A-D])[.):]\s+(.+)$")

    blocks: list[dict] = []
    current_block: dict | None = None
    non_opt_lines: list[str] = []

    for idx, (indent, stripped, clean) in enumerate(outside_code_lines):
        # Only top-level list items (indent <= 1) can be user-selectable choices
        m_clean = clean_opt_re.match(clean) if indent <= 1 else None
        m_raw = raw_opt_re.match(stripped) if indent <= 1 else None
        if m_clean and m_raw:
            marker_raw = m_clean.group(1).strip("[]")
            item_num = (
                int(marker_raw)
                if marker_raw.isdigit()
                else (ord(marker_raw.upper()) - ord("A") + 1)
            )
            raw_body = m_raw.group(1).strip()
            clean_body = m_clean.group(2).strip()

            # Convert "**Short Heading.** Long explanation" -> "Short Heading :: Long explanation"
            # so the C++ HUD pill displays the clean heading and hover shows the full text!
            bold_lead = re.match(
                r"^\*\*([^*]{2,48}?)[.:—–-]?\*\*\s*(?:[—–:-]\s*)?(.+)$", raw_body
            )
            if bold_lead:
                heading = re.sub(r"[*_`#]+", "", bold_lead.group(1)).strip(" .:-\t")
                rest = re.sub(r"[*_`#]+", "", bold_lead.group(2)).strip()
                formatted_item = f"{heading} :: {rest}" if rest else heading
                has_bold_lead = True
            else:
                formatted_item = clean_body
                has_bold_lead = False

            if current_block is None:
                prev_line = non_opt_lines[-1] if non_opt_lines else ""
                current_block = {
                    "prev_line": prev_line,
                    "start_idx": idx,
                    "end_idx": idx,
                    "nums": [item_num],
                    "items": [formatted_item],
                    "bold_leads": [has_bold_lead],
                }
            else:
                current_block["end_idx"] = idx
                current_block["nums"].append(item_num)
                current_block["items"].append(formatted_item)
                current_block["bold_leads"].append(has_bold_lead)
        else:
            if current_block is not None:
                blocks.append(current_block)
                current_block = None
            if (
                not stripped.startswith("|")
                and not stripped.startswith("<")
                and clean != "---"
            ):
                non_opt_lines.append(clean)

    if current_block is not None:
        blocks.append(current_block)

    total_lines = len(outside_code_lines)
    chosen_options: list[str] = []

    for b in reversed(blocks):
        nums = b["nums"]
        items = b["items"]
        prev_line = b["prev_line"]
        after_lines = [
            outside_code_lines[i][2] for i in range(b["end_idx"] + 1, total_lines)
        ]
        after_lines_count = len(after_lines)
        immediately_after = after_lines[0] if after_lines else ""

        is_sequential = nums == list(range(1, len(nums) + 1))
        is_valid_count = 2 <= len(items) <= 4
        is_near_end = after_lines_count <= 3
        not_info_header = not bool(INFORMATIONAL_HEADER_RE.search(prev_line))
        intro_has_cue = bool(CHOICE_INTRO_CUE_RE.search(prev_line))
        outro_has_cue = after_lines_count == 1 and bool(
            CHOICE_INTRO_CUE_RE.search(immediately_after)
        )
        all_bold_titled = all(b["bold_leads"])

        if (
            is_sequential
            and is_valid_count
            and is_near_end
            and not_info_header
            and (intro_has_cue or outro_has_cue or (all_bold_titled and after_lines_count <= 2))
        ):
            chosen_options = items
            break

    # If no numbered block was selected, include any skipped numbered items back into summary fallback
    if not non_opt_lines and blocks:
        non_opt_lines = [it.replace(" :: ", " — ") for it in blocks[-1]["items"]]

    title = non_opt_lines[0][:90] if non_opt_lines else "Agent finished turn"
    summary = " ".join(non_opt_lines[1:4]) if len(non_opt_lines) > 1 else title
    return (title, summary, chosen_options)


def parse_antigravity_line(line: str) -> tuple[bool, str]:
    """Returns (is_final_turn_response, markdown_text) for an Antigravity transcript line."""
    if '"PLANNER_RESPONSE"' not in line:
        return (False, "")
    try:
        obj = json.loads(line)
        if obj.get("type") != "PLANNER_RESPONSE" or obj.get("status") != "DONE":
            return (False, "")
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
                if block.get("name") in ("AskUserQuestion", "ask_user_question"):
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


def parse_codex_line(line: str) -> tuple[bool, str, list[str]]:
    """Returns (is_final_turn_response, markdown_text, explicit_options) for a Codex rollout jsonl line."""
    if (
        '"task_complete"' not in line
        and '"request_user_input"' not in line
        and '"ask_user_question"' not in line
    ):
        return (False, "", [])
    try:
        obj = json.loads(line)
        payload = obj.get("payload") or {}
        if not isinstance(payload, dict):
            return (False, "", [])

        ptype = payload.get("type")
        if ptype == "task_complete":
            msg = (payload.get("last_agent_message") or "").strip()
            if (
                not msg
                or msg.startswith("{")
                or msg.startswith("AUTOSWARM")
                or "sent to the parent" in msg.lower()
            ):
                return (False, "", [])
            return (True, msg, [])

        if ptype in ("function_call", "custom_tool_call") and payload.get("name") in (
            "request_user_input",
            "ask_user_question",
            "AskUserQuestion",
        ):
            raw_args = payload.get("arguments") or payload.get("input") or "{}"
            args = json.loads(raw_args) if isinstance(raw_args, str) else raw_args
            qs = (args.get("questions") or []) if isinstance(args, dict) else []
            if qs and isinstance(qs[0], dict):
                q_title = qs[0].get("question") or qs[0].get("prompt") or "Codex needs your input"
                explicit_opts: list[str] = []
                for o in (qs[0].get("options") or [])[:4]:
                    if isinstance(o, dict):
                        lbl = o.get("label") or o.get("title") or ""
                        desc = o.get("description") or ""
                        explicit_opts.append(f"{lbl} :: {desc}" if desc else lbl)
                    elif isinstance(o, str):
                        explicit_opts.append(o)
                if explicit_opts:
                    return (True, q_title, explicit_opts)
        return (False, "", [])
    except Exception:
        return (False, "", [])


def get_codex_active_rollout_paths(home: str) -> list[str]:
    """Queries ~/.codex/state_5.sqlite for recently updated non-subagent rollout paths."""
    paths: list[str] = []
    db_path = os.path.join(home, ".codex", "state_5.sqlite")
    if not os.path.isfile(db_path):
        return paths
    try:
        con = sqlite3.connect(f"file:{db_path}?mode=ro", uri=True, timeout=0.5)
        rows = con.execute(
            "SELECT rollout_path, source FROM threads ORDER BY updated_at DESC LIMIT 12"
        ).fetchall()
        con.close()
        for rpath, source in rows:
            if not rpath:
                continue
            if source and '"subagent"' in str(source):
                continue
            clean_p = str(rpath)
            if clean_p.startswith("\\\\?\\"):
                clean_p = clean_p[4:]
            if os.path.isfile(clean_p):
                paths.append(clean_p)
    except Exception:
        pass
    return paths


def resolve_transcript_candidates(payload: dict) -> list[tuple[str, str]]:
    """Returns list of (app_label, path)."""
    candidates: list[tuple[str, str]] = []
    claude_tp = payload.get("transcript_path", "")
    if claude_tp:
        candidates.append(("Claude Code", claude_tp))

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

    def gather_sources() -> list[tuple[str, list[str], list[str], list[str]]]:
        """Returns list of (app_name, file_paths, exe_names, title_keywords)."""
        agy_files = glob.glob(
            os.path.join(
                home,
                ".gemini",
                "antigravity",
                "brain",
                "*",
                ".system_generated",
                "logs",
                "transcript.jsonl",
            )
        )
        claude_files = glob.glob(os.path.join(home, ".claude", "projects", "*", "*.jsonl"))
        codex_glob = glob.glob(os.path.join(home, ".codex", "sessions", "*", "*", "*", "*.jsonl"))
        codex_db_paths = get_codex_active_rollout_paths(home)
        codex_files = list(dict.fromkeys(codex_db_paths + codex_glob))

        return [
            ("Antigravity", agy_files, ["Antigravity.exe"], ["Antigravity"]),
            (
                "Claude Code",
                claude_files,
                ["Claude.exe", "Cursor.exe", "Code.exe", "WindowsTerminal.exe"],
                ["Claude", "Cursor", "Visual Studio Code", "Windows PowerShell", "Terminal"],
            ),
            (
                "Codex",
                codex_files,
                ["Codex.exe", "codex.exe", "Cursor.exe", "Code.exe", "WindowsTerminal.exe"],
                ["Codex", "OpenAI Codex", "Terminal", "Windows PowerShell"],
            ),
        ]

    # Initialize offsets to current end-of-file so we only trigger on NEW completed cycles
    now = time.time()
    for _app, paths, _exes, _kws in gather_sources():
        for path in paths:
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
        for app_name, paths, exe_names, win_keywords in gather_sources():
            for path in paths:
                try:
                    mtime = os.path.getmtime(path)
                    if now - mtime > 3600:
                        continue
                    size = os.path.getsize(path)
                    if path not in file_offsets:
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
                            triggered, text, explicit_opts = parse_codex_line(line)

                        if triggered and text:
                            title, summary, parsed_opts = extract_choices_and_summary(text)
                            final_opts = explicit_opts if explicit_opts else parsed_opts
                            sig = f"{app_name}|{title}|{summary}|{';'.join(final_opts)}"
                            if sig != last_dispatched_sig:
                                last_dispatched_sig = sig
                                badge = (
                                    f"{len(final_opts)} Choices · {app_name}"
                                    if final_opts
                                    else "Cycle Complete"
                                )
                                target_hwnd = find_window_by_process_or_title(
                                    exe_names, win_keywords
                                )
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
        if not raw_stdin.strip() and len(sys.argv) > 1 and sys.argv[-1].strip().startswith("{"):
            raw_stdin = sys.argv[-1]
        payload = json.loads(raw_stdin) if raw_stdin.strip() else {}
    except Exception:
        payload = {}

    # Support direct Codex notify payload (`last-assistant-message` / `last_agent_message`)
    codex_direct_msg = (
        payload.get("last-assistant-message")
        or payload.get("last_agent_message")
        or ""
    ).strip()

    if codex_direct_msg:
        app_label = "Codex"
        last_response = codex_direct_msg
        explicit_opts: list[str] = []
        target_hwnd = find_window_by_process_or_title(
            ["Codex.exe", "codex.exe", "WindowsTerminal.exe"],
            ["Codex", "OpenAI Codex", "Terminal"],
        )
    else:
        app_label, last_response, explicit_opts = read_last_response_from_candidates(payload)
        target_hwnd = find_window_by_process_or_title(
            ["Antigravity.exe"] if app_label == "Antigravity" else ["Claude.exe", "Cursor.exe", "Code.exe"],
            [app_label],
        )

    if last_response:
        title, summary, parsed_opts = extract_choices_and_summary(last_response)
        options = explicit_opts if explicit_opts else parsed_opts
    else:
        last_user = payload.get("lastUserInput") or payload.get("prompt") or ""
        title = "Finished: " + (last_user[:60] if last_user else "LLM Cycle Complete")
        summary = f"{app_label} finished its cycle and is ready for your response."
        options = []

    badge = f"{len(options)} Choices · {app_label}" if options else "Cycle Complete"
    send_to_hud(
        app=app_label,
        badge=badge,
        title=title,
        summary=summary,
        options=options,
        hwnd=target_hwnd if target_hwnd else None,
    )

    # Output valid Stop hook JSON contract so Antigravity / Claude Code stops normally
    if "transcriptPath" in payload or "conversationId" in payload:
        sys.stdout.write(json.dumps({"decision": "allow"}) + "\n")
        sys.stdout.flush()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
