# System Requirements

This document is the **authoritative specification** of what the system must do.

Every requirement has a stable ID. Implementation and tests reference these IDs,
and `scripts/trace.sh` maps each requirement to the tests that verify it.

## How to read this document

Each requirement has:

| Field          | Meaning                                                         |
| -------------- | --------------------------------------------------------------- |
| **ID**         | Stable identifier, e.g. `REQ-DET-001`. Never reuse or renumber.  |
| **Statement**  | Normative behaviour, using SHALL / SHALL NOT.                    |
| **Rationale**  | Why the requirement exists.                                      |
| **Acceptance** | Objectively checkable criteria a test can assert.                |
| **Superseded by** | Present only on retired requirements. Names the replacement.  |

### Requirements have no status field

Two questions could be asked of any requirement: *is it agreed?* and *is it
built?* Neither is recorded here, because both are already recorded somewhere
that cannot be edited to say something untrue:

| Question      | Answered by                                                             |
| ------------- | ------------------------------------------------------------------------ |
| Is it agreed? | **Its presence in this document.** This file is the authoritative specification, and changes to it are reviewed. If it is here, it is binding. |
| Is it built?  | **The tests.** `scripts/trace.sh` derives the verified set from the tests and prints it. |

A status field would restate those facts in a mutable place that nothing
validates, and a restated fact eventually contradicts the original. Worse, it
makes the damage cheap: promoting a requirement by editing one word is a
one-line diff that is easy to miss, whereas adding a whole requirement is a
conspicuous one. Removing the field makes this document harder to corrupt, not
easier (ADR-0006).

Consequences worth knowing:

* A requirement with no test is **outstanding work**, reported by the gate as
  `UNVERIFIED` but not a failure. Nobody has claimed it works.
* A requirement that was verified and no longer is, is a **coverage
  regression** and fails the gate. That is what deleting a test to get green
  looks like from the outside. The verified set lives in `verified.txt` and
  only ratchets upwards.
* Nothing that is still under discussion belongs in this document. Put it in
  [Open questions](#open-questions), or leave it in an unmerged pull request.

### Retiring a requirement

Never delete a requirement and never reuse its ID. Add a `**Superseded by:**`
line naming the replacement:

```markdown
### REQ-EXA-001 — Older behaviour

**Superseded by:** REQ-EXA-002
```

The gate checks that the successor exists, and allows the retired requirement
to drop out of the verified set.

ID prefixes:

| Prefix | Area                                 |
| ------ | ------------------------------------ |
| `DET`  | Detection                            |
| `TRK`  | Target state machine and tracking    |
| `AIM`  | Targeting and angle calculation      |
| `COM`  | Raspberry Pi ↔ Arduino communication |
| `ACT`  | Actuation (servos, water)            |
| `SAF`  | Safety                               |
| `DEV`  | Development environment constraints  |

## Rules for changing this document

* Agents **must not** invent requirements. If behaviour is unspecified, raise an
  open question in the [Open questions](#open-questions) section and stop.
* Changing an approved requirement requires an ADR in `docs/decisions/`.
* Never delete a requirement; add a `**Superseded by:**` line naming the
  replacement.

---

## Detection

### REQ-DET-001 — Frame classification

**Statement:** The detection component SHALL classify every camera frame it
receives as exactly one of `NONE` or `FOUND`.

**Rationale:** A single, closed result set keeps the downstream state machine
total and testable.

**Acceptance:**

* Given a fixture frame containing a pigeon, the detector returns `FOUND`.
* Given a fixture frame containing no pigeon, the detector returns `NONE`.
* The detector never returns any other value and never throws for a well-formed
  frame.

### REQ-DET-002 — Detector is independent of image acquisition

**Statement:** The detection component SHALL accept frames through an interface
and SHALL NOT depend on any camera, GPIO or platform-specific API.

**Rationale:** Enables deterministic testing from recorded fixtures
(`REQ-DEV-002`).

**Acceptance:**

* `core/` contains no include of a camera, GPIO, Raspberry Pi or Arduino header.
* The full detection path can be exercised from files on disk.

### REQ-DET-003 — Detection throughput

**Statement:** On the deployment target (Raspberry Pi 3B, 1 GB RAM) the
detection pipeline SHALL process at least **5 frames per second**.

**Rationale:** Confirmation needs three consecutive frames and verification
needs a fourth, so the engagement latency is roughly four frame periods. At
5 FPS that is under a second, which is fast enough to still find the bird where
it was seen.

**Acceptance:**

* Measured on target hardware, sustained throughput is at least 5 FPS.
* This is verified by a hardware-in-the-loop test, which stays out of the
  default suite and out of CI (`REQ-DEV-003`).

### REQ-DET-004 — Frame pixel format

**Statement:** A frame presented to the detection component SHALL be packed
**RGB888**: three 8-bit channels per pixel in red, green, blue order, rows
ordered top to bottom and separated by a stride in bytes that is at least three
times the frame width. `core/` SHALL NOT perform colour-space conversion; a
producer that cannot emit RGB888 converts on its own side.

**Rationale:** Detection runtimes generally expect 8-bit RGB, and the Raspberry
Pi camera stack can emit it directly. YUV420 would save a conversion on the Pi
but complicates every fixture, and fixtures are the thing that has to stay
simple because every deterministic test depends on them (`REQ-DEV-002`).
Declaring one format also means the frame type carries no format field to get
wrong.

**Acceptance:**

* The frame type carries width, height and stride, and the buffer it views is
  at least `stride × height` bytes.
* Fixtures are stored as RGB888 and are byte-identical between runs.
* A pixel at `(x, y)` begins at byte `y × stride + x × 3`.

---

## Target state machine

### REQ-TRK-001 — Target states

**Statement:** The system SHALL have exactly three target states: `SEARCHING`,
`TARGET_LOCKED` and `TARGET_LOST`. The initial state SHALL be `SEARCHING`.

**Acceptance:**

* A freshly constructed state machine reports `SEARCHING`.
* No transition can produce a state outside this set.

### REQ-TRK-002 — Lock confirmation requires three consecutive detections of one track

**Statement:** While in `SEARCHING`, the system SHALL transition to
`TARGET_LOCKED` only after the **same track** (`REQ-TRK-007`) has been detected
in three consecutive frames.

**Rationale:** Single-frame detections are too noisy to justify firing water.
Requiring the same track, rather than mere presence, prevents three different
birds passing through the frame from confirming a target that was never there,
and gives `REQ-AIM-001` a well-defined position to aim at.

**Acceptance:**

* `FOUND`, `FOUND` on one track → still `SEARCHING`.
* `FOUND`, `FOUND`, `FOUND` on one track → `TARGET_LOCKED`.
* `FOUND`, `FOUND`, `NONE`, `FOUND`, `FOUND` → still `SEARCHING`
  (see `REQ-TRK-003`).
* Three consecutive frames each containing a detection, but too far apart to
  associate into one track, do **not** confirm a target.

### REQ-TRK-007 — Detections are associated into tracks across frames

**Statement:** The system SHALL associate each detection with the nearest
existing track whose centroid lies within a configured association radius. The
radius SHALL be **inclusive**: a detection whose centroid is exactly the
association radius from a track's centroid SHALL associate with that track. A
detection that matches no track SHALL start a new track. A track that receives
no detection in a frame SHALL have its consecutive-detection count reset
(`REQ-TRK-003`).

**Rationale:** Confirmation and aiming are both statements about *a pigeon*,
not about *a frame*. Without association neither is well defined.

"Within a configured radius" reads inclusively in plain English, and a closed
interval is what a reader assumes, so the inclusive reading is the one that
surprises nobody. In floating-point practice exact equality essentially never
occurs, so this settles the specification rather than the observable behaviour
— but a hole in a specification gets filled by two different guesses in two
different places, which is how a tracker and its test end up disagreeing.

**Acceptance:**

* Two detections in successive frames within the association radius produce one
  track with a count of two.
* A detection whose centroid lies at **exactly** the association radius from an
  existing track's centroid joins that track, raising its count to two; one a
  fraction of a pixel further away does not.
* A detection in the following frame that lies beyond the association radius
  from every existing track starts a **new** track with a count of one, while
  the track it failed to match is discarded because it received no detection
  (`REQ-TRK-009`).
* Association is deterministic: identical frame sequences produce identical
  tracks, independent of iteration order (`REQ-DEV-002`).

### REQ-TRK-008 — One target is engaged at a time

**Statement:** When several tracks are confirmable in the same frame, the
system SHALL select exactly one — the detection with the largest bounding-box
area — and SHALL ignore the others for the duration of the engagement. Ties
SHALL be broken deterministically by ascending X then ascending Y centroid
coordinate.

**Rationale:** Pigeons are gregarious, so multi-bird frames are the normal
case, not an edge case. Refusing to act on ambiguous frames would leave the
system permanently idle. The largest detection is the closest, and therefore
the one the deterrent can most plausibly reach.

**Acceptance:**

* A frame containing three confirmable tracks produces exactly one engagement.
* Two detections of identical area produce a stable, documented choice, and the
  same choice on every run.
* No fire command is ever issued for more than one target in one engagement.

### REQ-TRK-003 — Non-detection resets the confirmation counter

**Statement:** While in `SEARCHING`, a frame classified `NONE` SHALL reset the
consecutive-detection counter to zero.

**Acceptance:**

* After any interleaved `NONE`, three further consecutive `FOUND` frames are
  required before the state becomes `TARGET_LOCKED`.

### REQ-TRK-004 — Re-verification before firing

**Statement:** After entering `TARGET_LOCKED` and after aiming commands have
been sent, the system SHALL classify the **immediately following frame** before
issuing a fire command. There SHALL be no wall-clock timeout on this step.

**Rationale:** The target may leave while the servos are moving. Expressing the
deadline in frames rather than milliseconds keeps the target state machine a
pure function of its inputs, with no clock to inject and no timing-sensitive
tests (`REQ-DEV-002`).

**Acceptance:**

* Verification frame `FOUND` → a fire command is issued.
* Verification frame `NONE` → no fire command is issued.
* The state machine reads no clock on this path.
* If no further frame arrives, the engagement remains unresolved and no fire
  command is issued.

### REQ-TRK-005 — Target loss

**Statement:** If the verification frame required by `REQ-TRK-004` is classified
`NONE`, the system SHALL transition to `TARGET_LOST`, SHALL NOT issue a fire
command, and SHALL then return to `SEARCHING` with a zeroed confirmation
counter.

**Acceptance:**

* The sequence `FOUND` ×3 then `NONE` yields states `TARGET_LOCKED` →
  `TARGET_LOST` → `SEARCHING`.
* No fire command is emitted anywhere in that sequence.

### REQ-TRK-006 — `TARGET_LOST` is only reachable from `TARGET_LOCKED`

**Statement:** The system SHALL enter `TARGET_LOST` only from `TARGET_LOCKED`.

**Acceptance:**

* No sequence of frames starting in `SEARCHING` reaches `TARGET_LOST` without
  passing through `TARGET_LOCKED`.

### REQ-TRK-009 — A track that misses a frame is discarded

**Statement:** A track that receives no detection in a frame SHALL be
discarded. The system SHALL NOT retain tracks across a missed frame, and SHALL
NOT provide a configurable retention period.

**Rationale:** Strengthens the counter reset of `REQ-TRK-003` and
`REQ-TRK-007`. A retained track whose count has been reset to zero re-associates
at a count of one, which is indistinguishable from a brand-new track: retention
changes no confirmation outcome. It would, however, add a retention parameter
to calibrate and a decay path to test. Track identity matters only while a
track is engaged, and that case is already decided — a locked track that is not
re-detected produces `TARGET_LOST` on the same frame (`REQ-TRK-010`,
`REQ-TRK-005`). Recorded in ADR-0010.

**Acceptance:**

* A track absent for one frame and detected again in the next has a
  consecutive-detection count of one, not two.
* Every track in the track set has a consecutive-detection count of at least
  one.
* No configured track-retention or track-decay parameter exists.

### REQ-TRK-010 — Verification re-detects the locked track

**Statement:** The verification frame required by `REQ-TRK-004` SHALL be
satisfied only if **the locked track itself** is detected in it. A verification
frame in which the locked track is absent SHALL produce the transition to
`TARGET_LOST` required by `REQ-TRK-005`, whether that frame is classified
`NONE` or `FOUND` on other tracks.

**Rationale:** The reasoning behind `REQ-TRK-002` — three different birds must
not confirm a target that was never persistently there — applies identically to
verification. Accepting any detection would let one bird confirm the engagement
and a different bird authorise the water, which also contradicts the
one-target-at-a-time rule of `REQ-TRK-008`. This is the stricter reading, and
its failure mode is not firing. Recorded in ADR-0009.

**Acceptance:**

* Verification frame contains the locked track → a fire command is issued.
* Verification frame is `FOUND` but contains only other tracks → no fire
  command, and the state becomes `TARGET_LOST`.
* Verification frame is `NONE` → no fire command, and the state becomes
  `TARGET_LOST`.

### REQ-TRK-011 — An engagement ends when the fire command is issued

**Statement:** After a fire command has been issued the system SHALL return to
`SEARCHING`. It SHALL NOT enter any state outside the three of `REQ-TRK-001`,
and it MAY engage the same track again, subject to the cool-down and maximum
engagement rate of `REQ-SAF-005`.

**Rationale:** A fourth "fired" state would breach `REQ-TRK-001`. Refusing ever
to re-engage a track would require remembering every track fired upon, for an
unbounded time, and the bird just sprayed is precisely the one most likely to
still be there. Harassment is bounded by the rate limiter, which is a
requirement that already exists and is already tested, rather than by memory in
the state machine. Recorded in ADR-0012.

**Acceptance:**

* `FOUND` ×3 on one track followed by a verification frame on that track yields
  `TARGET_LOCKED`, then a fire command, then `SEARCHING`.
* No second fire command is issued before the cool-down has elapsed, even while
  the same track remains confirmable.
* No state outside the three of `REQ-TRK-001` is reachable after firing.

### REQ-TRK-012 — Every engagement earns its own confirmation

**Statement:** When an engagement ends — because a fire command was issued
(`REQ-TRK-011`) or because the engagement was abandoned (`REQ-COM-002`) — the
engaged track's consecutive-detection count SHALL be reset. The bird that was
engaged SHALL be confirmed again only after three fresh consecutive detections
(`REQ-TRK-002`), counted from the first frame after the engagement ended. No
engagement SHALL be confirmed by detections that were counted towards a
previous engagement.

**Rationale:** `REQ-SAF-002` forbids a fire command unless `REQ-TRK-002` and
`REQ-TRK-004` have both been satisfied **for the current engagement**. Without
this reset, a second burst is authorised by detections that occurred during the
*first* engagement: the confirmation is inherited rather than earned, which is
exactly what `REQ-SAF-002` exists to prevent. Resetting makes that requirement
literally true rather than approximately true.

It costs no responsiveness. Three fresh detections at 5 FPS (`REQ-DET-003`) is
600 ms, and the cool-down is 2 s (`REQ-SAF-005`), so re-confirmation completes
comfortably inside a wait the system must serve anyway: the rig is no slower to
fire, it merely stops claiming a confirmation it did not earn. It also avoids
pointless actuation — without the reset the rig re-locks and re-aims on every
frame at a bird it is forbidden to fire at, which is servo wear for no
deterrent benefit.

The accepted trade-off: if the bird is detected intermittently, the count can
restart and delay the next burst beyond the cool-down, so some opportunities
that a non-resetting design would take will be missed. A missed deterrent is
cheap; an unearned burst is not.

The `TARGET_LOST` path needs no rule of its own: the track was not detected, so
`REQ-TRK-009` has already discarded it. Recorded in ADR-0013.

**Acceptance:**

* A track detected in every frame, engaged and fired upon in frame N, is not
  confirmed in frame N+1 or N+2, and is confirmed in frame N+3 at the earliest.
* No aiming command is issued for that bird in frames N+1 and N+2, and the
  state is `SEARCHING` throughout them.
* The consecutive-detection count observed for that bird in frame N+1 is one,
  not five.
* After an engagement abandoned under `REQ-COM-002`, the same holds from the
  frame in which it was abandoned.
* A track that was not the engaged one keeps its consecutive-detection count
  across the end of another track's engagement.

---

## Targeting

### REQ-AIM-001 — Angle calculation

**Statement:** The camera SHALL be mounted on the pan/tilt rig, boresighted
with the deterrent nozzle. For a confirmed target the system SHALL compute the
X and Y servo angles as an angular offset from the image centre, derived from
the detection centroid and the camera's horizontal and vertical fields of view,
corrected by a configured boresight offset.

**Rationale:** A boresighted camera makes the aiming direction independent of
the range to the target, so no depth estimate is required from a single camera.
Calibration reduces to two field-of-view values and a boresight offset.

**Acceptance:**

* A target at the image centre yields the configured neutral angles plus the
  boresight offset.
* A target at the horizontal image edge yields an offset of half the horizontal
  field of view.
* Known fixture positions yield the documented expected angles within a
  documented tolerance.
* The computation uses no range, depth or scale estimate.

### REQ-AIM-002 — Angles are clamped to the mechanical envelope

**Statement:** Computed angles SHALL be clamped to the configured minimum and
maximum for each axis before being sent to the actuator system. The deployment
envelope is X ∈ [−90°, +90°] and Y ∈ [0°, +45°], where X = 0° points straight
ahead, positive X is to the right, **Y = 0° is the horizon** and positive Y is
elevated. The system SHALL NOT command any elevation below the horizon. A
default-constructed configuration SHALL have an empty envelope and SHALL
therefore permit no firing until explicitly configured (`REQ-SAF-004`).

**Rationale:** Anchoring Y = 0° at the horizon makes "never point at the
ground" a property of the datum itself rather than a rule someone has to
remember. A negative elevation is not merely out of range, it is
unrepresentable in the configured envelope.

**Acceptance:**

* An out-of-range computed angle is emitted as the corresponding limit.
* No angle outside the configured envelope is ever emitted.
* A default-constructed configuration emits no fire command for any input.
* A target below the horizon in the image clamps to Y = 0°, never below.
* The nozzle is never commanded below the horizontal.

### REQ-AIM-003 — Calibration is data, loaded outside `core/`

**Statement:** Camera fields of view, boresight offset, neutral angles,
mechanical envelope and association radius SHALL be stored in a
version-controlled key/value configuration file, parsed in `raspberry/` and
passed into `core/` as a plain value type. `core/` SHALL NOT read the file
itself.

**Rationale:** Keeps `core/` free of I/O (`REQ-DEV-001`) and keeps calibration
reviewable in version control rather than living in someone's memory. A
key/value format needs no third-party parser, which matters on a 1 GB target.

**Acceptance:**

* `core/` contains no file access; the configuration arrives as a struct.
* A missing or malformed configuration file is rejected with a clear error and
  leaves the system in the safe, non-firing default state (`REQ-SAF-004`).
* The same configuration file always produces the same in-memory values.

---

## Communication

### REQ-COM-001 — Documented protocol at the device boundary

**Statement:** The Raspberry Pi ↔ Arduino protocol SHALL be specified in
`docs/architecture/` and SHALL be implementable and testable without hardware.

**Acceptance:**

* Encoding and decoding are covered by tests that use no serial device.
* Malformed input is rejected without undefined behaviour.

**Blocked on work not yet done.** The protocol has not been designed. `core/`
deliberately models the device boundary as *operations* — `ActuatorLink` — and
not as an encoding (ADR-0007), so there is nothing in `core/` a test could
assert against without inventing an API. This requirement will therefore be
reported `UNVERIFIED` by `scripts/trace.sh` until the wire protocol is
specified in `docs/architecture/` and implemented in `raspberry/` and
`arduino/`. That is outstanding work with a known owner, not a missing test:
recorded here so the permanent `UNVERIFIED` line is explained rather than
mysterious.

### REQ-COM-002 — Fail-safe on communication loss

**Statement:** If the actuator link becomes unavailable, the system SHALL
abandon the current engagement, SHALL NOT issue a fire command, and SHALL return
to `SEARCHING`.

**Acceptance:**

* With a simulated link that fails after locking, no fire command is emitted and
  the final state is `SEARCHING`.

---

## Safety

> These requirements exist because the system aims a water jet under autonomous
> control. They are not optional.

### REQ-SAF-001 — Bounded fire duration

**Statement:** A single fire command SHALL activate the water actuator for no
longer than a configured maximum duration, defaulting to **500 ms**, after
which it SHALL deactivate without requiring a further command. The Arduino
SHALL enforce this bound independently, so that the actuator deactivates even
if the Raspberry Pi stops responding mid-engagement.

**Rationale:** A short burst is the deterrent; a long one wastes water and
soaks the surroundings. Enforcement on the Arduino is what makes the bound real
rather than aspirational — a limit that only the commanding device enforces
fails exactly when the commanding device fails.

**Acceptance:**

* A simulated actuator that receives a fire command is inactive again after the
  configured maximum duration.
* A simulated link that goes silent immediately after a fire command still
  results in an inactive actuator.
* A configured duration longer than the firmware's own bound is rejected or
  truncated by the firmware, not obeyed.

### REQ-SAF-005 — Engagement rate is limited

**Statement:** After firing, the system SHALL NOT begin another engagement for
a configured cool-down period, defaulting to **2 seconds**, and SHALL NOT
exceed a configured maximum engagement rate, defaulting to **6 engagements per
minute**.

Both limits SHALL use one convention for elapsed time: a period of duration *D*
that began at *t* SHALL be treated as **elapsed** once the monotonic clock
reads *t + D* or later, and a time window of length *D* ending at *now* SHALL
be **half-open** — it contains every instant strictly later than *now − D*. A
cool-down of 2 s beginning at *t* therefore occupies [*t*, *t* + 2000 ms), and
a burst exactly one minute old lies outside the one-minute window.

**Rationale:** Deterrence, not harassment. Rate limiting also protects the pump
from an abusive duty cycle and bounds water consumption.

One convention, stated once and covering both limits, rather than two rules
that will drift apart — the cool-down and the rate window are the same kind of
question asked twice. It is also what an implementer writing the obvious `>=`
comparison gets by default, which is a property worth having: a convention that
matches the naive implementation is one that survives the next author
(ADR-0014).

**Acceptance:**

* A fire command immediately followed by another confirmed target produces no
  second fire command until the cool-down has elapsed.
* A burst authorised at **exactly** the cool-down duration after the previous
  transmitted burst is permitted; one a millisecond earlier is refused.
* A sequence of confirmed targets arriving faster than the configured rate
  produces no more than the configured number of fire commands per minute.
* A transmitted burst **exactly** one minute old no longer counts towards the
  rate limit, so the engagement it occupied becomes available again at that
  instant.
* Rate limiting is driven by an injected monotonic clock, never by the wall
  clock, so the behaviour is deterministic under test (`REQ-DEV-002`).

### REQ-SAF-002 — No fire without a fresh confirmation

**Statement:** The system SHALL NOT issue a fire command unless `REQ-TRK-002`
and `REQ-TRK-004` have both been satisfied for the current engagement.

**Acceptance:**

* Exhaustive state-machine tests show no reachable path to a fire command that
  skips either condition.

### REQ-SAF-003 — Configurable exclusion zone

**Statement:** The system SHALL support a configured region of the angle space
in which firing is prohibited, and SHALL NOT fire while aimed inside it. The
zone SHALL be configurable per installation. The reference installation
configures **no exclusion zone**; the mechanism is nevertheless required and
SHALL remain implemented and tested.

**Rationale:** Prevents spraying neighbours, windows, walkways, or people. The
current installation has nothing within reach that needs excluding, but a
deterrent that cannot be told where not to fire is one that must be rebuilt the
first time it is repositioned. Retaining the mechanism costs a comparison;
retrofitting it later costs a redesign.

**Acceptance:**

* A target that resolves to angles inside a configured exclusion zone produces
  no fire command.
* With no exclusion zone configured, firing is permitted anywhere within the
  mechanical envelope.
* The exclusion-zone test suite runs with an explicitly configured zone, so the
  mechanism stays verified even while the reference installation configures
  none.

### REQ-SAF-004 — Safe state on startup and shutdown

**Statement:** On startup and on shutdown the actuator system SHALL be placed in
a state in which the water actuator is inactive.

**Acceptance:**

* The simulated actuator reports inactive immediately after construction and
  after shutdown.

### REQ-SAF-006 — Exclusion zone geometry

**Statement:** The exclusion zone of `REQ-SAF-003` SHALL be an axis-aligned
rectangle in servo-angle space, defined by one closed interval on each axis.
**At most one** exclusion zone SHALL be configurable per installation. Firing
SHALL be prohibited when the commanded angles lie inside both intervals.

**Rationale:** A rectangle in angle space is the shape the mechanical envelope
already uses, so the two are described in the same terms and reviewed in the
same diff. Limiting an installation to one zone keeps the configuration
incapable of expressing a second — an unrepresentable case needs no
undocumented behaviour — and matches what a rig mounted in one position needs:
one direction it must not spray. The reference installation configures none
(Q9), which is why the mechanism must be tested against an explicitly
configured zone (`REQ-SAF-003`).

**Acceptance:**

* Angles inside both intervals produce no fire command.
* Angles inside one interval but not the other permit firing.
* The configuration cannot represent more than one zone.
* With no zone configured, firing is permitted anywhere within the mechanical
  envelope.

### REQ-SAF-007 — A transmitted fire command counts

**Statement:** A fire command that has been transmitted to the actuator system
SHALL count towards the cool-down and the maximum engagement rate of
`REQ-SAF-005`, regardless of the response — including rejection, timeout, or
loss of the link. A fire command that was refused before transmission SHALL NOT
count.

**Rationale:** Once a command has left the Raspberry Pi, whether water left the
nozzle is not knowable from this side of the link. Counting it risks one missed
deterrent; not counting it risks a retry loop firing repeatedly over a flaky
link, which is the failure the rate limit exists to prevent. The asymmetry of
those two costs decides it. Recorded in ADR-0011.

**Acceptance:**

* A simulated link that rejects a fire command still starts the cool-down.
* A simulated link that fails after transmission produces no further fire
  command until the cool-down has elapsed.
* A burst refused before transmission — empty envelope, exclusion zone, rate
  limit — starts no cool-down and consumes no engagement.

### REQ-SAF-008 — Only a healthy link may fire

**Statement:** The system SHALL issue a fire command only while the actuator
link reports `OK`. Every other link status SHALL refuse the burst, and each
status SHALL produce its **own** refusal reason, distinct from the reason
produced by any other status and from every other cause of refusal.

**Rationale:** A link that is not known to be healthy is not a link to send
water over. `REQ-COM-002` already says what to do when the link becomes
*unavailable*; it says nothing about a link that is open but failing, or one
that rejects what it is told, and those were left as an implementer's
coin-flip. Refusing on anything but `OK` makes the set of statuses total: there
is no status without an answer, and the answer errs towards not firing.

Distinct reasons make a refusal diagnosable. "It did not fire" is not a
diagnosis, and a rig in the field that cannot say whether the link was down,
the exchange failed or the firmware refused the command is a rig that has to be
debugged by guesswork. The reasons are one-to-one with the statuses so that a
status added later cannot quietly inherit another's reason (ADR-0015).

**Acceptance:**

* A link reporting `OK` and an otherwise permitted engagement produces a fire
  command.
* A link reporting an unavailable link, a transport failure, or a rejection
  each produces no fire command.
* The refusal reason differs for each of those three statuses, and no two
  statuses share a reason.
* Every `LinkStatus` value is either `OK` or has a refusal reason: no status
  is unhandled.

---

## Development environment

### REQ-DEV-001 — Hardware-free build, run and test

**Statement:** The complete system SHALL build, run in simulation and pass its
test suite on macOS with no Raspberry Pi, Arduino, camera, servo, water actuator
or GPIO present.

**Acceptance:**

* `scripts/check.sh` passes on a clean macOS checkout.

### REQ-DEV-002 — Deterministic tests

**Statement:** The test suite SHALL be deterministic: no wall-clock dependence,
no unseeded randomness, no network access, no reliance on test ordering.

**Acceptance:**

* Repeated runs, including shuffled runs, produce identical results.

### REQ-DEV-003 — Hardware-in-the-loop tests are segregated

**Statement:** Tests requiring physical hardware SHALL be excluded from the
default test suite and from CI.

**Acceptance:**

* The default `ctest` invocation runs no test that opens a serial port or a
  camera device.

---

## Configured parameters

Every tunable value in one place. The requirement is authoritative; this table
is a reference. Defaults are the values the system ships with.

| Parameter                  | Default        | Requirement   |
| -------------------------- | -------------- | ------------- |
| Confirmation frames        | 3              | `REQ-TRK-002` |
| Track association radius   | *uncalibrated* | `REQ-TRK-007` |
| Verification deadline      | next frame     | `REQ-TRK-004` |
| Detection throughput       | 5 FPS          | `REQ-DET-003` |
| Camera horizontal FOV      | *uncalibrated* | `REQ-AIM-001` |
| Camera vertical FOV        | *uncalibrated* | `REQ-AIM-001` |
| Boresight offset           | *uncalibrated* | `REQ-AIM-001` |
| X angle envelope           | −90° … +90°    | `REQ-AIM-002` |
| Y angle envelope           | 0° … +45°      | `REQ-AIM-002` |
| Default envelope           | empty          | `REQ-AIM-002` |
| Maximum fire duration      | 500 ms         | `REQ-SAF-001` |
| Cool-down after firing     | 2 s            | `REQ-SAF-005` |
| Maximum engagement rate    | 6 / minute     | `REQ-SAF-005` |
| Exclusion zone             | none configured, at most one | `REQ-SAF-003`, `REQ-SAF-006` |
| Frame pixel format         | RGB888, not configurable | `REQ-DET-004` |
| Track retention            | none; discarded on a missed frame | `REQ-TRK-009` |

*uncalibrated* means the value is a property of the physical rig and is
recorded during bring-up. Until then the empty default envelope prevents
firing, so an uncalibrated system is inert rather than dangerous.

The last two rows are parameters the system deliberately does **not** have.
They are listed so that nobody adds them back: one declared pixel format keeps
fixtures simple (`REQ-DET-004`), and immediate track discard was shown to
change no confirmation outcome, so a retention period would be a dial with
nothing on the other end of it (`REQ-TRK-009`, ADR-0010).

The reference installation configures no exclusion zone (Q9), and an
installation may configure at most one (Q14). The mechanism is still required
and still tested, because the zone is a property of where the rig is mounted,
and rigs get moved.

## Open questions

Unresolved specification gaps. **Agents must not invent answers to these** —
raise them with the maintainer.

*None outstanding.* Every question raised so far has been resolved; see below.
Add new rows here rather than guessing.

| #  | Question | Blocks |
| -- | -------- | ------ |
| —  | —        | —      |

### Resolved

Q1 to Q16 were raised while writing and decomposing the specification. Q17 to
Q19 were raised by the **test-engineer**, while encoding the acceptance
criteria as executable tests, at the points where a test could not be written
because the boundary was unstated. That provenance is worth keeping: it is
evidence that writing the tests first does the job it is there to do.

| #  | Question                                          | Resolution                                                                 |
| -- | ------------------------------------------------- | -------------------------------------------------------------------------- |
| Q1 | Multiple simultaneous pigeons                     | Engage the largest detection, deterministic tie-break. `REQ-TRK-008`, ADR-0003 |
| Q2 | Frame rate and verification timeout               | 5 FPS; verification uses the next frame, no clock. `REQ-DET-003`, `REQ-TRK-004`, ADR-0005 |
| Q3 | Coordinate frames and calibration                 | Camera boresighted on the rig; key/value config parsed outside `core/`. `REQ-AIM-001`, `REQ-AIM-003`, ADR-0004 |
| Q4 | Mechanical angle limits                           | X ∈ [−90°, +90°], Y ∈ [0°, +45°]; empty by default. `REQ-AIM-002`            |
| Q5 | Maximum fire duration and cool-down               | 500 ms burst, 2 s cool-down, enforced on the Arduino too. `REQ-SAF-001`, `REQ-SAF-005` |
| Q6 | Maximum engagement rate                           | 6 engagements per minute. `REQ-SAF-005`                                     |
| Q7 | Spatial association across frames                 | Associate detections into tracks; confirm one track. `REQ-TRK-007`, ADR-0003 |
| Q8 | Y-axis datum                                      | Y = 0° is the horizon; elevation is never negative. `REQ-AIM-002`            |
| Q9 | Exclusion zone for this installation              | None configured. The mechanism is retained and tested. `REQ-SAF-003`        |
| Q10 | Frame pixel encoding                             | Packed RGB888, 8 bits per channel, row stride in bytes. `REQ-DET-004`       |
| Q11 | Retention of a track that misses a frame         | Discarded immediately; retention changes no confirmation outcome. `REQ-TRK-009`, ADR-0010 |
| Q12 | Identity of the verification frame's detection   | The **same track** must be re-detected; otherwise `TARGET_LOST`. `REQ-TRK-010`, ADR-0009 |
| Q13 | State after the fire command                     | Return to `SEARCHING`; the same track may be re-engaged after the cool-down. `REQ-TRK-011` |
| Q14 | Exclusion zone geometry and cardinality          | One axis-aligned rectangle in angle space, at most one per installation. `REQ-SAF-006` |
| Q15 | Whether a failed fire command counts             | Any command that was **transmitted** counts, whatever the reply. `REQ-SAF-007`, ADR-0011 |
| Q16 | Resetting the count when an engagement ends      | Reset, after firing and after abandoning; each engagement earns its own confirmation. `REQ-TRK-012`, ADR-0013 |
| Q17 | Whether the association radius is inclusive      | Inclusive: `distance <= radius` associates. `REQ-TRK-007`                   |
| Q18 | When a duration has elapsed, and window edges    | `elapsed >= duration`; windows are half-open. One rule for both limits. `REQ-SAF-005`, ADR-0014 |
| Q19 | Link statuses other than `OK`                    | Only `OK` grants; every other status refuses with its own reason. `REQ-SAF-008`, ADR-0015 |

### Corrections

Defects in this document found after approval, repaired and recorded rather
than silently edited: a requirement that has changed is a requirement somebody
may already have built against.

| #  | Defect                                                                                                                                                                                                                                 | Repair                                                                                                                                                                                                                                              |
| -- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| C1 | `REQ-TRK-007`'s acceptance bullet "two detections in successive frames beyond the association radius produce two tracks, each with a count of one" cannot hold under `REQ-TRK-009`: if the detections are in successive frames, the first track is discarded the moment it goes undetected, so the two tracks never coexist. Found by the test-engineer, who could not encode it. | Reworded as a statement about successive frames that is true under immediate discard: a detection beyond the radius from every existing track starts a new track with a count of one, while the track it failed to match is discarded. No same-frame bullet was added; the requirement is about association across frames. |
