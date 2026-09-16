# ADR-0015 — Only a healthy link may fire, with a distinct reason per status

**Status:** Accepted
**Date:** 2026-02-19
**Affects:** REQ-SAF-008, REQ-COM-002, `core/include/pigeon/core/actuator_link.hpp`, `core/include/pigeon/core/safety_policy.hpp`

## Context

`LinkStatus` has four values: `OK`, `UNAVAILABLE`, `TRANSPORT_FAILURE` and
`REJECTED`. `REQ-COM-002` says what to do when the link *fails* — abandon the
engagement, fire nothing, return to `SEARCHING` — but the headers documented
only `OK` and `UNAVAILABLE` in terms of firing. Whether a `TRANSPORT_FAILURE`
or a `REJECTED` link blocks a burst was left to whoever implemented
`authorise_fire` first.

That is a fail-open hole in a safety check. An implementer writing
`if (status == LinkStatus::UNAVAILABLE) refuse;` produces a rig that sends
water commands over a link whose last exchange failed, and nothing in the
build objects.

The hole also has a shape that reopens. Even if the four current values are
handled today, the next value added to `LinkStatus` — a timeout, a checksum
mismatch, a device fault code — inherits whatever the `else` branch happened to
be, and the gap is back without anyone deciding it should be.

Separately, a refusal that cannot say *which* rule it applied cannot be shown
to have applied the right one. `LINK_UNAVAILABLE` standing for three different
link conditions makes a field log ambiguous exactly when the rig is
misbehaving.

## Decision

**Only `LinkStatus::OK` authorises a fire command. Every other status refuses,
and each refuses with its own `FireRefusal` reason, shared with no other
status.** A link that is not known to be healthy is not a link to send water
over.

The rule is expressed as a single declared mapping in `safety_policy.hpp`:

```cpp
[[nodiscard]] std::optional<FireRefusal> refusal_for(LinkStatus status) noexcept;
```

`std::nullopt` means "this status may fire", and only `OK` produces it. Two
mechanisms keep a future `LinkStatus` value from slipping through:

1. **The definition of `refusal_for` is a `switch` with no `default:` label.**
   `-Wswitch` under the project's `-Werror` then refuses to compile an
   enumerator that has been given no answer. The requirement is documented on
   the declaration, so a reviewer has one specific line to look for.
2. **A `static_assert` in `safety_policy.hpp` pins the shape of `LinkStatus`**,
   with a message that tells the reader what the new status needs: its own
   `FireRefusal`, its own case, and an updated assertion. It catches an
   enumerator inserted, reordered or removed — changes `-Wswitch` in a
   different translation unit may report far from the reasons themselves — and
   it fails in the header beside the decision it protects.

## Alternatives considered

| Option | Why it was rejected |
| ------ | ------------------- |
| Let `TRANSPORT_FAILURE` and `REJECTED` share `LINK_UNAVAILABLE` | Makes a refusal anonymous. Three distinct faults — no link, a broken exchange, a device that said no — need three distinct responses from an operator, and a shared reason denies them the information. |
| Make `FireRefusal` a value type carrying the offending `LinkStatus` | Structurally airtight: a new status could not fail to have a reason, because the reason *is* the status. Rejected because it changes `FireRefusal` from an enumeration into a composite for one of its cases only, forces every caller to unpack a variant to answer "why?", and would invalidate the committed acceptance suite's refusal assertions for no safety gain over the two mechanisms above. |
| Treat only `OK` as permitting, but with a single `LINK_NOT_OK` reason | Half the decision. Fail-closed, but still undiagnosable, and still silent about a new status's intent. |
| Rely on code review to notice a new `LinkStatus` | Review is not a mechanism. The whole point of the fail-closed rule is that the failure mode is invisible at the call site. |
| Assert exhaustiveness with a `COUNT` sentinel enumerator | Adds a value to `LinkStatus` that is not a status, which a caller can construct and pass. It trades an unrepresentable state for a representable nonsense one. |

## Consequences

### Positive

* The safety check is fail-closed by construction: the permitting case is the
  single named one, and everything else refuses.
* A refusal names its cause, so a rig that will not fire can be diagnosed from
  its output rather than from a debugger.
* `LinkStatus` cannot grow without someone deciding, in writing, whether the
  new status may fire — the build stops until they do.
* `refusal_for` is a pure function of its argument, so its exhaustiveness is
  testable in isolation, without a link, a clock or a policy.

### Negative / accepted trade-offs

* An implementer who writes `default:` in `refusal_for` defeats the `-Wswitch`
  mechanism, and only the `static_assert` and a reviewer remain. The header
  says so explicitly rather than pretending the guarantee is absolute.
* The `static_assert` pins enumerator *values*, so appending a status at the
  end leaves it satisfied; that case is caught by `-Wswitch` instead. The two
  mechanisms cover each other's gap, and neither covers it alone.
* Conservatism costs deterrent opportunities: a burst is refused over a link
  that may in fact have been fine. A missed deterrent is cheap; water sent
  blind over a link in an unknown state is not.

### Follow-up work

* The committed acceptance suite exercises only `LinkStatus::OK` and
  `UNAVAILABLE` through `authorise_fire`. `TRANSPORT_FAILURE` and `REJECTED`,
  and the exhaustiveness of `refusal_for`, need tests from the test-engineer.

## Verification

* `REQ-SAF-008` carries acceptance criteria a test can assert directly: each
  non-`OK` status refuses, no two statuses share a reason, and every
  `LinkStatus` value is either `OK` or has a refusal reason.
* `-Werror` plus `-Wswitch` in the project's build settings fails the
  compilation of a `refusal_for` that omits a case, so the mechanism is
  enforced by `make check` rather than by convention.
* The `static_assert` in `safety_policy.hpp` fails the build if `LinkStatus`
  changes shape without the reasons being revisited.
* `scripts/trace.sh` requires `REQ-SAF-008` to be covered by a named test.
