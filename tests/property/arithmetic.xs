-- Arithmetic identity properties. These should hold under both interp
-- and vm backends; cross-backend divergence would be caught elsewhere.
load "tests/property/harness.xs"

forall("add commutes", 100,
    || [gen_int(), gen_int()],
    |xs| xs[0] + xs[1] == xs[1] + xs[0])

forall("mul commutes", 100,
    || [gen_int(), gen_int()],
    |xs| xs[0] * xs[1] == xs[1] * xs[0])

forall("zero identity", 100,
    || gen_int(),
    |n| n + 0 == n && 0 + n == n)

forall("sub inverse of add", 100,
    || [gen_int(), gen_int()],
    |xs| (xs[0] + xs[1]) - xs[1] == xs[0])

forall("abs non-negative", 100,
    || gen_int(),
    |n| { let a = if n < 0 { -n } else { n }; a >= 0 })

prop_report("arithmetic")
