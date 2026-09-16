# ADR-0008 — An unconfigured system is inert by construction

**Status:** Accepted
**Date:** 2026-09-15
**Affects:** `REQ-AIM-002`, `REQ-AIM-003`, `REQ-SAF-002`, `REQ-SAF-004`,
`REQ-TRK-007`

## Context

`REQ-AIM-002` requires that "a default-constructed configuration SHALL have an
empty envelope and SHALL therefore permit no firing until explicitly
configured", and `REQ-SAF-004` requires the actuator to be inactive on startup
and shutdown. The rig's field of view, boresight offset and association radius
are not known until bring-up, so the software will spend its early life
uncalibrated with a water valve attached to it.

The usual way to honour this is a validation step: parse the configuration,
check it, refuse to start if it is incomplete. That works exactly as long as
everybody remembers to call it. It fails on the paths nobody thought about — a
half-populated struct after a parse error, a unit test that constructs a
`Configuration` directly, a future field nobody added to the validator.

There is a stronger option: make "unconfigured" a state in which the *types*
cannot express a command, so that no validation step has to run at all.

## Decision

Safety by default is expressed **structurally**, in the representation, rather
than by a check someone must remember to perform.

**`AngleRange` is empty unless both limits are stated.** Its default member
initialisers place the minimum above the maximum:

```cpp
Angle minimum_{1.0};
Angle maximum_{-1.0};
```

A default-constructed range contains no angle, and the only way to obtain a
non-empty one is `AngleRange::inclusive`, which takes both limits together.
`MechanicalEnvelope` holds two of these, so a default-constructed envelope is
empty on both axes (`REQ-AIM-002`).

**Clamping to an empty range yields nothing, not a number.**
`AngleRange::clamp` and `clamp_to_envelope` return `std::optional`. An
unconfigured axis has no nearest legal angle, and returning one anyway would be
inventing an aim point nobody authorised. `std::nullopt` means "command
nothing", and it propagates: no aiming command, no clamped aim, no fire
authorisation.

**A default `Configuration` is inert in three independent ways**, so that no
single mistake restores firing:

| Default                     | Effect                                                      |
| --------------------------- | ------------------------------------------------------------ |
| Empty envelope              | Nothing can be aimed or fired (`REQ-AIM-002`)                |
| Zero association radius     | No detection joins a track, so nothing is ever confirmed (`REQ-TRK-007`, `REQ-TRK-002`) |
| Zero field of view          | Every target resolves to the neutral angles (`REQ-AIM-001`)  |

**Refusal is the default state of an authorisation.** A
default-constructed `FireAuthorisation` is refused with reason
`NOT_CONFIRMED`, and permission is carried by the presence of the granted
terms — `std::optional<GrantedFire>` — rather than by a boolean beside them
that could disagree. A forgotten assignment or an unhandled branch denies a
burst; it cannot grant one (`REQ-SAF-004`, `REQ-SAF-002`).

**`FireCommand::duration` defaults to zero.** An uninitialised fire command
opens the valve for no time at all.

**`DetectionOutcome` and `FrameInput` default to the empty, `NONE` case.**
Absence of evidence is the default input to the state machine.

The corollary, stated so it is not mistaken for an oversight: a malformed or
missing configuration file must leave `Configuration` at its defaults rather
than partially populated. Half a calibration is more dangerous than none.

## Alternatives considered

| Option                                                       | Why it was rejected                                                                                                                                 |
| ------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `AngleRange{0, 0}` by default, with a validation step         | A zero-width range still contains 0°, which in the servo frame is *straight ahead at the horizon* — the most plausible direction to hit a neighbour. The unconfigured default would be the one pointing at people. |
| A `bool configured` flag on `Configuration`                   | A flag can disagree with the data beside it, and every consumer must remember to consult it. The empty envelope cannot disagree with itself.              |
| `clamp` returning the input unchanged when the range is empty  | Silently converts "no envelope" into "any angle is fine", which is the exact inversion of the requirement.                                               |
| Throwing on an unconfigured envelope                          | An exception crossing a module boundary, on the most common early-life path, to report a condition that is expected rather than exceptional.              |
| Defaulting to the deployment envelope X ∈ [−90, +90], Y ∈ [0, +45] | Bakes one rig's geometry into hardware-independent logic (ADR-0004 rejected this for calibration generally), and makes an uncalibrated system fire *somewhere* rather than nowhere. |
| A non-default-constructible `Configuration`                   | Attractive — it would make the question unaskable — but every aggregate that holds one then becomes non-default-constructible too, and `REQ-AIM-003` wants a plain value type that `raspberry/` can fill in field by field. |

## Consequences

### Positive

* `REQ-AIM-002`'s "a default-constructed configuration emits no fire command
  for any input" is a property of the types, provable by a test that never has
  to enumerate the inputs it is quantified over.
* No initialisation order to get right and no validator to remember to call.
* The dangerous direction — straight ahead at the horizon — is not the default
  aim of an unconfigured rig, because there is no default aim at all.
* Bring-up failure mode is "the deterrent does nothing", which is observable
  and harmless, rather than "the deterrent fires somewhere unexpected".

### Negative / accepted trade-offs

* `std::optional` appears throughout the aiming path, so every caller handles
  an absence that only occurs when the rig is unconfigured. That verbosity is
  permanent and mostly unexercised in production.
* `AngleRange`'s inverted default is a deliberate oddity. It needs its comment
  to survive, or a future tidy-up will "fix" it into `{}` and quietly restore
  firing. The comment is in the header and the reason is here.
* An empty envelope and a misconfigured one are indistinguishable at the point
  of refusal: both surface as `FireRefusal::NO_ENVELOPE`.

### Follow-up work

* `raspberry/` must ensure a parse failure yields defaults rather than a
  partially filled struct (`REQ-AIM-003`).
* The reference calibration file, once the rig exists, is the only place the
  deployment envelope is written down.

## Verification

* `REQ-AIM-002` acceptance: "a default-constructed configuration emits no fire
  command for any input", and "a target below the horizon clamps to Y = 0°,
  never below".
* `REQ-SAF-004` acceptance: the simulated actuator reports inactive
  immediately after construction and after shutdown.
* The representation itself: `AngleRange`'s limits are private, so the only
  route to a non-empty range is `inclusive`, and no test can create one by
  accident.
* `core/src/public_headers.cpp` keeps these types compiled, so a change that
  breaks the empty-by-default invariant fails the build rather than waiting for
  a reviewer to notice.
