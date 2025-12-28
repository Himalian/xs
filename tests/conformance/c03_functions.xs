-- fn declarations, lambdas, closures, fn*, return.

fn add(a, b) { return a + b }
assert_eq(add(2, 3), 5)

-- closures capture their environment
fn mk_counter() {
    var n = 0
    return fn() { n = n + 1; return n }
}
let c = mk_counter()
assert_eq(c(), 1)
assert_eq(c(), 2)
assert_eq(c(), 3)

-- generator (fn*) produces an iterable; drain it via for
fn* range(n) {
    var i = 0
    while i < n { yield i; i = i + 1 }
}
var rs = []
for x in range(4) { rs.push(x) }
assert_eq(rs, [0, 1, 2, 3])

-- lambdas
let inc = |x| x + 1
assert_eq(inc(5), 6)

println("CONFORMANCE OK")
