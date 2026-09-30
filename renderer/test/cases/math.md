Inline $E = mc^2$, a price of $5 and $10, and an escaped \$ sign.

$$
\int_0^1 x^2\,dx = \frac{1}{3}
$$

Broken: $\fracc{1}{2}$.

```nebula:math
tex: \sum_{i=1}^{n} i = \frac{n(n+1)}{2}
number: true
```

```nebula:math
\alpha^
```

```nebula:plot
x: [0, 20]
params: {a: 0.15, w: 2}
functions:
  - {y: "exp(-a x) cos(w x)", label: signal}
  - "exp(-a x)"
```

```nebula:plot
functions:
  - "exp(-a x) sn(x)"
```

```nebula:plot
type: parametric
functions:
  - {x: "cos(3t)", y: "sin(2t)"}
```

```nebula:plot
type: polar
functions: "1 + cos(theta)"
```

```nebula:plot
type: surface
x: [-3, 3]
y: [-3, 3]
functions: "sin(x) cos(y)"
params: {x: 1}
```

```nebula:callout
text: Bad $\beta^$ in a callout.
```
