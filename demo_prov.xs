load "tests/plugins/provenance.xs"

print("=== Trace Type Demo ===")
print("")

let base = 42
let offset = 10
let sum = base + offset
fn double(n) { return n * 2 }
let result = double(sum)

-- get a Trace object
let tr = provenance("result")

print("-- basic properties --")
print("value:    " + str(tr.value))
print("origin:   " + tr.origin)
print("detail:   " + tr.detail)
print("var_name: " + tr.var_name)
print("line:     " + str(tr.line))
print("")

print("-- return chain (what happened inside the function) --")
if tr.return_chain != null {
    print("return_chain.origin: " + tr.return_chain.origin)
    print("return_chain.detail: " + tr.return_chain.detail)
    print("return_chain.value:  " + str(tr.return_chain.value))
}
print("")

print("-- short summary --")
print(tr.short())
print("")

print("-- tree view --")
tr.show()
print("")

-- demo with binop to show full chain
print("=== Binop chain Demo ===")
print("")
let btr = provenance("sum")
print("-- chain --")
let chain = btr.chain()
print("chain depth: " + str(chain.len()))
var ci = 0
while ci < chain.len() {
    let cc = chain[ci]
    print("  [" + str(ci) + "] " + cc.origin + " = " + str(cc.value))
    ci = ci + 1
}
print("")

print("-- depth: " + str(btr.depth()))
print("-- root: " + btr.root().origin + " = " + str(btr.root().value))
print("")

let found = btr.find("literal")
if found != null {
    print("found literal: " + str(found.value))
}

let all_lits = btr.filter("literal")
print("all literals in chain: " + str(all_lits.len()))
print("")

print("-- tree view --")
btr.show()
print("")

print("-- json export --")
let jj = btr.json()
print("json: " + str(jj))
print("")

print("=== History Type Demo ===")
print("")

var counter = 0
counter = counter + 1
counter = counter * 3
counter = counter + 7

let hh = provenance_history("counter")
print("history length: " + str(hh.len()))
print("current: " + str(hh.current().value))
print("initial: " + str(hh.initial().value))
print("")

print("-- values --")
let vals = hh.values()
print("all values: " + str(vals))
print("")

print("-- formatted history --")
hh.show()
print("")

print("-- diff --")
print(hh.diff(0, 3))
print("")

print("=== trace/history keywords ===")
trace sum
print("")
history counter
