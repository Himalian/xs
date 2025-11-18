-- pattern match compilation tests

-- basic literal matching
fn classify(x) {
    match x {
        1 => "one"
        2 => "two"
        3 => "three"
        _ => "other"
    }
}
assert_eq(classify(1), "one")
assert_eq(classify(2), "two")
assert_eq(classify(3), "three")
assert_eq(classify(99), "other")

-- variable binding in match
fn describe(val) {
    match val {
        0 => "zero"
        n => "number: " ++ str(n)
    }
}
assert_eq(describe(0), "zero")
assert_eq(describe(42), "number: 42")

-- tuple matching
fn sum_pair(p) {
    match p {
        (0, y) => y
        (x, 0) => x
        (x, y) => x + y
    }
}
assert_eq(sum_pair((0, 5)), 5)
assert_eq(sum_pair((3, 0)), 3)
assert_eq(sum_pair((2, 3)), 5)

-- enum matching
enum Shape {
    Circle(r),
    Rect(w, h),
    Point
}

fn area(s) {
    match s {
        Shape::Circle(r) => 3 * r * r
        Shape::Rect(w, h) => w * h
        Shape::Point => 0
    }
}
assert_eq(area(Shape::Circle(5)), 75)
assert_eq(area(Shape::Rect(3, 4)), 12)
assert_eq(area(Shape::Point), 0)

-- or patterns
fn is_weekend(day) {
    match day {
        "sat" | "sun" => true
        _ => false
    }
}
assert_eq(is_weekend("sat"), true)
assert_eq(is_weekend("sun"), true)
assert_eq(is_weekend("mon"), false)

-- guarded match
fn abs_val(x) {
    match x {
        n if n < 0 => -n
        n => n
    }
}
assert_eq(abs_val(-5), 5)
assert_eq(abs_val(10), 10)

-- range patterns
fn grade(score) {
    match score {
        90..101 => "A"
        80..90 => "B"
        70..80 => "C"
        _ => "F"
    }
}
assert_eq(grade(95), "A")
assert_eq(grade(85), "B")
assert_eq(grade(75), "C")
assert_eq(grade(50), "F")

-- nested match
fn nested(x) {
    match x {
        (1, (2, 3)) => "deep"
        (1, _) => "shallow"
        _ => "none"
    }
}
assert_eq(nested((1, (2, 3))), "deep")
assert_eq(nested((1, (4, 5))), "shallow")
assert_eq(nested((2, (2, 3))), "none")

-- wildcard in different positions
fn first_or_second(pair) {
    match pair {
        (1, _) => "first is one"
        (_, 2) => "second is two"
        _ => "neither"
    }
}
assert_eq(first_or_second((1, 99)), "first is one")
assert_eq(first_or_second((99, 2)), "second is two")
assert_eq(first_or_second((3, 3)), "neither")

println("all match compiler tests passed")
