-- test http module (client-side only, no actual server in CI)

-- http module exists
assert(http != null, "http module should exist")

-- verify methods exist
assert(http.get != null, "http.get should exist")
assert(http.post != null, "http.post should exist")
assert(http.put != null, "http.put should exist")
assert(http.delete != null, "http.delete should exist")
assert(http.patch != null, "http.patch should exist")
assert(http.request != null, "http.request should exist")

print("test_http_server: all passed")
