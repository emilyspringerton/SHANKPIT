#!/usr/bin/env python3
"""Headless sanity lint for Windows .bat files (runs on Linux, no cmd.exe).

Catches the classic batch pitfalls we can detect statically. It is NOT a
batch interpreter: passing means "no known pitfall found", not "works".

Usage: python3 lint_bat.py [file.bat ...]   (default: setup.bat next to this file)
Exit code 0 = no errors (warnings allowed), 1 = errors found.
"""
import os
import re
import sys


def strip_quoted(s):
    return re.sub(r'"[^"]*"', '""', s)


def lint(path):
    errors, warns = [], []
    raw = open(path, "rb").read()

    # --- encoding / line endings
    if b"\r\n" not in raw or re.search(rb"(?<!\r)\n", raw):
        errors.append((0, "file must use CRLF line endings on every line (LF-only breaks goto/call labels)"))
    try:
        raw.decode("ascii")
    except UnicodeDecodeError as e:
        errors.append((0, "non-ASCII byte at offset %d (cmd uses the OEM codepage; keep ASCII)" % e.start))
    if raw.startswith(b"\xef\xbb\xbf"):
        errors.append((1, "UTF-8 BOM breaks the first line of a batch file"))

    text = raw.decode("ascii", "replace").replace("\r\n", "\n")
    lines = text.split("\n")

    labels, label_line = {}, {}
    gotos = []  # (lineno, label)
    depth = 0
    delayed_on = bool(re.search(r"EnableDelayedExpansion", text, re.I))

    for n, line in enumerate(lines, 1):
        s = line.strip()
        low = s.lower()
        if not s:
            continue
        if low.startswith("rem ") or low == "rem" or s.startswith("::"):
            continue
        if low.startswith("@echo off"):
            continue

        m = re.match(r"^:([A-Za-z0-9_.\-]+)", s)
        if m and not s.startswith("::"):
            name = m.group(1).lower()
            if name in labels:
                errors.append((n, "duplicate label :%s (first at line %d)" % (name, label_line[name])))
            labels[name] = True
            label_line[name] = n
            continue

        for gm in re.finditer(r"\bgoto\s+:?([A-Za-z0-9_.\-]+)", s, re.I):
            gotos.append((n, gm.group(1).lower()))
        for cm in re.finditer(r"\bcall\s+:([A-Za-z0-9_.\-]+)", s, re.I):
            gotos.append((n, cm.group(1).lower()))

        # for-loop variable must be %%X in a batch file
        if re.match(r"^for\b", low) and re.search(r"(?<!%)%[A-Za-z]\s+in\b", s):
            errors.append((n, "for loop variable uses single % (needs %%X inside a .bat)"))

        # delayed expansion syntax without it enabled
        if not delayed_on and re.search(r"![A-Za-z_][A-Za-z0-9_]*!", s):
            warns.append((n, "!VAR! used but delayed expansion is not enabled"))

        # hardcoded secrets
        if "_FILE=" not in s and re.search(r"set\s+\"?[A-Za-z_]*(PASS|PASSWORD|SECRET|TOKEN)(?!_?FILE)[A-Za-z_]*=[^%\"\s]", s, re.I) \
                and not re.search(r"=\s*\"?$", s):
            errors.append((n, "possible hardcoded secret in a set statement"))
        if re.search(r"VALUE_OF_[A-Z]*PASSWORD=[^\"%\s]", s):
            errors.append((n, "literal password passed to msiexec; use a %variable%"))

        # :log does `echo ... %~1` so the message is re-parsed unquoted: < > | & become operators
        lm = re.match(r'^(?:if\b.*?\s)?call\s+:(?:log|fail)\s+"(.*)"\s*$', s, re.I)
        if lm and re.search(r"[<>|&]", re.sub(r"%%?[A-Za-z0-9_~:,]+%?", "", lm.group(1))):
            errors.append((n, "call :log/:fail message contains < > | & which cmd re-parses as an operator (e.g. '->' redirects into a file)"))

        # unquoted set with trailing whitespace, or & | in value
        sm = re.match(r"^set\s+([A-Za-z_][A-Za-z0-9_]*)=(.*)$", s, re.I)
        if sm and not re.match(r"^set\s+/", s, re.I) and not s.lower().startswith('set "'):
            warns.append((n, "unquoted set; use set \"VAR=value\" to avoid trailing space and &|<> problems"))

        if re.match(r"^timeout\b", low) or re.search(r"[&|(]\s*timeout\b", low):
            warns.append((n, "timeout fails when stdin is redirected; use ping -n N 127.0.0.1 >nul"))
        if re.match(r"^exit(\s+\d+)?\s*$", low):
            warns.append((n, "exit without /b closes the whole console window"))
        if re.search(r"\bif\s+%[A-Za-z_][A-Za-z0-9_]*%\s*==", s, re.I):
            warns.append((n, "if %VAR%== without quotes breaks when VAR is empty"))

        # parenthesis blocks
        q = strip_quoted(s)
        is_echo = low.startswith("echo") or re.match(r"^(>>?\S+\s+)?echo\b", low) is not None
        opens = q.count("(") - len(re.findall(r"\^\(", q))
        closes = q.count(")") - len(re.findall(r"\^\)", q))
        if depth > 0 and "%errorlevel%" in low:
            errors.append((n, "%errorlevel% inside a parenthesised block expands at parse time; use if errorlevel or goto"))
        if depth > 0 and is_echo and closes and "^)" not in s:
            errors.append((n, "echo containing ) inside a block closes the block early; escape as ^)"))
        if re.search(r"\bset\s+/a\b", low) is None and depth > 0 and re.search(r"%[A-Za-z_]+%", s) and False:
            pass
        depth += opens - closes
        if depth < 0:
            errors.append((n, "unbalanced ) (depth went negative)"))
            depth = 0

    if depth != 0:
        errors.append((len(lines), "unbalanced parentheses at end of file (depth %d)" % depth))

    for n, tgt in gotos:
        if tgt != "eof" and tgt not in labels:
            errors.append((n, "goto/call target :%s does not exist" % tgt))

    used = {t for _, t in gotos}
    for name, ln in label_line.items():
        if name not in used:
            warns.append((ln, "label :%s is never referenced" % name))

    if not re.search(r"exit\s+/b|goto\s+:?eof", text, re.I):
        warns.append((0, "no exit /b or goto :eof found; subroutines may fall through"))

    # first non-label code line after the main body must not fall into subroutines
    return errors, warns


def main(argv):
    here = os.path.dirname(os.path.abspath(__file__))
    paths = argv[1:] or [os.path.join(here, "setup.bat")]
    bad = 0
    for p in paths:
        errors, warns = lint(p)
        for n, msg in sorted(warns):
            print("%s:%d: warning: %s" % (p, n, msg))
        for n, msg in sorted(errors):
            print("%s:%d: error: %s" % (p, n, msg))
        print("%s: %d error(s), %d warning(s)" % (p, len(errors), len(warns)))
        bad += len(errors)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
