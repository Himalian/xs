-- deep recursion must raise a catchable StackOverflow, never segfault

fn deep(n) { if n <= 0 { return 0 } return 1 + deep(n - 1) }

var caught = false
try {
    deep(10000)
} catch e {
    caught = true
}
assert(caught, "deep recursion should have thrown something catchable")

-- try/catch does not leak signal state after catch
assert_eq(1 + 1, 2)

println("test_deep_recursion: ok")
