-- EXPECT_ERROR: S0042

let outer = []
fn fill() {
    @scoped let tmp = "leak"
    outer.push(tmp)
}
fill()
