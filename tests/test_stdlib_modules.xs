-- JSON round-trip via map literals
import json

let m = #{"name": "xs", "version": 3, "active": true}
let s = json.stringify(m)
let obj = json.parse(s)
assert_eq(obj["name"], "xs")
assert_eq(obj["version"], 3)
assert_eq(obj["active"], true)

-- stringify with indent
let pretty = json.stringify(m, 2)
assert(pretty.contains("\n"))

-- json arrays
let arr = json.parse("[1, 2.5, null, false]")
assert_eq(arr[0], 1)
assert_eq(arr[2], null)
assert_eq(arr[3], false)

-- nested via stringify round-trip
let nested = #{"tags": ["fast", "fun"], "meta": #{"count": 42}}
let ns = json.stringify(nested)
let np = json.parse(ns)
assert_eq(np["tags"][0], "fast")
assert_eq(np["tags"][1], "fun")
assert_eq(np["meta"]["count"], 42)

-- json stringify primitives
assert_eq(json.stringify(42), "42")
assert_eq(json.stringify(true), "true")
assert_eq(json.stringify(null), "null")

-- json.valid
assert_eq(json.valid("[1,2]"), true)
assert_eq(json.valid("not json"), false)

-- CSV
import csv
let data = csv.parse("a,b,c\n1,2,3\n4,5,6")
assert_eq(data.len(), 3)
assert_eq(data[0][0], "a")
assert_eq(data[1][1], "2")
assert_eq(data[2][2], "6")

-- csv stringify
let csv_str = csv.stringify([["x", "y"], ["1", "2"]])
assert(csv_str.contains("x"))
assert(csv_str.contains("1"))

-- csv with custom delimiter (tab)
let tsv = csv.parse("a\tb\tc\n1\t2\t3", "\t")
assert_eq(tsv[0][1], "b")
assert_eq(tsv[1][2], "3")

-- csv stringify with custom delimiter
let tsv_out = csv.stringify([["a", "b"], ["1", "2"]], "\t")
assert(tsv_out.contains("a\tb"))

-- csv quoted fields (build via concat)
let dq = "\""
let csv_q = "name,val\n" + dq + "a,b" + dq + ",ok"
let qdata = csv.parse(csv_q)
assert_eq(qdata[1][0], "a,b")
assert_eq(qdata[1][1], "ok")

-- TOML
import toml
let toml_in = "[package]\nname = " + dq + "myapp" + dq + "\nversion = " + dq + "1.0" + dq + "\n\n[deps]\nfoo = " + dq + "0.1" + dq
let config = toml.parse(toml_in)
assert_eq(config["package"]["name"], "myapp")
assert_eq(config["deps"]["foo"], "0.1")

-- toml types
let t2_in = "num = 42\npi = 3.14\nflag = true"
let t2 = toml.parse(t2_in)
assert_eq(t2["num"], 42)
assert_eq(t2["flag"], true)

-- toml arrays
let t_arr = "tags = [" + dq + "a" + dq + ", " + dq + "b" + dq + "]"
let t3 = toml.parse(t_arr)
assert_eq(t3["tags"][0], "a")
assert_eq(t3["tags"][1], "b")

-- toml dotted sections
let t4_in = "[a.b]\nval = " + dq + "deep" + dq
let t4 = toml.parse(t4_in)
assert_eq(t4["a"]["b"]["val"], "deep")

-- toml comments
let t5_in = "# comment\nkey = " + dq + "val" + dq + " # inline"
let t5 = toml.parse(t5_in)
assert_eq(t5["key"], "val")

-- test module
import test
test.assert(true, "should pass")
test.assert_eq(1, 1, "one is one")
test.assert_ne(1, 2, "one is not two")

-- http module exists
import http
assert_eq(type(http), "module")

print("stdlib modules tests passed")
