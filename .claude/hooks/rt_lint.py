#!/usr/bin/env python3
"""Realtime-safety tripwire for ZYRON.

Scans C++ functions that run on the audio thread for constructs that allocate, lock, do I/O, log,
throw or wait. Which functions count as "audio thread":
  * entry points by name (processBlock, process, getNextAudioBlock, audioDeviceIOCallbackWithContext,
    audioDeviceIOCallback, renderNextBlock) in files under src/Audio/ or src/Recording/;
  * any function whose definition is directly preceded by a line comment that is exactly `// RT`
    (anywhere under src/).

This is a cheap regex-level tripwire, not a proof: it cannot follow calls into helpers (mark those
`// RT`) and it flags some safe code as "warn". rt-safety-reviewer does the real review.

Modes:
  hook (no args)          read a Claude Code PostToolUse JSON payload on stdin;
                          findings -> stderr, exit code 2 (fed back to Claude); never fails hard
  --file PATH [PATH ...]  scan files directly (entry-point names always enabled); report on stdout

Exit codes: 0 clean or not applicable, 2 findings.
Output is plain ASCII on purpose (Windows consoles may use a legacy code page).
"""
from __future__ import annotations

import argparse
import bisect
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path

ENTRY_NAMES = (
    "processBlock",
    "process",
    "getNextAudioBlock",
    "audioDeviceIOCallbackWithContext",
    "audioDeviceIOCallback",
    "renderNextBlock",
)
CPP_SUFFIXES = frozenset({".cpp", ".cc", ".cxx", ".h", ".hpp", ".hh", ".inl", ".ipp"})
SOURCE_DIR = "/src/"
ENTRY_SCOPED_DIRS = ("/src/Audio/", "/src/Recording/")
MAX_SNIPPET = 100

RT_MARKER = re.compile(r"^[ \t]*//[ \t]*RT[ \t]*$", re.MULTILINE)
ENTRY_CALL = re.compile(
    r"(?<![\w.>])(?:[\w:]+::)?(" + "|".join(ENTRY_NAMES) + r")\s*\("
)
QUALIFIERS = re.compile(
    r"\s*(?:(?:const|noexcept|override|final|volatile)\b(?:\s*\([^)]*\))?\s*|&&?\s*)*"
    r"(?:->\s*[^{;]+?)?\s*"
)


@dataclass(frozen=True)
class Rule:
    rule_id: str
    severity: str  # "error": must not appear on the audio thread; "warn": verify
    pattern: re.Pattern[str]
    message: str


def _rule(rule_id: str, severity: str, regex: str, message: str, flags: int = 0) -> Rule:
    return Rule(rule_id, severity, re.compile(regex, flags), message)


RULES = (
    _rule("alloc", "error",
          r"\bnew\s+(?!\()[A-Za-z_:]|\bdelete\b|\b(?:malloc|calloc|realloc|free)\s*\(",
          "heap allocation or free"),
    _rule("make-ptr", "error", r"\bstd::make_(?:unique|shared)\b", "make_unique/make_shared allocates"),
    _rule("lock", "error",
          r"\bstd::(?:recursive_|timed_|shared_)?mutex\b"
          r"|\bstd::(?:lock_guard|unique_lock|scoped_lock|shared_lock|condition_variable(?:_any)?)\b"
          r"|\b(?:juce::)?(?:CriticalSection|ScopedLock|SpinLock)\b",
          "blocking synchronisation (mutex/lock/condition variable)"),
    _rule("thread", "error", r"\bstd::(?:thread|jthread|async)\b", "creates threads/tasks"),
    _rule("io", "error",
          r"\b(?:fopen|fclose|fread|fwrite|fprintf|printf|puts)\s*\("
          r"|\bstd::(?:cout|cerr|clog|ofstream|ifstream|fstream)\b"
          r"|\b(?:juce::)?(?:File|FileInputStream|FileOutputStream|Logger)\b"
          r"|\bDBG\s*\(|\bsqlite3_\w+",
          "file I/O, logging or database access"),
    _rule("wait", "error",
          r"\bstd::this_thread::sleep_\w+|\b(?:juce::)?Thread::(?:sleep|wait)\b"
          r"|(?:\.|->)\s*wait(?:_for|_until)?\s*\(",
          "sleeps or waits"),
    _rule("throw", "error", r"\bthrow\b", "throws (RT code must not throw)"),
    _rule("msgthread", "error", r"\b(?:juce::)?MessageManager\b|\bcallAsync\b",
          "talks to the message thread"),
    _rule("string", "warn",
          r"\bstd::(?:string|wstring|to_string)\b|\bjuce::(?:String|StringArray|var)\b",
          "string/var use allocates"),
    _rule("std-function", "warn", r"\bstd::function\b", "constructing std::function may allocate"),
    _rule("growth", "warn",
          r"(?:\.|->)\s*(?:push_back|emplace_back|resize|reserve|insert|emplace|assign|append)\s*\(",
          "may reallocate; confirm capacity was reserved in prepare()"),
    _rule("container", "warn",
          r"\bstd::(?:vector|map|set|unordered_map|unordered_set|list|deque)\s*<[^;{}]*>\s+\w+\s*[;({=]",
          "container constructed inside RT function (allocates)"),
    _rule("static-local", "warn",
          r"^[ \t]*static\s+(?!constexpr\b|inline\b|const\b|thread_local\b)[\w:<>,*& ]+?\s+\w+\s*(?:=|\{|;|\()",
          "function-local static: guarded init may lock; initialise in prepare()", re.MULTILINE),
    _rule("shared-ptr", "warn", r"\bstd::shared_ptr\b",
          "copy/last release can free memory on the audio thread; use deferred release"),
)


@dataclass(frozen=True)
class Finding:
    path: str
    line: int
    severity: str
    rule_id: str
    message: str
    function: str
    snippet: str


def strip_comments_and_strings(text: str) -> str:
    """Blank out comments and string/char literals, preserving length and newlines."""
    out: list[str] = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if c == "/" and nxt == "/":
            j = text.find("\n", i)
            j = n if j == -1 else j
            out.append(" " * (j - i))
            i = j
        elif c == "/" and nxt == "*":
            j = text.find("*/", i + 2)
            j = n if j == -1 else j + 2
            out.append(re.sub(r"[^\n]", " ", text[i:j]))
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] not in (c, "\n"):
                j += 2 if text[j] == "\\" else 1
            j = min(j + 1, n)
            out.append(re.sub(r"[^\n]", " ", text[i:j]))
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def _match_close(s: str, open_idx: int, open_ch: str, close_ch: str) -> int:
    """Index of the bracket closing s[open_idx]; len(s) - 1 if unbalanced."""
    depth = 0
    for k in range(open_idx, len(s)):
        ch = s[k]
        if ch == open_ch:
            depth += 1
        elif ch == close_ch:
            depth -= 1
            if depth == 0:
                return k
    return len(s) - 1


def _entry_bodies(stripped: str) -> list[tuple[str, int]]:
    """(function name, index of body-opening brace) for entry-point definitions."""
    found: list[tuple[str, int]] = []
    for m in ENTRY_CALL.finditer(stripped):
        close = _match_close(stripped, m.end() - 1, "(", ")")
        tail = stripped[close + 1:]
        q = QUALIFIERS.match(tail)
        idx = close + 1 + (q.end() if q else 0)
        if idx < len(stripped) and stripped[idx] == "{":
            found.append((m.group(1), idx))
    return found


def _marked_bodies(text: str, stripped: str) -> list[tuple[str, int]]:
    """(function name, body brace index) for functions preceded by a `// RT` line."""
    found: list[tuple[str, int]] = []
    for m in RT_MARKER.finditer(text):
        brace = stripped.find("{", m.end())
        if brace == -1:
            continue
        head = stripped[m.end():brace]
        if ";" in head or "(" not in head:
            continue  # declaration or not a function
        names = re.findall(r"[~\w:]+(?=\s*\()", head)
        found.append((names[0] if names else "<rt-function>", brace))
    return found


def scan_source(text: str, path: str = "<memory>", entry_names: bool = True) -> list[Finding]:
    stripped = strip_comments_and_strings(text)
    bodies = dict.fromkeys(_marked_bodies(text, stripped))
    if entry_names:
        bodies.update(dict.fromkeys(_entry_bodies(stripped)))
    newline_offsets = [i for i, ch in enumerate(text) if ch == "\n"]
    lines = text.split("\n")
    findings: dict[tuple[int, str], Finding] = {}
    for name, brace in bodies:
        end = _match_close(stripped, brace, "{", "}")
        body = stripped[brace + 1:end]
        for rule in RULES:
            for m in rule.pattern.finditer(body):
                line = bisect.bisect_left(newline_offsets, brace + 1 + m.start()) + 1
                snippet = lines[line - 1].strip()[:MAX_SNIPPET]
                findings.setdefault(
                    (line, rule.rule_id),
                    Finding(path, line, rule.severity, rule.rule_id, rule.message, name, snippet),
                )
    return sorted(findings.values(), key=lambda f: (f.line, f.rule_id))


def classify_path(path: str) -> tuple[bool, bool]:
    """(should scan, entry-point names enabled) for a path seen in a hook payload."""
    p = path.replace("\\", "/")
    if Path(p).suffix.lower() not in CPP_SUFFIXES or SOURCE_DIR not in p:
        return False, False
    return True, any(d in p for d in ENTRY_SCOPED_DIRS)


def format_report(path: str, findings: list[Finding]) -> str:
    errors = sum(f.severity == "error" for f in findings)
    out = [f"RT-safety tripwire: {len(findings)} finding(s) in {path} ({errors} error, "
           f"{len(findings) - errors} warn)"]
    out += [f"  {path}:{f.line}: [{f.severity}] {f.rule_id}: {f.message} (in {f.function}) -> {f.snippet}"
            for f in findings]
    out.append("These functions run on the audio thread. Allocate/lock/log in prepare() or on a worker "
               "thread and hand data over via the lock-free queues. Regex heuristics only: confirm "
               "with rt-safety-reviewer; fix errors, fix or justify warns.")
    return "\n".join(out)


def _read(path: str) -> str | None:
    try:
        return Path(path).read_text(encoding="utf-8", errors="replace")
    except OSError:
        return None


def run_hook(payload: dict) -> tuple[int, str]:
    tool_input = payload.get("tool_input") or {}
    path = tool_input.get("file_path") or tool_input.get("path")
    if not isinstance(path, str):
        return 0, ""
    should_scan, entry_names = classify_path(path)
    text = _read(path) if should_scan else None
    if text is None:
        return 0, ""
    findings = scan_source(text, path, entry_names)
    return (2, format_report(path, findings)) if findings else (0, "")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--file", nargs="+", metavar="PATH", help="scan files directly")
    args = parser.parse_args(argv)
    if args.file:
        code = 0
        for path in args.file:
            text = _read(path)
            if text is None:
                print(f"{path}: cannot read", file=sys.stderr)
                continue
            findings = scan_source(text, path, entry_names=True)
            if findings:
                print(format_report(path, findings))
                code = 2
        return code
    try:
        code, message = run_hook(json.load(sys.stdin))
    except Exception as exc:  # a tripwire must never break the editing session
        print(f"rt_lint internal error (ignored): {exc}", file=sys.stderr)
        return 0
    if message:
        print(message, file=sys.stderr)
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
