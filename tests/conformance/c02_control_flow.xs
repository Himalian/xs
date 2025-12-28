-- if / else / while / for / match must all evaluate to the documented
-- result shape.

-- if as expression
let x = if true { 1 } else { 2 }
assert_eq(x, 1)

-- while
var n = 0
var i = 0
while i < 10 { n = n + i; i = i + 1 }
assert_eq(n, 45)

-- for over array
var total = 0
for v in [1, 2, 3, 4] { total = total + v }
assert_eq(total, 10)

-- match with guards
let m = match 5 {
    x if x < 0 => "neg"
    0 => "zero"
    x if x < 10 => "small"
    _ => "big"
}
assert_eq(m, "small")

println("CONFORMANCE OK")
