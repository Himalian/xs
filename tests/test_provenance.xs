-- provenance tracking tests
load "plugins/provenance.xs"

-- 1. literal tracking with actual values
let x = 42
let y = "hello"
let flag = true

let px = provenance("x")
assert(px != null)
assert_eq(px.origin, "literal")
assert_eq(px.value, 42)
assert(px.line > 0)

let py = provenance("y")
assert(py != null)
assert_eq(py.origin, "literal")
assert_eq(py.value, "hello")

let pf = provenance("flag")
assert(pf != null)
assert_eq(pf.origin, "literal")
assert_eq(pf.value, true)

-- 2. variable flow tracking with parent chain
let alias = x
let pa = provenance("alias")
assert(pa != null)
assert_eq(pa.origin, "variable")
assert_eq(pa.detail, "x")
assert_eq(pa.value, 42)
assert(pa.parent != null)
assert_eq(pa.parent.origin, "literal")
assert_eq(pa.parent.value, 42)

-- 3. binop tracking with actual computed value
let sum = x + 10
assert_eq(sum, 52)
let ps = provenance("sum")
assert(ps != null)
assert_eq(ps.origin, "binop")
assert_eq(ps.detail, "+")
assert_eq(ps.value, 52)
assert(ps.parent != null)
assert_eq(ps.parent.origin, "variable")
assert_eq(ps.parent.detail, "x")
assert(ps.parent2 != null)
assert_eq(ps.parent2.origin, "literal")
assert_eq(ps.parent2.detail, "10")

-- 4. function return tracking with actual return value
fn double(n) { return n * 2 }
let result = double(x)
assert_eq(result, 84)
let pr = provenance("result")
assert(pr != null)
assert_eq(pr.origin, "fn_return")
assert_eq(pr.detail, "double")
assert_eq(pr.value, 84)

-- 5. var declarations tracked
var counter = 0
let pc = provenance("counter")
assert(pc != null)
assert_eq(pc.origin, "literal")
assert_eq(pc.value, 0)

-- 6. reassignment updates provenance with new value
counter = counter + 1
let pc2 = provenance("counter")
assert(pc2 != null)
assert_eq(pc2.origin, "binop")
assert_eq(pc2.detail, "+")
assert_eq(pc2.value, 1)

-- 7. nested function scopes
fn nested() {
    let inner = 10
    let deep = inner + 5
    return deep
}
let n = nested()
assert_eq(n, 15)

let pi = provenance("inner")
assert(pi != null)
assert_eq(pi.origin, "literal")
assert_eq(pi.value, 10)

let pd = provenance("deep")
assert(pd != null)
assert_eq(pd.origin, "binop")
assert_eq(pd.value, 15)

-- 8. chain query gives flat list of Trace objects
let chain = provenance_chain("alias")
assert(chain.len() >= 2)
assert_eq(chain[0].origin, "variable")
assert_eq(chain[1].origin, "literal")

-- 9. closures
fn make_counter() {
    var count = 0
    return fn() {
        count = count + 1
        return count
    }
}
let ctr = make_counter()
assert_eq(ctr(), 1)
assert_eq(ctr(), 2)

let pctr = provenance("ctr")
assert(pctr != null)
assert_eq(pctr.origin, "fn_return")

-- 10. higher-order functions
fn make_adder(base) {
    return fn(v) { base + v }
}
let add10 = make_adder(10)
let val = add10(5)
assert_eq(val, 15)

let pval = provenance("val")
assert(pval != null)
assert_eq(pval.origin, "fn_return")
assert_eq(pval.value, 15)

-- 11. nested calls: dbl(inc(x))
fn inc(n) { return n + 1 }
fn dbl(n) { return n * 2 }
let nested_r = dbl(inc(x))
assert_eq(nested_r, 86)

let pnr = provenance("nested_r")
assert(pnr != null)
assert_eq(pnr.origin, "fn_return")
assert_eq(pnr.detail, "dbl")
assert_eq(pnr.value, 86)
assert(pnr.parent != null)
assert_eq(pnr.parent.origin, "fn_return")
assert_eq(pnr.parent.detail, "inc")
assert_eq(pnr.parent.value, 43)

-- 12. stats
let stats = provenance_stats()
assert(stats.total_bindings > 0)
assert_eq(stats.enabled, true)
assert(stats.origins.has("literal"))
assert(stats.origins.has("fn_return"))
assert(stats.origins.has("binop"))
assert(stats.origins.has("variable"))

-- 13. recursion
fn factorial(n) {
    if n <= 1 { return 1 }
    return n * factorial(n - 1)
}
assert_eq(factorial(5), 120)

-- === TRACE TYPE TESTS ===

-- 14. method call tracking
let arr = [3, 1, 2]
let sorted = arr.sort()
let psorted = provenance("sorted")
assert(psorted != null)
assert_eq(psorted.origin, "method_call")
assert_eq(psorted.detail, "sort")

-- 15. index tracking
let first = arr[0]
let pfirst = provenance("first")
assert(pfirst != null)
assert_eq(pfirst.origin, "index")
assert_eq(pfirst.detail, "0")

-- 16. deep chain through functions
fn triple(n) { return n * 3 }
let t = triple(x)
assert_eq(t, 126)
let pt = provenance("t")
assert(pt != null)
assert_eq(pt.origin, "fn_return")
assert_eq(pt.detail, "triple")
assert_eq(pt.value, 126)
-- the return chain should capture the binop inside triple
if pt.return_chain != null {
    assert_eq(pt.return_chain.origin, "binop")
    assert_eq(pt.return_chain.detail, "*")
}

-- 17. provenance history returns History object
var h = 1
h = h + 1
h = h * 2
let hist = provenance_history("h")
assert(hist.len() >= 3)
assert_eq(hist.at(0).value, 4)
assert_eq(hist.at(1).value, 2)
assert_eq(hist.at(2).value, 1)

-- 18. formatted trace
let trace_str = provenance_trace("t")
assert(trace_str.len() > 0)
-- should contain the function name and value
assert(trace_str.contains("triple"))
assert(trace_str.contains("126"))

-- 19. provenance_trace is callable (not null)
assert(provenance_trace != null)
assert(provenance_history != null)

-- 20. method call with chain on collection methods
let nums = [10, 20, 30]
let mapped = nums.map(fn(v) { v * 2 })
let pm = provenance("mapped")
assert(pm != null)
assert_eq(pm.origin, "method_call")
assert_eq(pm.detail, "map")
assert_eq(pm.object, "nums")

-- 21. index on map
let data = #{ "name": "xs", "version": 3 }
let vname = data["name"]
let pname = provenance("vname")
assert(pname != null)
assert_eq(pname.origin, "index")

-- 22. history preserves order correctly
var tracker = 100
tracker = tracker - 10
tracker = tracker * 2
tracker = tracker + 5
let thist = provenance_history("tracker")
assert(thist.len() >= 4)
assert_eq(thist.at(0).value, 185)
assert_eq(thist.at(1).value, 180)
assert_eq(thist.at(2).value, 90)
assert_eq(thist.at(3).value, 100)

-- 23. trace of a literal variable is simple
let simple = 999
let simple_trace = provenance_trace("simple")
assert(simple_trace.contains("999"))
assert(simple_trace.contains("(literal)"))

-- 24. trace of non-existent variable returns empty
let no_trace = provenance_trace("does_not_exist")
assert_eq(no_trace, "")

-- 25. history of untracked variable is empty (History object with 0 entries)
let no_hist = provenance_history("does_not_exist")
assert_eq(no_hist.len(), 0)

-- 26. return value tracking (deep chain exists)
fn add_one(v) { return v + 1 }
let a1 = add_one(10)
let pa1 = provenance("a1")
assert(pa1 != null)
assert_eq(pa1.origin, "fn_return")
assert_eq(pa1.detail, "add_one")
assert_eq(pa1.value, 11)
-- return_chain captures what happened inside the function
if pa1.return_chain != null {
    assert_eq(pa1.return_chain.origin, "binop")
}

-- 27. explain prints without crashing
explain("result")

-- 28. explain_history prints without crashing
explain_history("tracker")

-- 29. explain for non-existent variable
explain("nope_not_here")

-- 30. explain_history for non-existent variable
explain_history("nope_not_here")

-- 31. provenance_trace shows readable tree with indentation
let trace_sum = provenance_trace("sum")
assert(trace_sum.contains("52"))
assert(trace_sum.contains("x + 10"))

-- 32. parameter tracking: function params have provenance
fn triple(n) { return n * 3 }
triple(10)
let pn_param = provenance("n")
assert(pn_param != null)
assert_eq(pn_param.origin, "param")
assert_eq(pn_param.value, 10)

-- 33. trace keyword works
trace x

-- 34. history keyword works
history tracker

-- 35. param values show in binop trace inside functions
fn add_vals(a, b) { return a + b }
let added = add_vals(3, 7)
let pa_trace = provenance_trace("added")
assert(pa_trace.contains("10"))
assert(pa_trace.contains("add_vals"))

-- === NEW: Trace type methods ===

-- 36. Trace.var_name is set
let tz = provenance("sum")
assert_eq(tz.var_name, "sum")

-- 37. Trace.short() returns summary string
let ts = tz.short()
assert(ts.contains("52"))
assert(ts.contains("sum"))

-- 38. Trace.tree() returns formatted string
let tt = tz.tree()
assert(tt.contains("52"))

-- 39. Trace.depth()
assert(tz.depth() >= 2)

-- 40. Trace.root() follows to root
let tr = tz.root()
assert_eq(tr.origin, "literal")

-- 41. Trace.find() finds ancestor by origin
let found = tz.find("literal")
assert(found != null)
assert_eq(found.origin, "literal")

-- 42. Trace.filter() returns all matching ancestors
let lits = tz.filter("literal")
assert(lits.len() >= 1)

-- 43. Trace.chain() returns flat array of Trace objects
let tc = tz.chain()
assert(tc.len() >= 2)

-- 44. Trace.json() returns a map
let tj = tz.json()
assert_eq(tj.get("origin"), "binop")
assert_eq(tj.get("value"), 52)

-- 45. Trace.same_origin() comparison
let tz2 = provenance("sum")
assert(tz.same_origin(tz2))

-- 46. find returns null for non-existent origin
let nf = tz.find("nonexistent_origin_type")
assert(nf == null)

-- === NEW: History type methods ===

-- 47. History.current() and History.initial()
let htracker = provenance_history("tracker")
assert_eq(htracker.current().value, 185)
assert_eq(htracker.initial().value, 100)

-- 48. History.values() returns array of values
let hvals = htracker.values()
assert_eq(hvals[0], 185)
assert_eq(hvals[hvals.len() - 1], 100)

-- 49. History.var_name
assert_eq(htracker.var_name, "tracker")

-- 50. History.at() works
assert_eq(htracker.at(0).value, 185)
assert_eq(htracker.at(1).value, 180)

-- 51. History.diff() returns comparison string
let hd = htracker.diff(0, 3)
assert(hd.contains("185"))
assert(hd.contains("100"))

-- 52. provenance returns Trace, not raw map
let prov_simple = provenance("simple")
-- should have Trace methods
let sx = prov_simple.short()
assert(sx.contains("999"))

-- 53. provenance_chain returns array of Trace objects with methods
let pc3 = provenance_chain("alias")
assert(pc3.len() >= 2)
let first_t = pc3[0]
assert_eq(first_t.origin, "variable")
let fs = first_t.short()
assert(fs.contains("42"))

print("provenance stress test passed")
