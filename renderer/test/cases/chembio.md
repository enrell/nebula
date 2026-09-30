# Chemistry and biology

```nebula:molecule
CC(=O)Oc1ccccc1C(=O)O
```

```nebula:molecule
items:
  - {smiles: "c1ccccc1", name: benzene}
  - "C1CC(C"
```

```nebula:reaction
smiles: "CCO.CC(=O)O>[H+]>CCOC(C)=O.O"
conditions: H₂SO₄
```

```nebula:reaction
CCO>>
```

```nebula:structure
data: 1a8o.pdb
```

```nebula:structure
data: results.csv
```

```nebula:sequence
data: globins.fa
annotations:
  - {start: 1, end: 10, label: N-term}
```

```nebula:sequence
seq: ACGTXACGT
type: dna
```

```nebula:tree
data: globins.nwk
highlight: [human_HBB, gorilla]
```

```nebula:tree
((a,b),c
```

```nebula:tracks
region: "chr7:117,480,000-117,670,000"
tracks:
  - {type: features, data: genes.bed}
  - {type: signal, data: coverage.bedgraph}
```

```nebula:tracks
region: chr7
tracks:
  - {type: variants, variants: [{pos: 1}]}
```
