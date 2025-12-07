-- make sure oversized strings do not overflow fixed-size buffers

fn repeat(s, n) {
    var out = ""
    var i = 0
    while i < n { out = out ++ s; i = i + 1 }
    return out
}

let big = repeat("x", 5000)
assert_eq(big.len(), 5000)

-- interpolation with a very long embedded expression
let a = 1 + 2 + 3 + 4 + 5 + 6 + 7 + 8 + 9 + 10
assert_eq("sum={a}".len(), 6)

-- string concat chain
var s = ""
var i = 0
while i < 200 { s = s ++ "abc"; i = i + 1 }
assert_eq(s.len(), 600)

println("test_huge_strings: ok")
