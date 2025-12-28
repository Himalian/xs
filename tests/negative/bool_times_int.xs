-- EXPECT_RUNTIME_ERROR
-- multiplying a bool by an int has no defined meaning; should error
let r = true * 5
println(r)
