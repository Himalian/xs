-- Core literals: the parser must accept every shape and the runtime
-- must round-trip the value untouched.
assert_eq(0, 0)
assert_eq(-1, -1)
assert_eq(1_000_000, 1000000)
assert_eq(0xff, 255)
assert_eq(0b1010, 10)
assert_eq(0o17, 15)

assert_eq(3.14 + 0.0, 3.14)
assert_eq(-2.5, -2.5)

assert_eq("hello".len(), 5)
assert_eq("\n".len(), 1)
assert_eq("a\tb".len(), 3)

assert_eq(true, true)
assert_eq(false, false)
assert_eq(null, null)

assert_eq([1, 2, 3].len(), 3)
assert_eq(#{"k": 1}["k"], 1)

println("CONFORMANCE OK")
