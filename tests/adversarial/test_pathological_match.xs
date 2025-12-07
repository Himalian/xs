-- match exprs with many arms, deeply nested patterns, overlapping guards

fn classify(n) {
    return match n {
        0 => "zero"
        1 | 2 | 3 | 4 | 5 => "small"
        6..=10 => "medium"
        n if n < 0 => "negative"
        n if n > 1000000 => "huge"
        _ => "other"
    }
}

assert_eq(classify(0), "zero")
assert_eq(classify(3), "small")
assert_eq(classify(7), "medium")
assert_eq(classify(-5), "negative")
assert_eq(classify(9999999), "huge")
assert_eq(classify(42), "other")

-- nested map patterns in every arm
fn route(req) {
    return match req {
        #{"method": "GET", "path": "/"} => "home"
        #{"method": "GET", "path": p} => "get:{p}"
        #{"method": "POST", "path": p, "body": b} => "post:{p}:{b}"
        _ => "404"
    }
}

assert_eq(route(#{"method": "GET", "path": "/"}), "home")
assert_eq(route(#{"method": "GET", "path": "/x"}), "get:/x")
assert_eq(route(#{"method": "POST", "path": "/y", "body": "hi"}), "post:/y:hi")
assert_eq(route(#{"method": "DELETE", "path": "/z"}), "404")

println("test_pathological_match: ok")
