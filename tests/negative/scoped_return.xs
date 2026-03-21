-- EXPECT_ERROR: S0042

fn build() {
    @scoped let buf = [1, 2, 3]
    return buf
}

println(build())
