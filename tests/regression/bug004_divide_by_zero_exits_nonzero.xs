-- bug004: divide by zero reported a runtime error but the process still
-- exited 0, masking bugs in pipelines that relied on non-zero exits.
-- Fix: xs_runtime_error now increments a global counter that main
-- forces into the process exit code.
--
-- This regression fixture asserts the inverse direction: a clean program
-- that never triggers a runtime error still exits 0.
let x = 1 / 1
assert_eq(x, 1)
println("bug004: ok")
