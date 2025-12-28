-- Every documented pattern shape: literals, bindings, slices, maps,
-- tuples, wildcards. Patterns must only match subjects of the right
-- shape, binding variables from the successful arm only.

-- literal vs wild
assert_eq(match 1 { 0 => "zero" _ => "other" }, "other")

-- binding
assert_eq(match 7 { x => x + 1 }, 8)

-- slice patterns, must not match non-arrays
assert_eq(match [] { [] => "empty" _ => "full" }, "empty")
assert_eq(match [1, 2] { [a, b] => a + b _ => -1 }, 3)
assert_eq(match 99 { [] => "empty" _ => "int" }, "int")

-- map patterns
let m = #{ "k": 10, "v": 20 }
assert_eq(match m { #{"k": k} => k _ => -1 }, 10)
assert_eq(match 1 { #{} => "map" _ => "int" }, "int")

-- tuple patterns
let pair = (1, 2)
assert_eq(match pair { (a, b) => a * b }, 2)

println("CONFORMANCE OK")
