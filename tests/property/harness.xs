-- Tiny property-based test harness in XS.
--
-- A property is a function that returns true/false (or throws) given a
-- randomly generated input. forall() runs the property N times and
-- prints PASS/FAIL. Seed is fixed per run so failures are reproducible.
--
-- Usage from each tests/property/*.xs file:
--     load "tests/property/harness.xs"
--     forall("name", 100, () => gen_int_array(20), (xs) => ...)

var _seed = 12648430

fn _rand() {
    _seed = (_seed * 1103515245 + 12345) % 2147483648
    return _seed
}

fn _rand_int(lo, hi) {
    return lo + _rand() % (hi - lo + 1)
}

fn gen_int() { return _rand_int(-100, 100) }
fn gen_nat() { return _rand_int(0, 100) }

fn gen_int_array(max_len) {
    let n = _rand_int(0, max_len)
    var out = []
    var i = 0
    while i < n {
        out.push(gen_int())
        i = i + 1
    }
    return out
}

fn gen_str(max_len) {
    let alpha = "abcdefghijklmnopqrstuvwxyz"
    let n = _rand_int(0, max_len)
    var out = ""
    var i = 0
    while i < n {
        out = out + alpha[_rand_int(0, alpha.len() - 1)]
        i = i + 1
    }
    return out
}

var _prop_failed = 0
var _prop_passed = 0

fn forall(name, n, gen, prop) {
    var i = 0
    while i < n {
        let input = gen()
        var ok = false
        try {
            ok = prop(input)
        } catch e {
            ok = false
            println("  FAIL  {name} iter={i} threw: {e}")
        }
        if !ok {
            _prop_failed = _prop_failed + 1
            println("  FAIL  {name} iter={i} input={input}")
            return
        }
        i = i + 1
    }
    _prop_passed = _prop_passed + 1
    println("  ok    {name} ({n} iters)")
}

fn prop_report(suite) {
    println("[property:{suite}] {_prop_passed} passed, {_prop_failed} failed")
    if _prop_failed > 0 { throw "property suite failed" }
}
