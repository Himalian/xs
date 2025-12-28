# Conformance suite

These are the tests the language must pass to be called "XS". They
cover the minimum surface documented in LANGUAGE.md:

- core literals and arithmetic
- control flow (if/while/for/match)
- functions, closures, generators
- collections (array, map, tuple, struct)
- error handling (try/catch/finally, defer)
- pattern matching against every supported pattern kind
- modules (import/export)

Every file must run to completion and print `CONFORMANCE OK` as its
final line. If a conformance file fails, the language is broken; the
regression corpus exists for smaller targeted fixtures.
