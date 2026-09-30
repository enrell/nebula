# Test fixtures

Small files the checker tests and `tests/media.sh` read. Everything here is synthetic unless listed below.

| File | Source |
|---|---|
| `1a8o.pdb` | PDB entry 1A8O (HIV-1 capsid C-terminal domain), as shipped in Biopython's test suite. PDB data is public domain (CC0). |
| `globins.fa` | β-globin sequences typed for the tests; human and chimpanzee match UniProt P68871, the others are close approximations. Do not use them as data. |
| `refs.bib` | Real bibliographic records (Watson & Crick 1953, Schrödinger 1926, LIGO/Virgo 2016), plus one deliberately broken entry. |
| `genes.bed`, `coverage.bedgraph` | Synthetic features around the CFTR locus (illustrative coordinates, not an annotation release). |
| `density.npy`, `orbital.cube`, `field.npy`, `regions.geojson`, `img/cells*.png`, `*.csv` | Generated for the tests. |
