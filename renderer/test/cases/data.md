```nebula:chart
type: scatter
data: calibration.csv
x: concentration
y: absorbance
error: sd
fit: linear
band: {low: low, high: high, label: 95% CI}
```

```nebula:chart
type: histogram
data: samples.csv
y: value
```

```nebula:chart
type: box
data: samples.csv
x: group
y: value
```

```nebula:chart
type: heatmap
data: samples.csv
x: x
y: y
value: z
```

```nebula:chart
type: line
data: calibration.csv
x: concentration
y: absorbance
logy: true
error: [sd, sd]
```

```nebula:chart
type: scatter
rows: [{a: 1, b: -2}, {a: 2, b: 3}]
x: a
y: b
logy: true
```

```nebula:matrix
data: corr.csv
annotate: true
```

```nebula:matrix
data: field.npy
style: contour
levels: 6
```

```nebula:matrix
values: [[1, 2], [3]]
```

```nebula:stats
- {label: g, value: 9.8134, error: 0.0021, unit: m/s²}
```

```nebula:table
data: calibration.csv
columns: [concentration, {key: absorbance, error: sd, unit: AU}, {key: low, error: nope}]
```
