# Physics, networks, maps and images

```nebula:field
u: y
v: "-sin(x) - b y"
params:
  b: {value: 0.25, min: 0, max: 1.5}
```

```nebula:field
u: y
v: "-b x"
params:
  b: {value: 2, min: 0, max: 1}
```

```nebula:field
data: field.npy
```

```nebula:animation
t: [0, 6.28]
x: [-3, 3]
functions: ["sin(x - t)"]
points:
  - {x: "cos(t)", y: "sin(t)"}
```

```nebula:animation
functions: ["sin(x - tt)"]
```

```nebula:volume
data: density.npy
```

```nebula:volume
data: field.npy
```

```nebula:graph
- [a, b, 2]
- [b, c]
```

```nebula:graph
nodes: [alpha, beta]
edges:
  - [alpha, betta]
```

```nebula:diagram
source: |
  flowchart LR
    A --> B
```

```mermaid
sequenceDiagramm
  A->>B: hi
```

```nebula:map
data: regions.geojson
color: warming
```

```nebula:map
data: regions.geojson
color: warmth
```

```nebula:image
src: img/cells.png
compare: img/cells-segmented.png
scale: "0.65 µm"
```

```nebula:image
src: img/cells.png
scale: tiny
```
