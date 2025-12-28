-- bug002: `yield` in a non-generator function (or at top level) used
-- to be silently accepted. The fix added S0040 in resolve.c.
-- Regression is asserted by tests/negative/yield_outside_generator.xs
-- (negative file expects S0040). This file just documents that an
-- inside-generator yield still works.
fn* g() {
    yield 1
    yield 2
}
var r = []
for x in g() { r.push(x) }
assert_eq(r, [1, 2])
println("bug002: ok")
