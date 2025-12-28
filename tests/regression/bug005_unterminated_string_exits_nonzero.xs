-- bug005: an unterminated string literal printed L0001 but the program
-- kept running with rc=0 (lexer errors were rendered directly without
-- flowing through the diag context). The fix tracks lex.n_errors and
-- treats it as a fatal parse failure.
--
-- The actual failure case lives in tests/negative/unclosed_string.xs;
-- this file asserts that a well-formed string still works.
let s = "hello\n"
assert_eq(s.len(), 6)
println("bug005: ok")
