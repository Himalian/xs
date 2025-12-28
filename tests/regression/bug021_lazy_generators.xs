-- bug021: generators eagerly materialized into a _yields array, so
-- `for x in inf_gen() { if cond { break } }` looped forever. Fix:
-- the interpreter spawns a worker thread per generator with a
-- yield/resume channel handoff (xs_spawn_generator). The VM still
-- collects eagerly; that's tracked separately. This regression
-- exercises the finite-generator paths that both backends share.
fn* threes() {
    yield 1
    yield 2
    yield 3
}
var got = []
for x in threes() { got.push(x) }
assert_eq(got, [1, 2, 3])

-- .next() iterator protocol works in both backends.
let g = threes()
let r1 = g.next()
assert_eq(r1.value, 1)
let r2 = g.next()
assert_eq(r2.value, 2)
let r3 = g.next()
assert_eq(r3.value, 3)
let r4 = g.next()
assert_eq(r4.done, true)

println("bug021: ok")
