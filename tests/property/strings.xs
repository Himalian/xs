-- String invariants.
load "tests/property/harness.xs"

forall("empty has length 0", 1,
    || "",
    |s| s.len() == 0)

forall("concat lengths add", 50,
    || [gen_str(10), gen_str(10)],
    |pair| (pair[0] + pair[1]).len() == pair[0].len() + pair[1].len())

forall("reverse involutive (chars)", 50,
    || gen_str(10),
    |s| {
        var r = ""
        var i = s.len() - 1
        while i >= 0 { r = r + s[i]; i = i - 1 }
        var rr = ""
        var j = r.len() - 1
        while j >= 0 { rr = rr + r[j]; j = j - 1 }
        rr == s
    })

prop_report("strings")
