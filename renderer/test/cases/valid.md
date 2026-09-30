---
title: Everything valid
---
Intro with a [link](https://example.com) and an image ![px](img/pixel.png).

```nebula:callout
Short form, the body is the text.
```

```nebula:stats #kpis
- {label: Tests, value: 142, delta: +18, tone: good}
- {label: Size, value: 2 MB}
```

```nebula:table
columns: [suite, {key: passed, label: ok}]
data: results.csv
sort: {by: passed, desc: true}
```

```nebula:table
rows:
  - {name: x, n: 1}
  - {name: y, n: 2}
```

```nebula:table
data: rows.json
```

```nebula:checklist
- "[x] done"
- "[ ] todo"
- {text: running, status: running}
```

```nebula:code
file: src/sample.py
lines: 5-6
```

```nebula:code
code: echo hi
lang: bash
```

```nebula:image
img/pixel.png
```

```js
// ordinary code fences stay Markdown
```
