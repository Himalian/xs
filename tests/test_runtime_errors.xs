-- Expected-error tests. These exercise the compiler's and runtime's
-- error paths: the previous suite had nothing that failed on purpose,
-- so regressions like "yield outside generator silently works" slipped
-- through. Each assertion here pins a specific failure mode.

-- channel.recv now blocks until a value is available (real concurrent
-- channel backed by mutex+condvar). The "throws ChannelEmpty" behavior
-- only applied to the old buffer-only placeholder. try_recv is the
-- non-blocking form.
let ch2 = channel()
assert_eq(ch2.try_recv(), null)

-- recv after send works
let ch = channel()
ch.send(1)
ch.send(2)
assert_eq(ch.recv(), 1)
assert_eq(ch.recv(), 2)

-- divide by zero reports a runtime error; the expression yields null
-- so downstream code keeps running, but the process exits non-zero.
-- Tested in tests/negative/divide_by_zero.xs.

println("test_runtime_errors: all passed")
