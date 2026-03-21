-- a @scoped binding that is only printed (not stored, not returned)
-- must be accepted. exercises the safe-callee allow-list.

fn run() {
    @scoped let n = 42
    println(n)
    println(n + 1)
}

run()
