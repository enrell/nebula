```nebula:chart
type: line
data: bench.csv
x: run
y: [before_ms, after_ms]
unit: ms
```

```nebula:chart
type: pie
rows: [{k: a, v: 1}, {k: b, v: 3}]
x: k
y: [v, k]
```

```nebula:chart
type: bar
data: bench.csv
x: run
y: befor_ms
```

```nebula:chart
type: bar
data: bench.csv
x: before_ms
y: run
```

```nebula:chart3d
type: surface
data: grid.csv
x: lr
y: batch
z: loss
```

```nebula:chart3d
type: surface
rows: [[1, 1, 3], [1, 2, 4], [2, 1, 5]]
columns: [a, b, c]
x: a
y: b
z: c
```

```nebula:html
height: 120
html: |
  <canvas id="c"></canvas>
  <script src="https://cdn.example.com/lib.js"></script>
```

```nebula:html
html: <p>x</p>
file: a.html
```
