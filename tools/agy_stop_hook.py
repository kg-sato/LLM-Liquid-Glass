#!/usr/bin/env python3
"""
Antigravity / Claude Code / Codex automatic Stop-hook bridge for Windhawk LLM Liquid Glass HUD.

When registered in hooks.json under the "Stop" lifecycle event, this script runs automatically
every time the LLM finishes a turn, inspects the last assistant response from the transcript
to extract any multiple-choice options or summary text, and pops up the Apple Liquid Glass HUD.
"""

import json
import os
import re
import sys

# Ensure we can import llm_hud_send from the same directory
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from llm_hud_send import send_to_hud


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
        elif not line.startswith("```") and not line.startswith("|") and not line.startswith("<"):
            summary_lines.append(clean)

    title = summary_lines[0][:90] if summary_lines else "Agent finished turn"
    summary = " ".join(summary_lines[1:4]) if len(summary_lines) > 1 else title
    return (title, summary, options)


def read_last_model_response(transcript_path: str) -> str:
    if not transcript_path or not os.path.isfile(transcript_path):
        return ""
    try:
        last_content = ""
        with open(transcript_path, "r", encoding="utf-8", errors="ignore") as f:
            for line in f:
                if '"PLANNER_RESPONSE"' in line or '"assistant"' in line:
                    try:
                        obj = json.loads(line)
                        content = obj.get("content") or ""
                        if isinstance(content, str) and content.strip():
                            last_content = content.strip()
                    except Exception:
                        pass
        return last_content
    except Exception:
        return ""


def main() -> int:
    try:
        raw_stdin = sys.stdin.read()
        payload = json.loads(raw_stdin) if raw_stdin.strip() else {}
    except Exception:
        payload = {}

    transcript_path = payload.get("transcriptPath", "")
    last_response = read_last_model_response(transcript_path)

    if last_response:
        title, summary, options = extract_choices_and_summary(last_response)
    else:
        last_user = payload.get("lastUserInput", "")
        title = "Finished: " + (last_user[:60] if last_user else "LLM Cycle Complete")
        summary = "Antigravity finished its cycle and is ready for your response."
        options = []

    badge = f"{len(options)} Choices" if options else "Cycle Complete"
    send_to_hud(
        app="Antigravity",
        badge=badge,
        title=title,
        summary=summary,
        options=options,
    )

    # Output valid Stop hook JSON contract so Antigravity stops normally
    sys.stdout.write(json.dumps({"decision": "allow"}) + "\n")
    sys.stdout.flush()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
