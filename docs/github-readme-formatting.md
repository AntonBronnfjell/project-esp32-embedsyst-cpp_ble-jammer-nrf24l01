# GitHub-Flavored Markdown: HTML techniques for readable READMEs

A reference of the HTML tags GitHub's Markdown renderer supports, with neutral
examples you can lift into any repo. GitHub sanitizes HTML (no `<script>`, no
`style` attributes, limited CSS), so the reliable toolkit is a specific set of
tags and a few native extensions. The two tool READMEs in this repo
([`tools/rf-interference-detector`](../tools/rf-interference-detector/README.md),
[`tools/ble-scanner`](../tools/ble-scanner/README.md)) use everything below.

---

## 1. Centering with `<div align="center">`

Markdown has no alignment; the `align` attribute does. Wrap a title block, a
logo, or a badge row:

```html
<div align="center">

# Project Name
**One-line tagline**

</div>
```

> [!TIP]
> Leave blank lines **inside** the `<div>` around Markdown content — otherwise
> GitHub treats it as raw HTML and won't render the `#` heading or `**bold**`.

## 2. Badges (shields.io)

Badges are just images. Keep them in a single centered paragraph:

```markdown
![platform](https://img.shields.io/badge/platform-ESP32--S3-E7352C?logo=espressif&logoColor=white)
![status](https://img.shields.io/badge/status-stable-success)
```

Escape literal dashes in the label as `--`. Dynamic badges exist too, e.g.
`https://img.shields.io/github/actions/workflow/status/OWNER/REPO/ci.yml`.

## 3. Collapsible sections with `<details>`

Hide long troubleshooting / appendix content behind a click:

```html
<details>
<summary><b>Click to expand</b></summary>

<br>

Content here — remember a blank line so Markdown renders inside.

</details>
```

## 4. Light/dark-aware images with `<picture>`

Serve a different asset depending on the viewer's GitHub theme:

```html
<picture>
  <source media="(prefers-color-scheme: dark)"  srcset="docs/images/diagram-dark.png">
  <source media="(prefers-color-scheme: light)" srcset="docs/images/diagram-light.png">
  <img alt="wiring diagram" src="docs/images/diagram-light.png" width="520">
</picture>
```

`width=` on `<img>` lets you size images, which bare Markdown `![]()` can't.

## 5. Tables

Native Markdown tables cover most needs; alignment is set by the `---` row:

```markdown
| Pin  | GPIO | Note        |
| :--- | ---: | :---------: |
| CE   |   16 | left/right/center |
```

Use raw `<table>` only when you need cells to contain block content (lists,
multiple paragraphs, `<br>`) that Markdown tables can't hold.

## 6. GFM alert / admonition blocks

A GitHub-native extension — five types, rendered with an icon and color:

```markdown
> [!NOTE]
> Neutral, useful context.

> [!TIP]
> A helpful suggestion.

> [!IMPORTANT]
> Key information the reader must not miss.

> [!WARNING]
> Negative consequence if ignored.

> [!CAUTION]
> Risk / danger.
```

## 7. Other reliably-supported tags

| Tag | Use |
| --- | --- |
| `<kbd>Ctrl</kbd>` | render keystrokes: <kbd>Ctrl</kbd>+<kbd>C</kbd> |
| `<sub>` / `<sup>` | subscript / superscript (H<sub>2</sub>O, x<sup>2</sup>) |
| `<br>` | hard line break inside a cell or `<div>` |
| `<hr>` or `---` | horizontal rule |
| `<a id="anchor">` | manual anchor target for in-page links |
| task lists `- [x]` | GFM checkboxes |
| ` ```mermaid ` | GitHub renders Mermaid diagrams natively |

## 8. What does NOT work

- `<style>`, external CSS, or `style="..."` attributes — stripped.
- `<script>` and most event attributes — stripped.
- Arbitrary colored text — not supported; use badges or admonitions instead.
- Iframes / embedded video — link to the asset or use an animated GIF/`<img>`.

---

### A reusable header skeleton

```html
<div align="center">

# 🛰️ Project Name

**Short tagline that says what it is in one line**

![badge1](...) ![badge2](...) ![badge3](...)

</div>

> [!NOTE]
> One or two sentences of essential framing.

---
```
