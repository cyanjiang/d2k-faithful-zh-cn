# Coding conventions

- C++20. Namespaces follow layers: `d2k::formats`, `d2k::sim`, `d2k::platform`, and so on.
- Files `snake_case.cpp/.hpp`. Types `PascalCase`, functions and variables
  `snake_case`, constants `kPascalCase`, members end in `_`.
- `sim/` must be deterministic. Floats only as the original uses them
  (float32 with its formulas and operation order, strict IEEE, no FMA; see
  CLAUDE.md §10); no C library `rand()` (the game's own `sim::Rng`), no
  wall-clock time, no unordered-container iteration in logic. It never
  depends on render, audio, video, ui, or platform. The determinism
  fingerprint test compares builds across compilers.
- Parsers use `formats::ByteReader` (bounds-checked) and throw `FormatError`
  on bad data.
- Mark unverified guesses with `// TODO(verify):`. Cite exe addresses as
  `// RE: 1.06 0x00412340 (unit::update)` with the details in `docs/re/`.
- Tests use doctest. Golden tests against real data must skip when
  `gamedata/` is absent.
- Never commit original data or large decompiled listings.
