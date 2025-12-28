-- Array invariants. These catch silent bugs in map/filter/reverse and
-- any implementation that mutates when it shouldn't.
load "tests/property/harness.xs"

forall("len(push(xs, x)) == len(xs) + 1", 50,
    || [gen_int_array(10), gen_int()],
    |args| {
        let xs = args[0]
        let before = xs.len()
        xs.push(args[1])
        xs.len() == before + 1
    })

forall("reverse is involutive", 50,
    || gen_int_array(10),
    |xs| xs.reverse().reverse() == xs)

forall("map identity", 50,
    || gen_int_array(10),
    |xs| xs.map(|x| x) == xs)

forall("filter subset", 50,
    || gen_int_array(20),
    |xs| xs.filter(|x| x > 0).len() <= xs.len())

forall("sort preserves length", 50,
    || gen_int_array(20),
    |xs| xs.sort().len() == xs.len())

forall("concat length", 50,
    || [gen_int_array(10), gen_int_array(10)],
    |args| args[0].concat(args[1]).len() == args[0].len() + args[1].len())

prop_report("arrays")
