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
- **Errors are values.** Every operation that can fail returns `result<T>`, a
  `boost::system::result` whose errors are of the category `webcpp.xactor`,
  and xactor throws nothing of its own, so its headers compile without
  exceptions, which the lint checks. Whether a program uses exceptions is its
  user's choice, on every target.
- **Fixed numbers.** The values of `errc` and `status` are fixed and never
  reused.
- **Boost.Asio is native only.** `drivers.hpp` includes it for `asio_driver`
  alone, outside WASI, each include marked `lint-world:`. Boost.Asio does not
  compile for WASI, so `example/xactor_asio.cpp` is the one program built
  natively only. A driver drains its io_context with `poll`; the lint bans
  `run`, `run_one` and `run_for`.
- **Targets.** `test/Jamfile` and `example/Jamfile` declare native, wasip2
  and wasip3.
- **Tests.** Each `test/*_test.cpp` is a lightweight_test program that
  `test/Jamfile` declares with `webcpp.run`. A case that cannot go on after a
  failed check returns (`if (!BOOST_TEST(...)) { return; }`); a helper, which
  cannot return from its case, calls `require` (`test/require.hpp`). What
  uses `asio_driver` is inside `#ifndef __wasi__`, the condition under which
  `drivers.hpp` declares it. `test/CONVERSION.md` records the conversion from
  Boost.Test, case by case.
- **The page.** `doc/xactor.adoc` and the sections it includes are xactor's
  page, which `b2 libs/xactor/doc` builds with the MrDocs reference. The
  `// tag::<name>[]` and `// end::<name>[]` lines of the examples mark what
  it includes; the `@see` of a Doc Comment names a section of its guide by
  title, and a `(doc: #<anchor>)` in a `//` comment names one by anchor. The
  build fails on a title or an anchor that names no section, so a section
  renamed changes them in the same commit. Any count the page states would be
  typed by hand, so it states none of the tree's.
- **The reference.** Every public symbol has a Doc Comment, which MrDocs
  turns into the API reference, strict: `b2 libs/xactor/doc//reference`
  fails on any symbol, parameter or return value left undocumented.
  `doc/mrdocs.yml` gives each enumerator a section of its own.
