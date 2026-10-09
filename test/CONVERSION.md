# xactor's tests: the conversion to lightweight_test

xactor's tests came from xstate-cpp (`main`, `cc11cec`): the four Boost.Test files of
`test/xactor/`, built by that repository's `suite` rule with `test/runner.cpp`. They are now
Boost.Core lightweight_test programs (`<boost/core/lightweight_test.hpp>`), one per file, which
`test/Jamfile` declares with `webcpp.run` for native, wasip2 and wasip3. This file records what
each old test checked and where the new one checks it, so that the conversion can be reviewed
assertion by assertion.

## The programs

| Old file | Old target | New file | New target |
| --- | --- | --- | --- |
| `test/xactor/scheduler_test.cpp` | `xactor-scheduler` | `test/scheduler_test.cpp` | `scheduler` |
| `test/xactor/lifecycle_test.cpp` | `xactor-lifecycle` | `test/lifecycle_test.cpp` | `lifecycle` |
| `test/xactor/fuel_test.cpp` | `xactor-fuel` | `test/fuel_test.cpp` | `fuel` |
| `test/xactor/create_actor_test.cpp` | `xactor-create-actor` | `test/create_actor_test.cpp` | `create_actor` |

Both builds had a second variant of each program, `<target>-noexcept`, without exceptions and
without RTTI, when the conversion was made:
- the old `suite` rule built it with `<exception-handling>off <rtti>off` and
  `STATELY_TEST_NO_EXCEPTIONS`, and compiled Boost.Test's own `runner.cpp` with exceptions on;
- `webcpp.run` built it natively with `<exception-handling>off <rtti>off` and
  `BOOST_NO_EXCEPTIONS`, and linked `tools/throw_exception.cpp`. lightweight_test is a header,
  and compiled in each variant.

That variant no longer exists: webcpp builds no variant without exceptions and none without
RTTI (owner decision, 2026-10-08). "Without exceptions" is now checked by the lint, which
compiles every public header, and every test and example, with `exception-handling=off`. The
records below that name a `-noexcept` variant are of the conversion, and stay as they were
made.

`test/runner.cpp` did not move, since there is no framework left to compile.

## How each form was converted

| Old | New | Why |
| --- | --- | --- |
| `BOOST_AUTO_TEST_CASE(name)` | `void name()` in the file's anonymous namespace, called from `main` in the old declaration order; `main` returns `boost::report_errors()` | Boost.Test ran a file's cases in declaration order. A `static void` function would have the same internal linkage, but clang-tidy's `misc-use-anonymous-namespace` rejects it. |
| `BOOST_TEST(a == b)`, operands that print | `BOOST_TEST_EQ(a, b)`; `!=` becomes `BOOST_TEST_NE` | Both print the two values on failure. This includes old `lifecycle_test.cpp` line 679, `BOOST_TEST((output != nullptr) == expected.has_output)`, whose left operand alone is parenthesized: `BOOST_TEST_EQ(output != nullptr, expected.has_output)`. |
| `BOOST_TEST((expr))`, `BOOST_TEST_REQUIRE((expr))` | `BOOST_TEST(expr)`, `if (!BOOST_TEST(expr)) { return; }` or `require(BOOST_TEST(expr))` | The old suite wrapped an expression in parentheses where Boost.Test could not print its operands, or could not split it, so Boost.Test printed no values. Some operands have no `operator<<`: `status`, `envelope_log`, a vector of `logged_envelope`, of pairs or of `actor_ref`, an optional and an iterator. Some expressions join two checks with `&&`. `BOOST_TEST_EQ` would not compile for the first, and does not apply to the second. One wrapped comparison has operands that print, `(record.stopped.error() == ...)` at old `lifecycle_test.cpp` line 476, and became `BOOST_TEST_EQ`. |
| `BOOST_TEST(x)` | `BOOST_TEST(x)` | |
| `BOOST_TEST(a == b, boost::test_tools::per_element())` | `BOOST_TEST_ALL_EQ(a.begin(), a.end(), b.begin(), b.end())` | Both compare the sizes and each element, and count as one assertion. In three places of `lifecycle_test.cpp`, the vector `names_of` returns is first bound to a local, whose iterators are passed. |
| `BOOST_TEST_REQUIRE(x)` in a test case | `if (!BOOST_TEST(x)) { return; }`, or `BOOST_TEST_EQ`/`_NE` by the rows above | In Boost 1.92, `BOOST_TEST` expands to `::boost::detail::test_impl(...)`, which returns `bool`, and so does `test_with_impl` for `BOOST_TEST_EQ`. The case ends, as Boost.Test ended it. |
| `BOOST_TEST_REQUIRE(x)` in a helper | `require(BOOST_TEST(x))`, from `test/require.hpp` | A helper cannot return from its caller's case, and a program built without exceptions cannot throw out of it. So `require` ends the program with `std::exit(boost::report_errors())`, after `BOOST_TEST` has reported the failure. This differs on the failure path only: the cases after it do not run, where Boost.Test ran them. The verdict is the same. |
| `BOOST_TEST(expr, name)` and `BOOST_TEST_REQUIRE(expr, target)`, a message naming a loop's item | `if (!BOOST_TEST(expr)) { BOOST_LIGHTWEIGHT_TEST_OSTREAM << "  for " << name << '\n'; }`, and the same before the `return;` of the required one | lightweight_test takes no message, so the item is printed on the line after the failure, which names its file, line and function. The check itself is unchanged, and stays not required where it was not. There are four sites, all in `lifecycle_test.cpp`, at old lines 494, 510, 515 and 578. |
| `BOOST_CHECK_THROW`, `STATELY_TEST_NO_EXCEPTIONS` | (none) | None of the four files uses either. So there is no `BOOST_TEST_THROWS`, and no `#ifndef BOOST_NO_EXCEPTIONS`. |
| Fixtures and data cases | (none) | None of the four files has a `BOOST_FIXTURE_TEST_CASE`, a `BOOST_DATA_TEST_CASE`, a suite or a global fixture. Five cases loop over their data in the case's body, and these loops are kept as they were: `finishing_and_failing_drop_the_actor_s_timers`, `stop_child_refuses_itself_a_grandchild_and_no_actor`, `an_ended_actor_neither_stops_nor_finishes_again`, `an_error_after_the_actor_ended_changes_nothing` and `each_call_of_a_turn_costs_what_it_says`. The helpers that make a case's actors and run them are plain functions in both versions, listed in the tables below. |

These are the macros of each file, old and new. Each old `BOOST_TEST_REQUIRE` became one
`if`-return or one `require`, and each assertion stayed one assertion:

| File | Old `BOOST_TEST_REQUIRE` | Old `BOOST_TEST` | New `if (!BOOST_TEST...)` | New `require(BOOST_TEST...)` | New `BOOST_TEST` | New `BOOST_TEST_EQ` | New `BOOST_TEST_NE` | New `BOOST_TEST_ALL_EQ` |
| --- | --: | --: | --: | --: | --: | --: | --: | --: |
| `scheduler_test.cpp` | 43 | 83 | 42 | 1 | 62 | 59 | 1 | 4 |
| `lifecycle_test.cpp` | 42 | 85 | 37 | 5 | 75 | 47 | 2 | 3 |
| `fuel_test.cpp` | 7 | 30 | 4 | 3 | 10 | 25 | 0 | 2 |
| `create_actor_test.cpp` | 3 | 2 | 3 | 0 | 4 | 1 | 0 | 0 |

The new `BOOST_TEST`, `_EQ`, `_NE` and `_ALL_EQ` columns include those inside an `if`-return or
a `require`. Of the old `BOOST_TEST`s, 4, 3, 2 and 0 compare `per_element`. In
`lifecycle_test.cpp`, 3 `BOOST_TEST`s and 1 `BOOST_TEST_REQUIRE` carry a message.

## How the assertions were counted

Two counts were taken, and both agree, case by case. Each assertion was also paired with its
counterpart, as the end of this section describes.

- **Static**, in the source. Comments and string literals are blanked. Then every assertion
  macro is attributed to the outermost function body that holds it: a lambda counts in the
  function around it, and a helper counts on its own row.
  - Old: `BOOST_TEST`, `BOOST_TEST_REQUIRE`, `BOOST_CHECK*`, `BOOST_REQUIRE*` and `BOOST_WARN*`.
  - New: `BOOST_TEST`, `BOOST_TEST_*` and `BOOST_ERROR`.

  Each file's total was cross-checked with a plain `grep -oE` of the same macros over the
  whole file: 126, 127, 37 and 5, old and new. These totals can be derived again at any time,
  from the old files at `cc11cec` and the new ones here, with the two commands below. The split
  by case was made by reading the function bodies, with a scratch script that is not kept; it
  can be checked against the tables by hand.

      grep -oE '\bBOOST_(TEST|TEST_REQUIRE|CHECK[A-Z_]*|REQUIRE[A-Z_]*|WARN[A-Z_]*)[[:space:]]*\(' <old file> | wc -l
      grep -oE '\bBOOST_(TEST(_[A-Z_]+)?|ERROR)[[:space:]]*\(' <new file> | wc -l

- **At run time**, the assertions each case executes, including those of the helpers it calls.
  - Old: Boost.Test's own count, from `--report_level=detailed`. Each old file was built in a
    scratch directory with Apple clang 21, Boost 1.92 and the header-only Boost.Test of
    `test/runner.cpp`.
  - New: each program was built in a scratch directory with a header forced in (`-include`),
    which wraps `BOOST_TEST`, `BOOST_TEST_EQ`, `BOOST_TEST_NE` and `BOOST_TEST_ALL_EQ` in a
    counter. The counter attributes each assertion to the case `main` was running: the
    outermost return address on its stack that lies in the executable is a call site in `main`,
    one per case. The call sites, in address order, are `main`'s calls, in source order.
  - The default build and a `-fno-exceptions -fno-rtti -DBOOST_NO_EXCEPTIONS` build gave the
    same counts.
  - The runtime counts, 636 old and new, are a record made once, at `d90fb5b`, the commit that
    added this file. The counting header was a scratch tool and is not kept: the old counts can
    be made again with Boost.Test's report, and the new ones only with a tool like it.

Both counts are of the native build. On WASI, `scheduler` has one case fewer (see below): 20
cases, 124 assertions in the source, and 235 at run time, derived from the source.

The assertions were also compared one by one. Within each file, the old and the new assertions
were paired in source order, and each was reduced to the expression it checks:
- `BOOST_TEST_EQ(a, b)` becomes `a == b`, and `_NE` becomes `!=`;
- `BOOST_TEST_ALL_EQ` becomes `a == b` of its two ranges;
- the outer parentheses and a message are dropped;
- whitespace and the case of a `u` suffix are ignored;
- whether the check is required (`BOOST_TEST_REQUIRE`, an `if`-return or a `require`) is kept.

Of the 295 pairs, 290 are identical, and all 295 agree on whether the check is required. The
other five are the differences listed below:
- `ticked`, in `scheduler_test.cpp`;
- three vectors of `names_of`, each bound to a local first, in `lifecycle_test.cpp`;
- `BOOST_TEST_EQ(output != nullptr, expected.has_output)`, for the old
  `(output != nullptr) == expected.has_output`, in `lifecycle_test.cpp`.

## The tables

A helper's assertions are counted on its own row in the static count. At run time, they are
counted in the case that calls them, as Boost.Test's report counts them.

### scheduler_test.cpp

| Old case | New function | Static, old | Static, new | Run, old | Run, new |
| --- | --- | --: | --: | --: | --: |
| `fifo_order_delivers_n_posts_in_order` | same | 7 | 7 | 101 | 101 |
| `fifo_order_holds_across_actors` | same | 5 | 5 | 5 | 5 |
| `both_drivers_produce_the_same_envelope_log` | same | 2 | 2 | 2 | 2 |
| `clock_tick_fires_timers_in_deadline_then_sequence_order` | same | 8 | 8 | 8 | 8 |
| `test_driver_delivers_exactly_one_message_per_step` | same | 8 | 8 | 14 | 14 |
| `a_keyed_timer_replaces_the_one_with_its_key_and_a_cancelled_one_never_fires` | same | 6 | 6 | 6 | 6 |
| `keys_belong_to_their_actor` | same | 4 | 4 | 4 | 4 |
| `a_retired_actor_s_timers_are_dropped` | same | 3 | 3 | 3 | 3 |
| `now_is_the_last_clock_tick` | same | 3 | 3 | 3 | 3 |
| `a_refused_keyed_timer_leaves_the_one_with_its_key` | same | 4 | 4 | 4 | 4 |
| `finishing_and_failing_drop_the_actor_s_timers` | same | 3 | 3 | 6 | 6 |
| `a_retired_actor_arms_no_timer` | same | 4 | 4 | 4 | 4 |
| `next_deadline_is_the_earliest_armed_timer` | same | 7 | 7 | 7 | 7 |
| `release_next_releases_one_due_timer_at_a_time` | same | 5 | 5 | 5 | 5 |
| `deliver_refuses_an_address_that_names_no_actor` | same | 4 | 4 | 6 | 6 |
| `run_turn_delivers_at_most_its_budget` | same | 11 | 11 | 17 | 17 |
| `a_replacement_timer_takes_a_new_arming_order` | same | 4 | 4 | 4 | 4 |
| `a_released_timer_belongs_to_the_releasing_execution_for_free` | same | 7 | 7 | 7 | 7 |
| `the_log_records_each_delivery_as_it_was_made` | same | 4 | 4 | 4 | 4 |
| `a_message_need_only_be_a_variant_that_can_be_moved` | same | 11 | 11 | 11 | 11 |
| `a_move_only_message_passes_every_path_that_carries_an_envelope` | same | 16 | 16 | 16 | 16 |
| **21 cases** | **21 functions** | **126** | **126** | **237** | **237** |

### lifecycle_test.cpp

| Old case | New function | Static, old | Static, new | Run, old | Run, new |
| --- | --- | --: | --: | --: | --: |
| `a_spawned_actor_is_active_and_has_no_output` | same | 3 | 3 | 4 | 4 |
| `spawn_refuses_a_null_logic` | same | 3 | 3 | 3 | 3 |
| `an_address_that_names_no_actor_has_no_status` | same | 3 | 3 | 3 | 3 |
| `finish_makes_the_actor_done_with_its_input_as_output` | same | 6 | 6 | 9 | 9 |
| `an_actor_finishes_once` | same | 3 | 3 | 6 | 6 |
| `a_done_actor_receives_nothing_and_nothing_is_logged` | same | 8 | 8 | 12 | 12 |
| `a_handler_error_ends_its_actor_and_not_the_run` | same | 10 | 10 | 15 | 15 |
| `stop_drops_the_pending_envelopes` | same | 6 | 6 | 8 | 8 |
| `stop_leaves_an_ended_actor_as_it_ended` | same | 4 | 4 | 7 | 7 |
| `the_statuses_keep_their_persisted_values` | same | 4 | 4 | 4 | 4 |
| `a_spawned_child_knows_its_parent` | same | 6 | 6 | 11 | 11 |
| `stop_child_stops_the_child_and_its_descendants` | same | 9 | 9 | 16 | 16 |
| `stop_child_refuses_an_actor_that_is_not_a_child` | same | 3 | 3 | 7 | 7 |
| `stop_child_stops_a_family_in_xstates_order` | `stop_child_stops_a_family_descendants_first` (renamed) | 7 | 7 | 26 | 26 |
| `stop_child_refuses_itself_a_grandchild_and_no_actor` | same | 5 | 5 | 60 | 60 |
| `stop_child_of_a_done_child_still_stops_its_descendants` | same | 5 | 5 | 18 | 18 |
| `the_host_s_stop_stops_the_whole_family` | same | 4 | 4 | 13 | 13 |
| `a_retired_actor_spawns_nothing` | same | 5 | 5 | 8 | 8 |
| `an_actor_that_stops_itself_ends_alone` | same | 5 | 5 | 24 | 24 |
| `an_ended_actor_neither_stops_nor_finishes_again` | same | 5 | 5 | 24 | 24 |
| `an_error_after_the_actor_ended_changes_nothing` | same | 4 | 4 | 14 | 14 |
| `an_actor_still_sends_in_the_turn_that_ended_it` | same | 6 | 6 | 11 | 11 |
| `a_message_to_an_ended_actor_is_paid_and_dropped` | same | 8 | 8 | 14 | 14 |
| helper `spawn` | same | 1 | 1 | in its callers | in its callers |
| helper `run_all` | same | 1 | 1 | in its callers | in its callers |
| helper `grow_tree` | same | 2 | 2 | in its callers | in its callers |
| helper `status_named` | same | 1 | 1 | in its callers | in its callers |
| **23 cases** | **23 functions** | **127** | **127** | **317** | **317** |

### fuel_test.cpp

| Old case | New function | Static, old | Static, new | Run, old | Run, new |
| --- | --- | --: | --: | --: | --: |
| `a_chain_of_messages_stops_when_its_execution_s_fuel_runs_out` | same | 4 | 4 | 7 | 7 |
| `messages_sent_side_by_side_share_their_execution_s_fuel` | same | 3 | 3 | 6 | 6 |
| `an_actor_pays_for_its_own_work_from_its_execution_s_fuel` | same | 4 | 4 | 7 | 7 |
| `an_actor_sees_the_fuel_its_execution_has_left` | same | 2 | 2 | 5 | 5 |
| `each_execution_has_fuel_of_its_own` | same | 3 | 3 | 8 | 8 |
| `a_host_delivery_costs_no_fuel` | same | 2 | 2 | 7 | 7 |
| `an_execution_not_yet_opened_has_the_whole_budget` | same | 3 | 3 | 6 | 6 |
| `a_later_delivery_shares_what_its_execution_has_left` | same | 4 | 4 | 9 | 9 |
| `a_reply_to_the_host_and_a_send_to_no_actor_are_refused_unpaid` | same | 6 | 6 | 12 | 12 |
| `each_call_of_a_turn_costs_what_it_says` | same | 3 | 3 | 10 | 10 |
| helper `spawn_worker` | same | 1 | 1 | in its callers | in its callers |
| helper `start_in` | same | 1 | 1 | in its callers | in its callers |
| helper `run` | same | 1 | 1 | in its callers | in its callers |
| **10 cases** | **10 functions** | **37** | **37** | **77** | **77** |

### create_actor_test.cpp

| Old case | New function | Static, old | Static, new | Run, old | Run, new |
| --- | --- | --: | --: | --: | --: |
| `create_actor_spawns_an_active_actor_that_receives_messages` | same | 5 | 5 | 5 | 5 |
| **1 case** | **1 function** | **5** | **5** | **5** | **5** |

In total, 55 cases, with 295 assertions in the source and 636 at run time, both old and new.

## Differences, case by case

No count differs, in any case or helper. These are the other differences:

- **`stop_child_stops_a_family_in_xstates_order`** is now
  `stop_child_stops_a_family_descendants_first`. Three comments of `lifecycle_test.cpp` no
  longer describe the order in which a family stops as XState's: they state it in xactor's own
  terms, "each actor after its descendants, siblings in spawn order". This matches the headers'
  comments (xactor derives from no original). The assertions are unchanged.
- **`both_drivers_produce_the_same_envelope_log`** is built only where `drivers.hpp` declares
  `asio_driver`, outside `__wasi__` (`#ifndef __wasi__`, both the function and its call in
  `main`). It runs natively, with its 2 assertions, and is absent on wasip2 and wasip3.
- **`a_message_need_only_be_a_variant_that_can_be_moved`** delivers its tick at time 1 with the
  Asio driver. On WASI, the FIFO driver delivers it, since there is no Asio driver. In both
  builds, the result goes to one variable, `ticked`, and one assertion,
  `BOOST_TEST_EQ(ticked.value(), 1U)`, checks it. The old suite asserted
  `asio.run_until_idle(&log).value() == 1u` directly, and natively the new suite checks the same
  value from the same call.
- **A failed `BOOST_TEST_REQUIRE` in a helper** (`spawn`, `run_all`, `grow_tree` and
  `status_named` of lifecycle; `spawn_worker`, `start_in` and `run` of fuel; the lambda
  `deliver_lines` of `run_turn_delivers_at_most_its_budget`) now ends the program. Before, it
  ended only its case. No such check fails while the library is correct, and none failed on the
  planted defects below.
- **An exception that escapes a case**, in the builds with exceptions, ends the program. The
  tests reach about 34 calls of `value()`, on a `result` or an `optional`, and 2 of
  `std::map::at`, any of which throws when what the test expects is not there. Boost.Test's
  execution monitor caught such an exception, reported it with the case's name and ran the
  remaining cases. Now it leaves `main`, and `std::terminate` ends the program: the runtime may
  print the exception's type and message, but no case, file or line. The verdict is the same,
  since the exit status is not zero. Built without exceptions, the throw was and is
  `boost::throw_exception`'s abort in both suites. None of these throws happens while the
  library is correct, and none happened on the planted defects below.
- **The four messages that named a loop's item** are printed on the line after the failure,
  where Boost.Test printed them in the failure's line (see the conversion table).

## Planted defects

Each defect was planted in a scratch copy of the tree, then reverted, with every test run from
scratch (`b2 -a`) each time. The same defect was planted in a scratch copy of xstate-cpp's
headers, and the old suite was run against it.

| Defect | Planted in `scheduler.hpp` | New tests that fail | Old tests that fail | After the revert |
| --- | --- | --- | --- | --- |
| (a) the move-only guarantee (doc: #xactor-invariant-28) | `Message payload;` of `timer` becomes `Message payload{};` | Under clang-18 with libstdc++ (the container lane), `scheduler` and `scheduler-noexcept` do not compile: "no matching constructor for initialization of `std::variant<ticket>`" at `scheduler.hpp:206`, instantiated through `std::erase_if` from `ticket_counter`'s `turn.finish(ticket{0})` in `a_message_need_only_be_a_variant_that_can_be_moved` (`scheduler_test.cpp:800` at `d90fb5b`) | The same error, from the same statement of the same case (the old `scheduler_test.cpp:733` at `cc11cec`) | clang-18: every test passes |
| (b) fuel accounting | `take` refuses when `remaining <= units` instead of `remaining < units` | `fuel`, `scheduler` and their `-noexcept` variants: 8 cases, with 15 failed assertions | The same 8 cases, with the same 15 failed assertions, case by case | every test passes |
| (c) a lifecycle status | `finish` retires its actor as `status::stopped` instead of `status::done` | `lifecycle`, `scheduler` and their `-noexcept` variants: 7 cases, with 7 failed assertions | The same 7 cases, with the same 7 failed assertions | every test passes |

Defect (a) compiles with g++-14 on libstdc++ and with Apple clang 21 on libc++, where every
test passes. Only Clang with libstdc++ instantiates the default member initializer when
`std::erase_if` asks whether a `timer` is default-constructible. That is why the lane runs in
the container.
