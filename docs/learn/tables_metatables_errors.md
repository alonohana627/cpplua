# Tables, metatables and errors: background

The ideas behind roadmap stages 6 to 8, for reading the code. How lua2 does each is in
[../final_docs/tables.md](../final_docs/tables.md),
[../final_docs/metatables.md](../final_docs/metatables.md) and
[../final_docs/errors.md](../final_docs/errors.md).

## 1. One structure for arrays and dictionaries

Lua has one data structure, so a table must be fast both as an array and as a hash map. Lua 5
splits each table in two: an **array part** for the integer keys 1..n and a **hash part** for
everything else. A key lands in the array part only if the array would be more than half full:
when the hash part has no free node, the table counts its integer keys by powers of two and picks
the largest size `2^k` with more than `2^(k-1)` keys below it. So `t[i] = v` in a loop fills an
array that doubles, and a sparse table stays in the hash part.

## 2. Chained scatter with Brent's variation

The hash part is an open table where collisions are chained *through the table itself*: each node
has an offset to the next node of its chain. A new key goes to its *main position* (its hash
modulo the size). If that node is taken by a key that belongs elsewhere (it is there only because
it collided earlier), that key is moved to a free node, and the new key takes its rightful place;
otherwise the new key goes to a free node, chained from its main position. The result: every key
is reachable from its main position, chains stay short even at 100% load, and no memory is
allocated per entry.

## 3. The border, and why `#` can surprise

`#t` must return a *border*: a non-nil `t[n]` followed by a nil `t[n+1]`. A table with holes has
several; finding any is enough, and Lua finds one by binary search. Since which one depends on the
table's history (its sizes, its `alimit` hint), lua2 copies Lua's algorithm exactly rather than
inventing its own, so programs that (wrongly) rely on `#` of tables with holes still behave the
same.

## 4. Metatables are a dispatch table

Every operation that cannot work on its operands asks a metatable before failing: `t.x` on a
missing key asks `__index`, `a + b` on tables asks `__add`, `f()` on a table asks `__call`. The
lookup is a plain table read by name, so it is cheap, and Lua makes the common case cheaper: a
metatable remembers in a bit which of the first events it does *not* have, so an `__index`-less
table answers in one test.

## 5. To-be-closed variables

`local x <close> = v` is Lua 5.4's version of a destructor or `defer`: `v`'s `__close` runs when
the scope ends, however it ends, including an error passing through. It needs the VM to keep a
list of the live ones, and the error machinery to call them while unwinding.

## 6. Finalizers resurrect

A finalizer (`__gc`) must run on an object that is unreachable, but the finalizer can read the
object, and even store it somewhere. So the collector does not free such an object: at the end of
marking it moves the unreachable ones to a waiting list and marks them (and everything they
reach) again; the finalizer runs later, and the object is freed only by a later cycle that finds
it unreachable again. Weak tables interact with this: an object being finalized is removed from
weak *values* first (no one should find it), but kept as a weak *key* until it is really gone.

## 7. Ephemerons

In a table with weak keys, an entry `k -> v` should keep `v` alive only while `k` is alive some
other way. If `v` refers to `k` (common: a cache of objects about objects), a naive collector
would keep both forever. Ephemeron tables are traversed specially: a value is marked only once its
key is, and the collector repeats the traversal until nothing new is marked.

## 8. Errors without longjmp

The C implementation of Lua unwinds errors with `longjmp`: fast to raise, but every protected call
pays a `setjmp`, and C++ code in between cannot clean up. lua2 returns errors as values: each
function checks its callee's status and passes errors on. In the VM this costs almost nothing,
because handlers return a status anyway, and a protected call becomes an ordinary call plus a
check. The trick is to unwind nothing on the way up: the frames stay as they were until the
`pcall` that catches the error, so a message handler can still look at the stack where the error
happened, which is what `xpcall(f, debug.traceback)` needs.
