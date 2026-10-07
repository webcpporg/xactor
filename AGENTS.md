# Working on xactor

webcpp's rules apply here: read the superproject's
[AGENTS.md](../../AGENTS.md)
(<https://github.com/webcpporg/webcpp/blob/main/AGENTS.md>) first. This file
holds only what is specific to xactor.

- **It stands alone.** xactor depends on Boost's headers only, and no
  header, comment or example of it names another webcpp library: those build
  on xactor, never the reverse. `build.jam` declares
  `/webcpp/xactor//xactor`, an alias that adds `include` and
  `/boost//headers`.
- **Determinism is its point.** The delivery order is a function of the
  enqueue order alone, time moves only through `clock_tick` and
  `release_next`, and every driver writes the same envelope log for the same
  run. A change that makes a run depend on anything else is a bug.
- **Errors are values.** Every operation returns `result<T>`, a
  `boost::system::result` whose errors are of the category `webcpp.xactor`,
  and xactor throws nothing of its own, so it builds with exceptions and RTTI
  off: on wasip2, and in every test's `-noexcept` variant.
- **Fixed numbers.** The values of `errc` and `status` are fixed and never
  reused.
- **Boost.Asio is native only.** `drivers.hpp` includes it for `asio_driver`
  alone, outside WASI, each include marked `lint-world:`. Boost.Asio does not
  compile for WASI, so `example/xactor_asio.cpp` is the one program built
  natively only. A driver drains its io_context with `poll`; the lint bans
  `run`, `run_one` and `run_for`.
- **Targets.** `test/Jamfile` and `example/Jamfile` declare native, wasip2
  and wasip3.
- **The page.** The `// tag::<name>[]` and `// end::<name>[]` lines of the
  examples mark what xactor's page includes, and the `(doc: #<anchor>)`
  references of the headers name its sections; keep both in step with
  `doc/`.
