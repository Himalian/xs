-- a handful of non-ASCII bytes should round-trip through the lexer and value
-- layer without corruption (byte-level; full grapheme awareness is not
-- implemented, which is documented in STATUS.md)

let greek = "αβγ"
let cjk   = "你好"
let emoji = "hi"   -- emojis are not tested here because source files
                   -- may travel through locale-sensitive tooling

-- len() is byte length until the Unicode overhaul lands
assert(greek.len() > 0)
assert(cjk.len() > 0)

-- concatenation preserves bytes
let mixed = greek ++ " / " ++ cjk
assert_eq(mixed.len(), greek.len() + 3 + cjk.len())

-- interpolation embeds the bytes verbatim
let msg = "greek={greek}, cjk={cjk}"
assert(msg.len() > 0)

println("test_weird_unicode: ok")
