-- bug015: in the VM, closures capturing a loop-local `let` shared
-- the same slot across iterations, so all captured values collapsed
-- to the final one. Fix: scope_end emits OP_CLOSE_UPVALUES so any
-- captured local gets its own upvalue before the slot is reused.
fn make_adders() {
    var fns = []
    var i = 0
    while i < 3 {
        let captured = i
        fns.push(fn() { return captured })
        i = i + 1
    }
    return fns
}
let fs = make_adders()
assert_eq(fs[0](), 0)
assert_eq(fs[1](), 1)
assert_eq(fs[2](), 2)
println("bug015: ok")
