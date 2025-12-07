-- contextual keywords: type, from, as, etc. usable as identifiers

fn check(type) { return type == "int" }
assert_eq(check("int"), true)
assert_eq(check("str"), false)

let type = "admin"
assert_eq(type, "admin")

struct User { type, name }
let u = User { type: "admin", name: "a" }
assert_eq(u.type, "admin")

let from = "sender"
let as = "alias"
assert_eq(from, "sender")
assert_eq(as, "alias")

let m = #{"type": "x", "value": 42}
assert_eq(m["type"], "x")

println("test_contextual_kw: all passed")
