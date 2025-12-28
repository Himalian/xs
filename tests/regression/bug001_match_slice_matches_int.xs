-- bug001: match 99 { [] => "empty" } matched and bound the slice
-- pattern against an integer subject. The fix was to emit an <array-like>
-- OP_IS guard before the length check in the VM compiler.
let r = match 99 {
    [] => "empty"
    [x] => "got {x}"
    _ => "other"
}
assert_eq(r, "other")
println("bug001: ok")
