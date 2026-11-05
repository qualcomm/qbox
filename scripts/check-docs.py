#!/usr/bin/env python3
"""Check supported local Markdown links without changing or running anything."""

from __future__ import annotations

import argparse
import html
import os
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from urllib.parse import unquote, urlsplit


SKIP_DIRS = {
    ".git",
    "_site",
    "vendor",
    "node_modules",
    "deps",
    ".venv",
    "venv",
}
DISCOVERY_TREES = ("docs", "platforms", "systemc-components", "tests")
ROOT_DOCS = ("README.md", "CONTRIBUTING.md")


@dataclass(frozen=True)
class Link:
    line: int
    destination: str


@dataclass(frozen=True)
class Note:
    line: int
    message: str
    external: bool = False


@dataclass(frozen=True)
class Problem:
    line: int
    message: str


def _inside(path: Path, root: Path) -> bool:
    try:
        path.relative_to(root)
        return True
    except ValueError:
        return False


def _is_generated_dir(name: str) -> bool:
    return name == "build" or name.startswith(("build-", "build_"))


def _resolve_root(raw_root: str | None) -> Path:
    cwd = Path.cwd().resolve()
    candidate = cwd if raw_root is None else Path(raw_root)
    if not candidate.is_absolute():
        candidate = cwd / candidate
    try:
        root = candidate.resolve(strict=True)
    except (OSError, RuntimeError) as exc:
        raise ValueError(f"cannot resolve documentation root: {exc}") from exc
    if not _inside(root, cwd):
        raise ValueError("documentation root must resolve within the current directory")
    # Check repository markers before opening any documentation files. This
    # prevents an accidental run from an unrelated working directory.
    try:
        root_entries = {entry.name for entry in os.scandir(root)}
    except OSError as exc:
        raise ValueError(f"cannot inspect documentation root: {exc}") from exc
    if "README.md" not in root_entries or "CMakeLists.txt" not in root_entries:
        raise ValueError("root must contain README.md and CMakeLists.txt")
    # README.md is also a scanned source. Leave its symlink safety decision to
    # discovery so an outside-root README symlink is skipped, not followed.
    for marker in ("CMakeLists.txt",):
        marker_path = root / marker
        try:
            resolved_marker = marker_path.resolve(strict=True)
        except (OSError, RuntimeError) as exc:
            raise ValueError(f"root marker cannot be resolved: {marker}") from exc
        if not _inside(resolved_marker, root) or not resolved_marker.is_file():
            raise ValueError(f"root marker must be a file inside the root: {marker}")
    return root


def _check_source_path(path: Path, root: Path) -> tuple[bool, str | None]:
    """Return whether a discovered source is safe to read under root."""
    try:
        resolved = path.resolve(strict=True)
    except (OSError, RuntimeError):
        return False, "source symlink is broken or cannot be resolved"
    if not _inside(resolved, root):
        return False, "source symlink resolves outside the check root"
    if not resolved.is_file():
        return False, "source is not a regular file"
    return True, None


def discover_markdown(root: Path) -> tuple[list[Path], list[tuple[Path, str]]]:
    """Find the documented scope in the working tree, without using Git."""
    found: set[Path] = set()
    skipped: list[tuple[Path, str]] = []
    with os.scandir(root) as entries:
        root_names = {entry.name for entry in entries}

    def add(path: Path) -> None:
        safe, reason = _check_source_path(path, root)
        if safe:
            found.add(path)
        elif path.is_symlink():
            skipped.append((path, reason or "unsafe source"))

    for name in ROOT_DOCS:
        path = root / name
        if name in root_names:
            add(path)

    for tree_name in DISCOVERY_TREES:
        start = root / tree_name
        if tree_name not in root_names:
            continue
        if start.is_symlink():
            try:
                if not _inside(start.resolve(strict=False), root):
                    skipped.append((start, "directory symlink resolves outside the check root"))
            except (OSError, RuntimeError):
                skipped.append((start, "directory symlink cannot be resolved"))
            continue
        if not start.is_dir():
            continue

        def on_walk_error(exc: OSError) -> None:
            raise exc

        for current, dirnames, filenames in os.walk(start, topdown=True, followlinks=False, onerror=on_walk_error):
            current_path = Path(current)
            kept_dirs: list[str] = []
            for dirname in sorted(dirnames):
                child = current_path / dirname
                if dirname in SKIP_DIRS or _is_generated_dir(dirname):
                    continue
                if child.is_symlink():
                    # Do not recurse through directory symlinks, even when
                    # they point back into the root.
                    try:
                        if not _inside(child.resolve(strict=False), root):
                            skipped.append((child, "directory symlink resolves outside the check root"))
                    except (OSError, RuntimeError):
                        skipped.append((child, "directory symlink cannot be resolved"))
                    continue
                kept_dirs.append(dirname)
            dirnames[:] = kept_dirs

            for filename in sorted(filenames):
                path = current_path / filename
                if tree_name == "docs":
                    match = path.suffix.lower() == ".md"
                else:
                    match = filename == "README.md"
                if match:
                    add(path)

    return sorted(found), skipped


def _visible_lines(text: str) -> list[tuple[int, str] | None]:
    """Mask fenced blocks and HTML comments; retain physical line numbers."""
    result: list[tuple[int, str] | None] = []
    fence_char: str | None = None
    fence_size = 0
    in_comment = False

    for line_number, line in enumerate(text.splitlines(), 1):
        if fence_char is not None:
            closer = re.match(r"^ {0,3}(`+|~+)\s*$", line)
            if closer and closer.group(1)[0] == fence_char and len(closer.group(1)) >= fence_size:
                fence_char = None
                fence_size = 0
            result.append(None)
            continue

        opener = re.match(r"^ {0,3}(`{3,}|~{3,})(.*)$", line)
        if opener:
            fence_char = opener.group(1)[0]
            fence_size = len(opener.group(1))
            result.append(None)
            continue

        visible = line
        if in_comment:
            end = visible.find("-->")
            if end < 0:
                result.append((line_number, ""))
                continue
            visible = visible[end + 3 :]
            in_comment = False
        while "<!--" in visible:
            begin = visible.find("<!--")
            end = visible.find("-->", begin + 4)
            if end < 0:
                visible = visible[:begin]
                in_comment = True
                break
            visible = visible[:begin] + " " * (end + 3 - begin) + visible[end + 3 :]
        result.append((line_number, visible))
    return result


def _reference_label(label: str) -> str:
    return " ".join(label.split()).casefold()


def _exact_path(
    lexical: Path, root: Path, display_path: str
) -> tuple[Path | None, str | None, str | None]:
    """Resolve an existing path with exact-case checks, bounded to root.

    Return (resolved path, broken-link message, skip message). Each directory
    is checked before it is scanned, and each symlink is resolved and bounded
    before the next component is inspected.
    """
    try:
        components = lexical.relative_to(root).parts
    except ValueError:
        return None, "local destination escapes the check root", None
    current = root
    for component in components:
        try:
            resolved_parent = current.resolve(strict=True)
        except FileNotFoundError:
            return None, f"target does not exist: {display_path}", None
        except (OSError, RuntimeError) as exc:
            if isinstance(exc, RuntimeError):
                return None, None, "destination symlink cannot be resolved"
            raise
        if not _inside(resolved_parent, root):
            return None, None, "destination symlink resolves outside the check root"
        try:
            with os.scandir(resolved_parent) as entries:
                names = [entry.name for entry in entries]
        except FileNotFoundError:
            return None, f"target does not exist: {display_path}", None
        except NotADirectoryError:
            return None, f"target path component is not a directory: {current}", None
        if component not in names:
            if any(name.casefold() == component.casefold() for name in names):
                return None, f"path component has incorrect case: {component}", None
            return None, f"target does not exist: {display_path}", None
        current = resolved_parent / component
        try:
            resolved_probe = current.resolve(strict=False)
        except (OSError, RuntimeError):
            return None, None, "destination symlink cannot be resolved"
        if not _inside(resolved_probe, root):
            return None, None, "destination symlink resolves outside the check root"
        try:
            resolved_current = current.resolve(strict=True)
        except FileNotFoundError:
            return None, f"target does not exist: {display_path}", None
        except (OSError, RuntimeError) as exc:
            if isinstance(exc, RuntimeError):
                return None, None, "destination symlink cannot be resolved"
            raise
        if not _inside(resolved_current, root):
            return None, None, "destination symlink resolves outside the check root"
        current = resolved_current
    return current, None, None


def _parse_definition(line: str) -> tuple[str, str] | None:
    match = re.match(r"^ {0,3}\[([^\]]+)\]:\s*(.*)$", line)
    if not match:
        return None
    rest = match.group(2).strip()
    if not rest:
        return None
    if rest.startswith("<"):
        close = rest.find(">")
        if close < 0:
            return None
        destination = rest[1:close]
    else:
        destination = re.match(r"\S+", rest).group(0)  # type: ignore[union-attr]
        # Bare GFM destinations may contain balanced parentheses. This small
        # definition parser accepts the common no-whitespace form only.
        if destination.count("(") != destination.count(")"):
            return None
    return _reference_label(match.group(1)), destination


def _mask_inline(
    line: str, line_number: int, *, mask_html: bool = True
) -> tuple[str, list[Note]]:
    """Hide inline code and HTML tags before scanning Markdown links."""
    chars = list(line)
    notes: list[Note] = []
    i = 0
    while i < len(line):
        if line[i] == "\\":
            i += 2
            continue
        if line[i] == "`":
            run_end = i
            while run_end < len(line) and line[run_end] == "`":
                run_end += 1
            run = line[i:run_end]
            close = line.find(run, run_end)
            if close >= 0:
                end = close + len(run)
                for pos in range(i, end):
                    if chars[pos] != "\t":
                        chars[pos] = " "
                i = end
            else:
                for pos in range(i, len(line)):
                    chars[pos] = " "
                break
            continue

        if mask_html and line[i] == "<":
            auto = re.match(r"<([^<>\s]+)>", line[i:])
            if auto and (":" in auto.group(1) or "@" in auto.group(1)):
                notes.append(Note(line_number, "GFM external autolink was not checked", external=True))
                for pos in range(i, i + len(auto.group(0))):
                    chars[pos] = " "
                i += len(auto.group(0))
                continue
            tag = re.match(r"</?[A-Za-z][^>]*>", line[i:])
            if tag:
                raw = tag.group(0)
                if re.match(r"<\s*a(?:\s|>)", raw, re.IGNORECASE) and re.search(
                    r"\b(?:href|name|id)\s*=", raw, re.IGNORECASE
                ):
                    notes.append(Note(line_number, "raw HTML anchor attributes are not checked"))
                elif re.search(r"\b(?:href|name|id)\s*=", raw, re.IGNORECASE):
                    notes.append(Note(line_number, "raw HTML link or anchor attributes are not checked"))
                for pos in range(i, i + len(raw)):
                    chars[pos] = " "
                i += len(raw)
                continue
        i += 1
    return "".join(chars), notes


def _closing_bracket(text: str, start: int, opening: str, closing: str) -> int | None:
    depth = 0
    i = start
    while i < len(text):
        if text[i] == "\\":
            i += 2
            continue
        if text[i] == opening:
            depth += 1
        elif text[i] == closing:
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return None


def _parse_inline_destination(text: str, opening: int) -> tuple[str, int] | None:
    """Parse a same-line GFM destination and return it with the close offset."""
    i = opening + 1
    while i < len(text) and text[i].isspace():
        i += 1
    if i >= len(text):
        return None
    if text[i] == ")":
        return "", i
    if text[i] == "<":
        end = i + 1
        while end < len(text):
            if text[end] == "\\":
                end += 2
                continue
            if text[end] == ">":
                break
            end += 1
        if end >= len(text):
            return None
        destination = text[i + 1 : end]
        i = end + 1
    else:
        begin = i
        depth = 0
        while i < len(text):
            char = text[i]
            if char == "\\":
                i += 2
                continue
            if char == "(" and depth < 32:
                depth += 1
            elif char == ")":
                if depth == 0:
                    break
                depth -= 1
            elif char.isspace() and depth == 0:
                break
            i += 1
        if depth:
            return None
        destination = text[begin:i]

    while i < len(text) and text[i].isspace():
        i += 1
    if i < len(text) and text[i] == ")":
        return destination, i
    # A quoted title is accepted. Other trailing syntax is ambiguous here.
    if i < len(text) and text[i] in "\"'":
        quote = text[i]
        i += 1
        while i < len(text):
            if text[i] == "\\":
                i += 2
                continue
            if text[i] == quote:
                i += 1
                break
            i += 1
        while i < len(text) and text[i].isspace():
            i += 1
        if i < len(text) and text[i] == ")":
            return destination, i
    return None


def extract_links(text: str) -> tuple[list[Link], list[Note], list[Problem]]:
    """Extract supported inline/reference links and report uncertain syntax."""
    visible = _visible_lines(text)
    definitions: dict[str, tuple[str, int]] = {}
    definition_destinations: dict[int, str] = {}
    definition_lines: set[int] = set()
    notes: list[Note] = []
    problems: list[Problem] = []

    for item in visible:
        if item is None:
            continue
        line_number, line = item
        # Keep angle-bracket destinations such as [label]: <path with space.md>.
        masked, _ = _mask_inline(line, line_number, mask_html=False)
        match = re.match(r"^ {0,3}\[[^\]]+\]:", masked)
        if not match:
            continue
        definition_lines.add(line_number)
        parsed = _parse_definition(masked)
        if parsed is None:
            notes.append(Note(line_number, "reference definition syntax is not supported"))
            continue
        label, destination = parsed
        definitions.setdefault(label, (destination, line_number))
        definition_destinations[line_number] = destination

    links: list[Link] = []
    # Add each valid definition once, even when no reference uses it.
    for item in visible:
        if item is None:
            continue
        line_number, line = item
        if line_number in definition_lines:
            destination = definition_destinations.get(line_number)
            if destination is not None:
                links.append(Link(line_number, destination))
            continue
        masked, line_notes = _mask_inline(line, line_number)
        notes.extend(line_notes)
        bare_scan = list(masked)
        i = 0
        while i < len(masked):
            image = masked[i] == "!" and i + 1 < len(masked) and masked[i + 1] == "["
            if masked[i] != "[" and not image:
                i += 1
                continue
            if i > 0 and masked[i - 1] == "\\":
                i += 1
                continue
            opening = i + 1 if image else i
            close = _closing_bracket(masked, opening, "[", "]")
            if close is None:
                # An unmatched opener can be a link split across lines. Do not
                # claim it was verified; ordinary bracketed prose may also
                # receive this conservative skip notice.
                notes.append(Note(line_number, "unmatched or multiline bracket syntax was not checked"))
                i += 2 if image else 1
                continue
            label_text = masked[opening + 1 : close]
            after = close + 1
            if after < len(masked) and masked[after] == "(":
                parsed_link = _parse_inline_destination(masked, after)
                if parsed_link is None:
                    notes.append(Note(line_number, "complex or multiline inline-link syntax is not checked"))
                    possible_end = _closing_bracket(masked, after, "(", ")")
                    stop = possible_end + 1 if possible_end is not None else after + 1
                    for pos in range(i, stop):
                        bare_scan[pos] = " "
                    i = after + 1
                    continue
                destination, end = parsed_link
                links.append(Link(line_number, destination))
                for pos in range(i, end + 1):
                    bare_scan[pos] = " "
                i = end + 1
                continue
            if after < len(masked) and masked[after] == "[":
                ref_end = _closing_bracket(masked, after, "[", "]")
                if ref_end is None:
                    notes.append(Note(line_number, "complex reference-link syntax is not checked"))
                    i = after + 1
                    continue
                ref = masked[after + 1 : ref_end] or label_text
                definition = definitions.get(_reference_label(ref))
                if definition is None:
                    problems.append(Problem(line_number, f"unresolved reference label [{ref}]"))
                else:
                    for pos in range(i, ref_end + 1):
                        bare_scan[pos] = " "
                i = ref_end + 1
                continue
            definition = definitions.get(_reference_label(label_text))
            if definition is not None:
                # The definition itself is checked at its source line.
                for pos in range(i, after):
                    bare_scan[pos] = " "
                i = after
                continue
            # Ordinary bracketed text is not a link.
            i = close + 1

        bare_url = re.compile(
            r"(?i)(?<![\w@])(?:https?://|ftp://|www\.)[^\s<>\[\]{}]+|"
            r"(?<![\w.+-])[A-Z0-9._%+-]+@[A-Z0-9.-]+\.[A-Z]{2,}"
        )
        for match in bare_url.finditer("".join(bare_scan)):
            notes.append(Note(line_number, "bare GFM external URL was not checked", external=True))

    return links, notes, problems


def _heading_text(markdown: str) -> list[str]:
    headings: list[str] = []
    previous: str | None = None
    for item in _visible_lines(markdown):
        if item is None:
            previous = None
            continue
        _, line = item
        atx = re.match(r"^ {0,3}#{1,6}(?:\s+|$)(.*?)\s*#*\s*$", line)
        if atx:
            headings.append(atx.group(1))
            previous = None
            continue
        if previous is not None and line.strip() and re.match(r"^ {0,3}(?:=+|-+)\s*$", line):
            headings.append(previous.strip())
        previous = line if line.strip() else None
    return headings


def _slug_base(heading: str) -> str:
    # Keep inline-code contents while removing the delimiters. GitHub includes
    # those contents in heading anchors.
    heading = re.sub(r"(`+)(.*?)\1", r"\2", heading)
    heading = re.sub(r"!?\[([^\]]*)\]\([^)]*\)", r"\1", heading)
    heading = re.sub(r"\[([^\]]*)\](?:\[[^\]]*\])?", r"\1", heading)
    heading = re.sub(r"<[^>]*>", " ", heading)
    heading = html.unescape(heading).casefold()
    chars: list[str] = []
    for char in heading:
        if char.isspace():
            chars.append("-")
        elif char.isalnum() or char in "_-":
            chars.append(char)
    return re.sub(r"-+", "-", "".join(chars)).strip("-")


def _html_anchor_ids(markdown: str) -> set[str]:
    """Collect raw HTML IDs only to report matching fragments as unverified."""
    found: set[str] = set()
    attribute = re.compile(r"\b(?:id|name)\s*=\s*(['\"])(.*?)\1", re.IGNORECASE)
    for item in _visible_lines(markdown):
        if item is None:
            continue
        _, line = item
        i = 0
        while i < len(line):
            if line[i] == "\\":
                i += 2
                continue
            if line[i] == "`":
                end_run = i
                while end_run < len(line) and line[end_run] == "`":
                    end_run += 1
                run = line[i:end_run]
                close = line.find(run, end_run)
                i = close + len(run) if close >= 0 else len(line)
                continue
            if line[i] == "<":
                tag = re.match(r"</?[A-Za-z][^>]*>", line[i:])
                if tag:
                    for match in attribute.finditer(tag.group(0)):
                        found.add(html.unescape(match.group(2)))
                    i += len(tag.group(0))
                    continue
            i += 1
    return found


def heading_slugs(markdown: str) -> tuple[set[str], set[str]]:
    """Return GitHub-style heading slugs and raw HTML ids/names (unverified)."""
    slugs: set[str] = set()
    explicit_ids: set[str] = set()
    used: set[str] = set()
    next_suffix: dict[str, int] = {}
    for heading in _heading_text(markdown):
        base = _slug_base(heading)
        if not base:
            continue
        slug = base
        if slug in used:
            suffix = next_suffix.get(base, 1)
            while f"{base}-{suffix}" in used:
                suffix += 1
            slug = f"{base}-{suffix}"
            next_suffix[base] = suffix + 1
        else:
            next_suffix.setdefault(base, 1)
        used.add(slug)
        slugs.add(slug)

    explicit_ids = _html_anchor_ids(markdown)
    return slugs, explicit_ids


def _display(path: Path, root: Path) -> str:
    try:
        return path.relative_to(root).as_posix()
    except ValueError:
        return path.as_posix()


def _check_destination(
    source: Path,
    root: Path,
    destination: str,
    line: int,
    contents: dict[Path, str],
    anchors: dict[Path, tuple[set[str], set[str]]],
) -> tuple[str | None, str | None]:
    """Return (broken message, skip message); at most one is set."""
    try:
        parts = urlsplit(destination)
    except ValueError:
        return None, "destination URL syntax is not supported"
    if parts.scheme or parts.netloc:
        return None, None  # External/absolute URLs are counted by the caller.
    if re.search(r"%(?![0-9A-Fa-f]{2})", parts.path + parts.fragment):
        return None, "malformed percent escape is not checked"

    decoded_path = unquote(parts.path)
    if not decoded_path:
        lexical = Path(os.path.abspath(source))
    elif decoded_path.startswith("/"):
        lexical = Path(os.path.abspath(root / decoded_path.lstrip("/")))
    else:
        lexical = Path(os.path.abspath(source.parent / decoded_path))
    if not _inside(lexical, root):
        return "local destination escapes the check root", None
    resolved, broken, skipped = _exact_path(
        lexical, root, parts.path or "(current document)"
    )
    if broken:
        return broken, None
    if skipped:
        return None, skipped
    assert resolved is not None

    fragment = unquote(parts.fragment)
    is_markdown = resolved.suffix.lower() in {".md", ".markdown"}
    if is_markdown and not resolved.is_file():
        return None, "non-regular Markdown target is not checked"
    if not fragment:
        return None, None
    if resolved.is_dir():
        return None, "fragment on a directory target cannot be checked"
    if not is_markdown:
        return None, None

    if resolved not in anchors:
        try:
            target_text = resolved.read_text(encoding="utf-8")
        except (OSError, UnicodeError) as exc:
            raise OSError(f"cannot read Markdown target {resolved}: {exc}") from exc
        contents[resolved] = target_text
        anchors[resolved] = heading_slugs(target_text)
    slugs, explicit_ids = anchors[resolved]
    wanted = _slug_base(fragment)
    if wanted in slugs:
        return None, None
    if fragment in explicit_ids:
        return None, "fragment matches a raw HTML anchor, which is not verified"
    return f"heading fragment not found: #{fragment}", None


def check_document(path: Path, root: Path, contents: dict[Path, str]) -> tuple[list[Problem], list[Note], int]:
    """Check one Markdown file; return broken links, skipped notes, external count."""
    try:
        text = contents.get(path)
        if text is None:
            text = path.read_text(encoding="utf-8")
            contents[path] = text
    except (OSError, UnicodeError) as exc:
        raise OSError(f"cannot read {_display(path, root)}: {exc}") from exc

    links, notes, problems = extract_links(text)
    heading_cache: dict[Path, tuple[set[str], set[str]]] = {path.resolve(): heading_slugs(text)}
    external_count = 0
    for link in links:
        try:
            parts = urlsplit(link.destination)
        except ValueError:
            notes.append(Note(link.line, "destination URL syntax is not supported"))
            continue
        if parts.scheme or parts.netloc:
            external_count += 1
            continue
        broken, skipped = _check_destination(
            path, root, link.destination, link.line, contents, heading_cache
        )
        if broken:
            problems.append(Problem(link.line, broken))
        elif skipped:
            notes.append(Note(link.line, skipped))
    external_count += sum(note.external for note in notes)
    return problems, notes, external_count


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Check supported local Markdown links. This is a partial GFM checker; it does not render Markdown."
    )
    parser.add_argument("--root", help="repository root within the current working directory (default: current directory)")
    args = parser.parse_args(argv)

    try:
        root = _resolve_root(args.root)
        paths, unsafe_sources = discover_markdown(root)
    except (ValueError, OSError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2

    contents: dict[Path, str] = {}
    total_problems = 0
    total_notes = 0
    total_external = 0
    read_errors = 0

    for path, reason in unsafe_sources:
        print(f"SKIPPED {_display(path, root)}: {reason}")
        total_notes += 1

    for path in paths:
        try:
            problems, notes, external_count = check_document(path, root, contents)
        except OSError as exc:
            print(f"ERROR: {exc}", file=sys.stderr)
            read_errors += 1
            continue
        for problem in problems:
            print(f"{_display(path, root)}:{problem.line}: BROKEN {problem.message}")
        for note in notes:
            print(f"{_display(path, root)}:{note.line}: SKIPPED {note.message}")
        total_problems += len(problems)
        total_notes += sum(not note.external for note in notes)
        total_external += external_count

    if read_errors:
        print(f"ERROR: {read_errors} document read/inspection error(s); {len(paths)} Markdown source(s) discovered")
        return 2
    print(f"Checked {len(paths)} Markdown source(s); {total_problems} broken supported link(s).")
    print(f"SKIPPED: {total_external} external link(s) were not fetched or checked.")
    print(f"SKIPPED: {total_notes} unsupported construct(s) or unsafe symlink source(s) were not verified.")
    print("LIMITS: partial GFM syntax and approximate GitHub heading slugs; this is not a renderer.")
    return 1 if total_problems else 0


if __name__ == "__main__":
    sys.exit(main())
