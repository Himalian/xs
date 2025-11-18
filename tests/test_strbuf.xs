-- string builder tests (exercises the language string ops that
-- the underlying strbuf.c powers)

-- basic string building
let parts = ["hello", " ", "world"]
var result = ""
for p in parts { result = result ++ p }
assert_eq(result, "hello world")

-- repeated concatenation
var buf = ""
for i in 0..10 { buf = buf ++ str(i) }
assert_eq(buf, "0123456789")

-- string contains
assert_eq("hello world".contains("world"), true)
assert_eq("hello world".contains("xyz"), false)

-- starts_with / ends_with
assert_eq("hello world".starts_with("hello"), true)
assert_eq("hello world".starts_with("world"), false)
assert_eq("hello world".ends_with("world"), true)
assert_eq("hello world".ends_with("hello"), false)

-- trim
assert_eq("  hello  ".trim(), "hello")

-- upper / lower
assert_eq("hello".upper(), "HELLO")
assert_eq("HELLO".lower(), "hello")
assert_eq("Hello World".upper(), "HELLO WORLD")

-- split
let words = "one,two,three".split(",")
assert_eq(len(words), 3)
assert_eq(words[0], "one")
assert_eq(words[1], "two")
assert_eq(words[2], "three")

-- join
let joined = ["a", "b", "c"].join("-")
assert_eq(joined, "a-b-c")

-- repeat
assert_eq("ab".repeat(3), "ababab")
assert_eq("x".repeat(5), "xxxxx")

-- replace
assert_eq("hello world".replace("world", "there"), "hello there")
assert_eq("aaa".replace("a", "bb"), "bbbbbb")

-- index_of
assert_eq("hello world".index_of("world"), 6)
assert_eq("hello world".index_of("xyz"), -1)
assert_eq("abcabc".index_of("bc"), 1)

-- slice / substring
assert_eq("hello world".slice(0, 5), "hello")
assert_eq("hello world".slice(6), "world")

-- length
assert_eq(len(""), 0)
assert_eq(len("hello"), 5)
assert_eq(len("hi there"), 8)

-- interpolation (exercises string building internally)
let name = "world"
let greeting = "hello {name}!"
assert_eq(greeting, "hello world!")

let x = 42
let msg = "the answer is {x}"
assert_eq(msg, "the answer is 42")

-- multi-part building
var s = ""
for i in 0..5 {
    s = s ++ "item" ++ str(i) ++ ","
}
assert_eq(s, "item0,item1,item2,item3,item4,")

-- empty string ops
assert_eq("".trim(), "")
assert_eq("".to_upper(), "")
assert_eq("".split(","), [""])
assert_eq(len("".split(",")), 1)

-- pad operations
assert_eq("42".pad_left(5, "0"), "00042")
assert_eq("hi".pad_right(5, "."), "hi...")

-- chars
let chars = "hello".chars()
assert_eq(len(chars), 5)
assert_eq(chars[0], "h")
assert_eq(chars[4], "o")

println("all strbuf tests passed")
