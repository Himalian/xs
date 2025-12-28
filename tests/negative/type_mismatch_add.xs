-- EXPECT_RUNTIME_ERROR
-- calling a non-function value must throw, not silently return null
let x = 42
let y = x()
println(y)
