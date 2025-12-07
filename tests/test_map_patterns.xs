-- map pattern matching in match expressions

let resp = #{"status": 200, "body": "ok"}

let desc = match resp {
    #{"status": 200, "body": b} => "ok:{b}"
    #{"status": 404} => "not found"
    _ => "?"
}
assert_eq(desc, "ok:ok")

let err = #{"status": 500}
let d2 = match err {
    #{"status": 200} => "ok"
    #{"status": s} => "err:{s}"
    _ => "?"
}
assert_eq(d2, "err:500")

-- shorthand: bare identifier binds to a key of the same name
let user = #{"name": "alice", "age": 30}
let greeting = match user {
    #{"name": n} => "hi {n}"
    _ => "?"
}
assert_eq(greeting, "hi alice")

-- open pattern with `..`: allow extra keys
let big = #{"a": 1, "b": 2, "c": 3}
let s = match big {
    #{"a": a, ..} => a
    _ => -1
}
assert_eq(s, 1)

-- a closed pattern does NOT require extra keys, just those present
-- (a closed pattern will still match when the map has additional keys;
--  enforce exact shape with an explicit len check if you need strictness)
let d3 = match #{"x": 1, "y": 2} {
    #{"x": a, "y": b} => a + b
    _ => 0
}
assert_eq(d3, 3)

-- nested: key holds another map
let wrap = #{"meta": #{"v": 42}, "data": "hi"}
let n2 = match wrap {
    #{"meta": #{"v": v}} => v
    _ => 0
}
assert_eq(n2, 42)

-- missing key fails to match, falls through
let partial = #{"status": 200}
let d4 = match partial {
    #{"status": 200, "body": b} => "body {b}"
    #{"status": s} => "no body, status {s}"
    _ => "?"
}
assert_eq(d4, "no body, status 200")

println("test_map_patterns: all passed")
