-- provenance tracking plugin
-- tracks runtime values through assignments, function calls, collections, and errors

-- dedup guard: skip re-registration if already loaded
if plugin.runtime.global.get("__prov_reg") == null {
plugin.runtime.global.set("__prov_reg", #{})
plugin.runtime.global.set("__prov_history", #{})
plugin.runtime.global.set("__prov_last_call", null)
plugin.runtime.global.set("__prov_last_binop", null)
plugin.runtime.global.set("__prov_last_method", null)
plugin.runtime.global.set("__prov_last_index", null)
plugin.runtime.global.set("__prov_last_return", null)
plugin.runtime.global.set("__prov_arg_stack", [])

-- Trace class: wraps a provenance entry with navigation and display methods
class Trace {
    origin = ""
    detail = ""
    value = null
    line = 0
    var_name = ""
    object = ""
    parent = null
    parent2 = null
    return_chain = null

    fn init(self) {}

    fn short(self) {
        var from_part = self.origin
        if self.detail != "" and self.detail != null {
            from_part = str(self.detail)
            if self.origin == "fn_return" {
                from_part = from_part + "()"
            }
        }
        return str(self.var_name) + " = " + str(self.value) + " from " + from_part + " at line " + str(self.line)
    }

    fn show(self) {
        print(self.tree())
    }

    fn tree(self) {
        let trace_fn = plugin.runtime.global.get("__prov_trace_entry_obj")
        let line_str_fn = plugin.runtime.global.get("__prov_line_str_obj")
        let describe_fn = plugin.runtime.global.get("__prov_describe_obj")
        var out = str(self.var_name) + " = " + str(self.value) + line_str_fn(self) + "\n"
        if self.origin == "fn_return" {
            let indent_fn = plugin.runtime.global.get("__prov_indent")
            out = out + indent_fn(1) + "from " + describe_fn(self) + line_str_fn(self) + "\n"
            if self.return_chain != null {
                out = out + trace_fn(self.return_chain, 2, 16)
            }
            if self.parent != null {
                out = out + trace_fn(self.parent, 2, 16)
            }
        } else {
            out = out + trace_fn(self, 1, 16)
        }
        -- trim trailing newline
        if out.len() > 0 and out[out.len() - 1] == "\n" {
            out = out.slice(0, out.len() - 1)
        }
        return out
    }

    fn chain(self) {
        let results = []
        var cur = self
        var lim = 0
        while cur != null and lim < 32 {
            results.push(cur)
            if cur.parent != null {
                cur = cur.parent
            } else {
                cur = null
            }
            lim = lim + 1
        }
        return results
    }

    fn depth(self) {
        var d = 0
        var cur = self
        while cur != null {
            d = d + 1
            if cur.parent != null {
                cur = cur.parent
            } else {
                cur = null
            }
        }
        return d
    }

    fn root(self) {
        var cur = self
        var lim = 0
        while cur.parent != null and lim < 32 {
            cur = cur.parent
            lim = lim + 1
        }
        return cur
    }

    fn find(self, origin_type) {
        var cur = self
        var lim = 0
        while cur != null and lim < 32 {
            if cur.origin == origin_type { return cur }
            if cur.parent != null {
                cur = cur.parent
            } else {
                cur = null
            }
            lim = lim + 1
        }
        return null
    }

    fn filter(self, origin_type) {
        let results = []
        var cur = self
        var lim = 0
        while cur != null and lim < 32 {
            if cur.origin == origin_type { results.push(cur) }
            if cur.parent != null {
                cur = cur.parent
            } else {
                cur = null
            }
            lim = lim + 1
        }
        return results
    }

    fn json(self) {
        let m = #{}
        m.set("origin", self.origin)
        m.set("detail", self.detail)
        m.set("value", self.value)
        m.set("line", self.line)
        m.set("var_name", self.var_name)
        return m
    }

    fn same_origin(self, other) {
        if other == null { return false }
        return self.origin == other.origin and self.detail == other.detail
    }
}

-- History class: wraps provenance history for a variable
class History {
    entries = []
    var_name = ""

    fn init(self, ents, name) {
        self.entries = ents
        self.var_name = name
    }

    fn len(self) { return self.entries.len() }

    fn current(self) {
        if self.entries.len() == 0 { return null }
        return self.entries[0]
    }

    fn initial(self) {
        if self.entries.len() == 0 { return null }
        return self.entries[self.entries.len() - 1]
    }

    fn at(self, i) { return self.entries[i] }

    fn values(self) {
        let result = []
        var i = 0
        while i < self.entries.len() {
            result.push(self.entries[i].value)
            i = i + 1
        }
        return result
    }

    fn show(self) {
        print(self.format())
    }

    fn format(self) {
        let describe_fn = plugin.runtime.global.get("__prov_describe_obj")
        let line_str_fn = plugin.runtime.global.get("__prov_line_str_obj")
        var out = str(self.var_name) + " history:\n"
        var i = 0
        while i < self.entries.len() {
            let e = self.entries[i]
            var label = "  [prev]    "
            if i == 0 {
                label = "  [current] "
            } elif i == self.entries.len() - 1 {
                label = "  [initial] "
            }
            out = out + label + str(e.value) + ", " + describe_fn(e) + line_str_fn(e) + "\n"
            i = i + 1
        }
        -- trim trailing newline
        if out.len() > 0 and out[out.len() - 1] == "\n" {
            out = out.slice(0, out.len() - 1)
        }
        return out
    }

    fn diff(self, a, b) {
        if a >= self.entries.len() or b >= self.entries.len() { return "index out of range" }
        let ea = self.entries[a]
        let eb = self.entries[b]
        return str(self.var_name) + "[" + str(a) + "] = " + str(ea.value) + " (" + ea.origin + ") vs [" + str(b) + "] = " + str(eb.value) + " (" + eb.origin + ")"
    }
}

-- store classes in globals so they're accessible from closures
plugin.runtime.global.set("__prov_Trace", Trace)
plugin.runtime.global.set("__prov_History", History)

-- wrap a raw entry map into a Trace object, recursively wrapping parents
plugin.runtime.global.set("__prov_wrap", fn(entry, name) {
    if entry == null { return null }
    let TraceCls = plugin.runtime.global.get("__prov_Trace")
    let wrap_fn = plugin.runtime.global.get("__prov_wrap")
    let __w = TraceCls()
    __w.var_name = name
    if entry.has("origin") { __w.origin = entry.get("origin") }
    if entry.has("value") { __w.value = entry.get("value") }
    if entry.has("detail") and entry.get("detail") != null { __w.detail = str(entry.get("detail")) }
    if entry.has("line") and entry.get("line") != null { __w.line = entry.get("line") }
    if entry.has("object") and entry.get("object") != null { __w.object = str(entry.get("object")) }
    if entry.has("parent") and entry.get("parent") != null {
        __w.parent = wrap_fn(entry.get("parent"), "")
    }
    if entry.has("parent2") and entry.get("parent2") != null {
        __w.parent2 = wrap_fn(entry.get("parent2"), "")
    }
    if entry.has("return_chain") and entry.get("return_chain") != null {
        __w.return_chain = wrap_fn(entry.get("return_chain"), "")
    }
    return __w
})

-- push to history for a variable name
plugin.runtime.global.set("__prov_push_history", fn(name, entry) {
    let hist_reg = plugin.runtime.global.get("__prov_history")
    var arr = []
    if hist_reg.has(name) {
        arr = hist_reg.get(name)
    }
    arr.push(entry)
    hist_reg.set(name, arr)
    plugin.runtime.global.set("__prov_history", hist_reg)
})

-- after a function call completes, stash the return value
plugin.runtime.after_eval("call", fn(node, result) {
    let callee = node.get("callee")
    var fn_name = "<anon>"
    if callee != null {
        let ck = callee.get("tag")
        if ck == "ident" {
            fn_name = callee.get("name")
        }
    }
    let args = node.get("args")
    var parent = null
    if args != null and args.len() > 0 {
        let first = args[0]
        if first != null {
            let ftag = first.get("tag")
            if ftag == "call" {
                parent = plugin.runtime.global.get("__prov_last_call")
            }
        }
    }

    let ret_chain = plugin.runtime.global.get("__prov_last_return")

    let entry = #{}
    entry.set("origin", "fn_return")
    entry.set("detail", fn_name)
    entry.set("value", result)
    entry.set("line", node.get("line"))
    if parent != null {
        entry.set("parent", parent)
    }
    if ret_chain != null {
        entry.set("return_chain", ret_chain)
        plugin.runtime.global.set("__prov_last_return", null)
    }
    plugin.runtime.global.set("__prov_last_call", entry)
    true
})

-- after a return statement fires, stash the provenance of the return expression
plugin.runtime.after_eval("return", fn(node, result) {
    let val_node = node.get("value")
    if val_node == null { return true }
    let classify = plugin.runtime.global.get("__prov_classify")
    let line = node.get("line")
    if line == null { line = 0 }
    let entry = classify(val_node, result, line)
    plugin.runtime.global.set("__prov_last_return", entry)
    true
})

-- after a binop completes, stash the result
plugin.runtime.after_eval("binop", fn(node, result) {
    let entry = #{}
    entry.set("origin", "binop")
    entry.set("detail", node.get("op"))
    entry.set("value", result)
    entry.set("line", node.get("line"))
    let left = node.get("left")
    let right = node.get("right")
    if left != null {
        let lt = left.get("tag")
        if lt == "ident" {
            let reg = plugin.runtime.global.get("__prov_reg")
            let lname = left.get("name")
            let lentry = #{}
            lentry.set("origin", "variable")
            lentry.set("detail", lname)
            if reg.has(lname) {
                let src_entry = reg.get(lname)
                lentry.set("value", src_entry.value)
                lentry.set("parent", src_entry)
            }
            entry.set("parent", lentry)
        } elif lt == "int" or lt == "float" or lt == "str" or lt == "bool" {
            let lentry = #{}
            lentry.set("origin", "literal")
            lentry.set("detail", str(left.get("value")))
            lentry.set("value", left.get("value"))
            entry.set("parent", lentry)
        }
    }
    if right != null {
        let rt = right.get("tag")
        if rt == "ident" {
            let reg = plugin.runtime.global.get("__prov_reg")
            let rname = right.get("name")
            let rentry = #{}
            rentry.set("origin", "variable")
            rentry.set("detail", rname)
            if reg.has(rname) {
                let src_entry = reg.get(rname)
                rentry.set("value", src_entry.value)
                rentry.set("parent", src_entry)
            }
            entry.set("parent2", rentry)
        } elif rt == "int" or rt == "float" or rt == "str" or rt == "bool" {
            let rentry = #{}
            rentry.set("origin", "literal")
            rentry.set("detail", str(right.get("value")))
            rentry.set("value", right.get("value"))
            entry.set("parent2", rentry)
        }
    }
    plugin.runtime.global.set("__prov_last_binop", entry)
    true
})

-- after method call, stash the result
plugin.runtime.after_eval("method_call", fn(node, result) {
    let method = node.get("method")
    let obj_node = node.get("obj")
    var obj_name = "<expr>"
    if obj_node != null {
        let ot = obj_node.get("tag")
        if ot == "ident" {
            obj_name = obj_node.get("name")
        }
    }
    let entry = #{}
    entry.set("origin", "method_call")
    entry.set("detail", method)
    entry.set("value", result)
    entry.set("line", node.get("line"))
    entry.set("object", obj_name)
    if obj_name != "<expr>" {
        let reg = plugin.runtime.global.get("__prov_reg")
        if reg.has(obj_name) {
            entry.set("parent", reg.get(obj_name))
        }
    }
    plugin.runtime.global.set("__prov_last_method", entry)
    true
})

-- after index access, stash the result
plugin.runtime.after_eval("index", fn(node, result) {
    let obj_node = node.get("obj")
    let idx_node = node.get("index")
    var obj_name = "<expr>"
    if obj_node != null {
        let ot = obj_node.get("tag")
        if ot == "ident" {
            obj_name = obj_node.get("name")
        }
    }
    var idx_detail = "?"
    if idx_node != null {
        let it = idx_node.get("tag")
        if it == "int" or it == "float" or it == "str" {
            idx_detail = str(idx_node.get("value"))
        } elif it == "ident" {
            idx_detail = idx_node.get("name")
        }
    }
    let entry = #{}
    entry.set("origin", "index")
    entry.set("detail", idx_detail)
    entry.set("value", result)
    entry.set("line", node.get("line"))
    entry.set("object", obj_name)
    if obj_name != "<expr>" {
        let reg = plugin.runtime.global.get("__prov_reg")
        if reg.has(obj_name) {
            entry.set("parent", reg.get(obj_name))
        }
    }
    plugin.runtime.global.set("__prov_last_index", entry)
    true
})

-- classify an init expression and pick up the stashed after-eval result
plugin.runtime.global.set("__prov_classify", fn(init, result, line) {
    if init == null { return null }
    let kind = init.get("tag")
    let entry = #{}
    if kind == "int" or kind == "float" or kind == "str" or kind == "bool" or kind == "null" {
        entry.set("origin", "literal")
        entry.set("detail", str(result))
        entry.set("value", result)
        entry.set("line", line)
        return entry
    }
    if kind == "call" {
        let last = plugin.runtime.global.get("__prov_last_call")
        if last != null { return last }
        entry.set("origin", "fn_return")
        entry.set("value", result)
        entry.set("line", line)
        return entry
    }
    if kind == "binop" {
        let last = plugin.runtime.global.get("__prov_last_binop")
        if last != null { return last }
        entry.set("origin", "binop")
        entry.set("value", result)
        entry.set("line", line)
        return entry
    }
    if kind == "method_call" {
        let last = plugin.runtime.global.get("__prov_last_method")
        if last != null { return last }
        entry.set("origin", "method_call")
        entry.set("value", result)
        entry.set("line", line)
        return entry
    }
    if kind == "index" {
        let last = plugin.runtime.global.get("__prov_last_index")
        if last != null { return last }
        entry.set("origin", "index")
        entry.set("value", result)
        entry.set("line", line)
        return entry
    }
    if kind == "ident" {
        let src = init.get("name")
        entry.set("origin", "variable")
        entry.set("detail", src)
        entry.set("value", result)
        entry.set("line", line)
        let reg = plugin.runtime.global.get("__prov_reg")
        if reg.has(src) { entry.set("parent", reg.get(src)) }
        return entry
    }
    if kind == "unary" {
        let op = init.get("op")
        entry.set("origin", "unary")
        entry.set("detail", op)
        entry.set("value", result)
        entry.set("line", line)
        return entry
    }
    if kind == "lambda" {
        entry.set("origin", "lambda")
        entry.set("detail", "")
        entry.set("value", result)
        entry.set("line", line)
        return entry
    }
    if kind == "array" {
        entry.set("origin", "array")
        entry.set("detail", "")
        entry.set("value", result)
        entry.set("line", line)
        return entry
    }
    if kind == "map" {
        entry.set("origin", "map")
        entry.set("detail", "")
        entry.set("value", result)
        entry.set("line", line)
        return entry
    }
    entry.set("origin", kind)
    entry.set("detail", "")
    entry.set("value", result)
    entry.set("line", line)
    return entry
})

-- helper to register a provenance entry and push history
plugin.runtime.global.set("__prov_register", fn(name, entry) {
    if entry == null { return null }
    let reg = plugin.runtime.global.get("__prov_reg")
    let push_hist = plugin.runtime.global.get("__prov_push_history")
    if reg.has(name) {
        push_hist(name, reg.get(name))
    }
    reg.set(name, entry)
    plugin.runtime.global.set("__prov_reg", reg)
})

-- serialize a provenance entry (raw map) to a JSON string for the tracer
plugin.runtime.global.set("__prov_serialize", fn(entry) {
    if entry == null { return "\{\}" }
    let self_fn = plugin.runtime.global.get("__prov_serialize")
    let q = chr(34)
    var out = "\{"
    var sep = ""
    if entry.has("origin") and entry.origin != null {
        out = out + sep + q + "origin" + q + ":" + q + str(entry.origin) + q
        sep = ","
    }
    if entry.has("detail") and entry.detail != null {
        out = out + sep + q + "detail" + q + ":" + q + str(entry.detail) + q
        sep = ","
    }
    if entry.has("value") and entry.value != null {
        let v = entry.value
        if type(v) == "str" {
            out = out + sep + q + "value" + q + ":" + q + str(v) + q
        } else {
            out = out + sep + q + "value" + q + ":" + str(v)
        }
        sep = ","
    }
    if entry.has("line") and entry.line != null and entry.line > 0 {
        out = out + sep + q + "line" + q + ":" + str(entry.line)
        sep = ","
    }
    if entry.has("parent") and entry.parent != null {
        out = out + sep + q + "parent" + q + ":" + self_fn(entry.parent)
        sep = ","
    }
    if entry.has("parent2") and entry.parent2 != null {
        out = out + sep + q + "parent2" + q + ":" + self_fn(entry.parent2)
        sep = ","
    }
    if entry.has("return_chain") and entry.return_chain != null {
        out = out + sep + q + "return_chain" + q + ":" + self_fn(entry.return_chain)
        sep = ","
    }
    out = out + "\}"
    return out
})

-- write rich provenance to tracer if active
plugin.runtime.global.set("__prov_write_trace", fn(name, entry) {
    if !__tracer_active() { return null }
    let serialize = plugin.runtime.global.get("__prov_serialize")
    let json = serialize(entry)
    __tracer_write_prov(name, json)
})

-- after let: record name -> provenance with actual value
plugin.runtime.after_eval("let", fn(node, result) {
    let name = node.get("name")
    if name == null { return true }
    let init = node.get("value")
    var line = node.get("line")
    if line == null { line = 0 }
    let classify = plugin.runtime.global.get("__prov_classify")
    var entry = classify(init, result, line)
    if entry == null {
        entry = #{}
        entry.set("origin", "param")
        entry.set("detail", name)
        entry.set("value", result)
        entry.set("line", line)
    }
    let register = plugin.runtime.global.get("__prov_register")
    register(name, entry)
    let write_trace = plugin.runtime.global.get("__prov_write_trace")
    write_trace(name, entry)
    true
})

-- after var: same as let
plugin.runtime.after_eval("var", fn(node, result) {
    let name = node.get("name")
    if name == null { return true }
    let init = node.get("value")
    let line = node.get("line")
    if line == null { line = 0 }
    let classify = plugin.runtime.global.get("__prov_classify")
    let entry = classify(init, result, line)
    let register = plugin.runtime.global.get("__prov_register")
    register(name, entry)
    let write_trace = plugin.runtime.global.get("__prov_write_trace")
    write_trace(name, entry)
    true
})

-- after assign: update provenance with new value
plugin.runtime.after_eval("assign", fn(node, result) {
    let target = node.get("target")
    if target == null { return true }
    let tname = target.get("name")
    if tname == null { return true }
    let init = node.get("value")
    let line = node.get("line")
    if line == null { line = 0 }
    let classify = plugin.runtime.global.get("__prov_classify")
    let entry = classify(init, result, line)
    let register = plugin.runtime.global.get("__prov_register")
    register(tname, entry)
    let write_trace = plugin.runtime.global.get("__prov_write_trace")
    write_trace(tname, entry)
    true
})

-- public API: query provenance for a variable, returns Trace object
plugin.runtime.global.set("provenance", fn(name) {
    let reg = plugin.runtime.global.get("__prov_reg")
    if reg.has(name) {
        let wrap = plugin.runtime.global.get("__prov_wrap")
        return wrap(reg.get(name), name)
    }
    return null
})

-- public API: get the full parent chain as flat array of Trace objects
plugin.runtime.global.set("provenance_chain", fn(name) {
    let reg = plugin.runtime.global.get("__prov_reg")
    if !reg.has(name) { return [] }
    let wrap = plugin.runtime.global.get("__prov_wrap")
    let __w = wrap(reg.get(name), name)
    return __w.chain()
})

-- public API: get tracking stats
plugin.runtime.global.set("provenance_stats", fn() {
    let reg = plugin.runtime.global.get("__prov_reg")
    let keys = reg.keys()
    let origins = #{}
    var count = 0
    for k in keys {
        if k.len() >= 7 and k.starts_with("__prov_") { continue }
        let e = reg.get(k)
        let o = e.origin
        if origins.has(o) {
            origins.set(o, origins.get(o) + 1)
        } else {
            origins.set(o, 1)
        }
        count = count + 1
    }
    let result = #{}
    result.set("total_bindings", count)
    result.set("enabled", true)
    result.set("origins", origins)
    return result
})

-- public API: get provenance history for a variable, returns History object
plugin.runtime.global.set("provenance_history", fn(name) {
    let reg = plugin.runtime.global.get("__prov_reg")
    let hist_reg = plugin.runtime.global.get("__prov_history")
    let wrap = plugin.runtime.global.get("__prov_wrap")
    let HistoryCls = plugin.runtime.global.get("__prov_History")
    let entries = []
    if reg.has(name) {
        entries.push(wrap(reg.get(name), name))
    }
    if hist_reg.has(name) {
        let arr = hist_reg.get(name)
        var i = arr.len() - 1
        while i >= 0 {
            entries.push(wrap(arr[i], name))
            i = i - 1
        }
    }
    return HistoryCls(entries, name)
})

-- indent helper
plugin.runtime.global.set("__prov_indent", fn(depth) {
    var s = ""
    var i = 0
    while i < depth {
        s = s + "  "
        i = i + 1
    }
    return s
})

-- describe a Trace object in human-readable form
plugin.runtime.global.set("__prov_describe_obj", fn(t) {
    if t == null { return "unknown" }
    let origin = t.origin
    if origin == "literal" {
        return str(t.value) + " (literal)"
    }
    if origin == "param" {
        return str(t.value) + " (parameter)"
    }
    if origin == "fn_return" {
        var detail = ""
        if t.detail != null and t.detail != "" {
            detail = str(t.detail)
        }
        return detail + "() returned " + str(t.value)
    }
    if origin == "binop" {
        var op = "?"
        if t.detail != null and t.detail != "" {
            op = str(t.detail)
        }
        var lhs = "?"
        var rhs = "?"
        if t.parent != null {
            let p = t.parent
            if p.origin == "variable" or p.origin == "literal" or p.origin == "param" {
                lhs = str(p.detail)
            } else {
                lhs = str(p.value)
            }
        }
        if t.parent2 != null {
            let p2 = t.parent2
            if p2.origin == "variable" or p2.origin == "literal" or p2.origin == "param" {
                rhs = str(p2.detail)
            } else {
                rhs = str(p2.value)
            }
        }
        return lhs + " " + op + " " + rhs + " = " + str(t.value)
    }
    if origin == "variable" {
        var vname = "?"
        if t.detail != null and t.detail != "" {
            vname = str(t.detail)
        }
        if t.value != null {
            return vname + " = " + str(t.value)
        }
        return vname
    }
    if origin == "method_call" {
        var mname = "?"
        if t.detail != null and t.detail != "" {
            mname = str(t.detail)
        }
        return mname + "() = " + str(t.value)
    }
    if origin == "index" {
        var idx = "?"
        if t.detail != null and t.detail != "" {
            idx = str(t.detail)
        }
        return "[" + idx + "] = " + str(t.value)
    }
    if origin == "unary" {
        var op = ""
        if t.detail != null and t.detail != "" {
            op = str(t.detail)
        }
        return op + str(t.value)
    }
    return origin + " = " + str(t.value)
})

-- describe a raw map entry (for internal use where we still have maps)
plugin.runtime.global.set("__prov_describe", fn(entry) {
    if entry == null { return "unknown" }
    let origin = entry.origin
    if origin == "literal" {
        return str(entry.value) + " (literal)"
    }
    if origin == "param" {
        return str(entry.value) + " (parameter)"
    }
    if origin == "fn_return" {
        var detail = ""
        if entry.has("detail") and entry.detail != null {
            detail = str(entry.detail)
        }
        return detail + "() returned " + str(entry.value)
    }
    if origin == "binop" {
        var op = "?"
        if entry.has("detail") and entry.detail != null {
            op = str(entry.detail)
        }
        var lhs = "?"
        var rhs = "?"
        if entry.has("parent") and entry.parent != null {
            let p = entry.parent
            if p.origin == "variable" {
                lhs = str(p.detail)
            } elif p.origin == "literal" {
                lhs = str(p.detail)
            } elif p.origin == "param" {
                lhs = str(p.detail)
            } else {
                lhs = str(p.value)
            }
        }
        if entry.has("parent2") and entry.parent2 != null {
            let p2 = entry.parent2
            if p2.origin == "variable" {
                rhs = str(p2.detail)
            } elif p2.origin == "literal" {
                rhs = str(p2.detail)
            } elif p2.origin == "param" {
                rhs = str(p2.detail)
            } else {
                rhs = str(p2.value)
            }
        }
        return lhs + " " + op + " " + rhs + " = " + str(entry.value)
    }
    if origin == "variable" {
        var vname = "?"
        if entry.has("detail") and entry.detail != null {
            vname = str(entry.detail)
        }
        if entry.has("value") and entry.value != null {
            return vname + " = " + str(entry.value)
        }
        return vname
    }
    if origin == "method_call" {
        var mname = "?"
        if entry.has("detail") and entry.detail != null {
            mname = str(entry.detail)
        }
        var obj = ""
        if entry.has("object") and entry.object != null {
            obj = str(entry.object) + "."
        }
        return obj + mname + "() = " + str(entry.value)
    }
    if origin == "index" {
        var idx = "?"
        if entry.has("detail") and entry.detail != null {
            idx = str(entry.detail)
        }
        var obj = ""
        if entry.has("object") and entry.object != null {
            obj = str(entry.object)
        }
        return obj + "[" + idx + "] = " + str(entry.value)
    }
    if origin == "unary" {
        var op = ""
        if entry.has("detail") and entry.detail != null {
            op = str(entry.detail)
        }
        return op + str(entry.value)
    }
    return origin + " = " + str(entry.value)
})

-- line info helper for Trace objects
plugin.runtime.global.set("__prov_line_str_obj", fn(t) {
    if t == null { return "" }
    if t.line != null and t.line > 0 {
        return " (line " + str(t.line) + ")"
    }
    return ""
})

-- line info helper for raw maps
plugin.runtime.global.set("__prov_line_str", fn(entry) {
    if entry == null { return "" }
    if entry.has("line") and entry.line != null and entry.line > 0 {
        return " (line " + str(entry.line) + ")"
    }
    return ""
})

-- recursive trace builder for Trace objects
plugin.runtime.global.set("__prov_trace_entry_obj", fn(t, depth, max_depth) {
    if t == null or depth > max_depth { return "" }
    let indent_fn = plugin.runtime.global.get("__prov_indent")
    let describe = plugin.runtime.global.get("__prov_describe_obj")
    let line_str_fn = plugin.runtime.global.get("__prov_line_str_obj")
    let self_fn = plugin.runtime.global.get("__prov_trace_entry_obj")
    let prefix = indent_fn(depth)
    var out = ""

    out = out + prefix + describe(t) + line_str_fn(t) + "\n"

    if t.return_chain != null {
        out = out + self_fn(t.return_chain, depth + 1, max_depth)
    }
    if t.parent != null {
        out = out + self_fn(t.parent, depth + 1, max_depth)
    }
    if t.parent2 != null {
        out = out + self_fn(t.parent2, depth + 1, max_depth)
    }

    return out
})

-- recursive trace builder for raw map entries (kept for backward compat)
plugin.runtime.global.set("__prov_trace_entry", fn(entry, depth, max_depth) {
    if entry == null or depth > max_depth { return "" }
    let indent_fn = plugin.runtime.global.get("__prov_indent")
    let describe = plugin.runtime.global.get("__prov_describe")
    let line_str_fn = plugin.runtime.global.get("__prov_line_str")
    let self_fn = plugin.runtime.global.get("__prov_trace_entry")
    let prefix = indent_fn(depth)
    var out = ""

    out = out + prefix + describe(entry) + line_str_fn(entry) + "\n"

    if entry.has("return_chain") and entry.return_chain != null {
        let rc = entry.return_chain
        out = out + self_fn(rc, depth + 1, max_depth)
    }
    if entry.has("parent") and entry.parent != null {
        out = out + self_fn(entry.parent, depth + 1, max_depth)
    }
    if entry.has("parent2") and entry.parent2 != null {
        out = out + self_fn(entry.parent2, depth + 1, max_depth)
    }

    return out
})

-- public API: build formatted trace string for a variable (returns string)
plugin.runtime.global.set("provenance_trace", fn(name) {
    let reg = plugin.runtime.global.get("__prov_reg")
    if !reg.has(name) { return "" }
    let entry = reg.get(name)
    let line_str_fn = plugin.runtime.global.get("__prov_line_str")
    let trace_entry = plugin.runtime.global.get("__prov_trace_entry")
    let describe = plugin.runtime.global.get("__prov_describe")
    var out = name + " = " + str(entry.value) + line_str_fn(entry) + "\n"
    if entry.origin == "fn_return" {
        let indent_fn = plugin.runtime.global.get("__prov_indent")
        out = out + indent_fn(1) + "from " + describe(entry) + line_str_fn(entry) + "\n"
        if entry.has("return_chain") and entry.return_chain != null {
            out = out + trace_entry(entry.return_chain, 2, 16)
        }
        if entry.has("parent") and entry.parent != null {
            out = out + trace_entry(entry.parent, 2, 16)
        }
    } else {
        out = out + trace_entry(entry, 1, 16)
    }
    return out
})

-- public API: print a human-readable short explanation
-- trace keyword and explain() now use Trace.show()
plugin.runtime.global.set("explain", fn(name) {
    let reg = plugin.runtime.global.get("__prov_reg")
    if !reg.has(name) {
        print(name + ": not tracked")
        return null
    }
    let wrap = plugin.runtime.global.get("__prov_wrap")
    let __w = wrap(reg.get(name), name)
    __w.show()
    return null
})

-- public API: print readable history using History.show()
plugin.runtime.global.set("explain_history", fn(name) {
    let reg = plugin.runtime.global.get("__prov_reg")
    let hist_reg = plugin.runtime.global.get("__prov_history")
    if !reg.has(name) and !hist_reg.has(name) {
        print(name + ": no history")
        return null
    }
    let prov_hist = plugin.runtime.global.get("provenance_history")
    let __h = prov_hist(name)
    __h.show()
    return null
})

-- on throw, dump formatted provenance context
plugin.runtime.after_eval("throw", fn(node, result) {
    let reg = plugin.runtime.global.get("__prov_reg")
    let keys = reg.keys()
    if keys.len() > 0 {
        print("")
        print("  Provenance at throw:")
        var idx = 0
        while idx < keys.len() {
            let k = keys[idx]
            idx = idx + 1
            if k.len() >= 2 and k[0] == "_" and k[1] == "_" { continue }
            let e = reg.get(k)
            if e == null { continue }
            var line_info = ""
            if e.has("line") and e.line != null and e.line > 0 {
                line_info = " (line " + str(e.line) + ")"
            }
            print("    " + k + " = " + str(e.value) + line_info + "  [" + e.origin + "]")
        }
        print("")
    }
    true
})

-- error integration
plugin.runtime.on_error(fn(err, prev) {
    throw err
})

} -- end dedup guard

plugin "provenance" {
  meta {
    id: "provenance"
    version: "6.0.0"
    priority: 5
    provides: ["provenance_tracking"]
  }

  parser {
    production trace_stmt(parser, token) {
      -- trace IDENT -> explain(ident_name)
      let name = parser.ident()
      let callee = #{"tag": "ident", "name": "explain"}
      let arg = #{"tag": "str", "value": name}
      return #{"tag": "call", "callee": callee, "args": [arg]}
    }

    production history_stmt(parser, token) {
      -- history IDENT -> explain_history(ident_name)
      let name = parser.ident()
      let callee = #{"tag": "ident", "name": "explain_history"}
      let arg = #{"tag": "str", "value": name}
      return #{"tag": "call", "callee": callee, "args": [arg]}
    }
  }
}
