-- EXPECT_RUNTIME_ERROR
-- subtracting strings has no defined meaning; should error not null
let r = "foo" - "bar"
println(r)
