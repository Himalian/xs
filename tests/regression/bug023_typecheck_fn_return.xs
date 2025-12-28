-- bug023: --check did not propagate function return types through
-- call expressions, so `let r: str = fn_returning_int()` slipped past
-- the type checker. Fix: NODE_FN_DECL now registers a TY_FN signature
-- on the function symbol so call sites can verify return-type
-- compatibility.
fn add(a: int, b: int) -> int { return a + b }
fn greet(name: str) -> str { return "hello, {name}" }

let n: int = add(1, 2)
let s: str = greet("world")
assert_eq(n, 3)
assert_eq(s, "hello, world")

println("bug023: ok")
