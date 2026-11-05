# Checking documentation links

Run the local documentation link checker from the repository root:

```sh
python3 scripts/check-docs.py
```

You can select another repository root below the current directory:

```sh
python3 scripts/check-docs.py --root path/to/repository
```

The checker reads the current working tree. It does not use Git, so new and
untracked Markdown files are included. It checks the root `README.md` and
`CONTRIBUTING.md`, Markdown files under `docs/`, and `README.md` files under
`platforms/`, `systemc-components/`, and `tests/`. It skips `.git`, generated
build directories named `build`, `build-*`, or `build_*`, `_site`, `vendor`,
`node_modules`, `deps`, `.venv`, and `venv`. Other names such as `building`
remain in scope. This includes build guides such as `docs/building.md`.

The checker uses only the Python standard library. It only reads files and
checks paths. It does not write files, start subprocesses, run document or
build commands, or access the network. External links are counted but are not
fetched or verified.

## What it checks

It checks common same-line GFM inline links, images, reference definitions,
and reference uses. It ignores links inside fenced or inline code. Markdown
inside `<details>` blocks is scanned, including content separated by blank
lines. Relative paths and repository-root paths are bounded to the selected
root, and every local path component must match case exactly. A path with
incorrect file or directory case is broken, including on case-insensitive
filesystems. Percent-encoded paths and query strings are
supported. Fragment-only links check headings in the current file. Markdown
heading fragments use approximate GitHub-style slugs, including duplicate
headings. Inline code content remains part of a heading slug. Bare GFM
external URLs and autolinks are counted as unchecked external links.

`BROKEN` findings produce exit status 1. Input or read errors produce status
2. Exit 0 means only that no detected broken supported links were found; it
does not mean that all links, external destinations, or rendered output were
verified. Each run prints separate external-link and
unsupported-construct `SKIPPED` counts, plus a limitations line, including
when there are no broken links. A skipped item is not verified and must not
be read as a successful check. External counts are link occurrences, not
unique destinations: a bare URL and a separate Markdown link to the same URL
count twice, while one recognized link span counts once.

## Limits

This checker is deliberately not a complete GFM parser or GitHub renderer.
Complex or multiline link syntax that it cannot parse confidently is reported
as skipped, not as a broken link. Raw HTML link and anchor attributes are not
verified; a fragment that matches a raw HTML anchor is reported as skipped.
Other explicit HTML IDs do not count as verified heading anchors. GFM
autolinks are also skipped. Slug handling is approximate and may differ from
GitHub for renderer-specific Markdown, Unicode symbols, or unusual heading
markup. Non-Markdown file fragments and fragments on directory links are not
verified. A non-regular Markdown target is skipped without reading it. A zero
exit status means only that no broken links were found in the supported
subset.

This is a manual local check. It is not wired into or activated by CI.

## Integration boundary

Repository Markdown is maintained guide format. The current
`.github/workflows/gh-pages.yml` and Doxygen configuration exclude `docs/`, so
these guides and their generated links are not integrated into generated
output. Configuration review is complete; generated-site integration is
pending separate approval. This does not mean that Doxygen has passed. Workflow
and Actions execution remain paused. Do not enable them or claim that CI tested
this checker.
