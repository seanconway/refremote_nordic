# Wireless Single-Operator Officiating System
# Functional Specification

**Version 2.1**
**Companion document:** *Project Scope v1.1*

---

## 1. About This Document

This document specifies **how the system behaves**. Market definition, boundaries, operating assumptions, design principles, external officiating responsibilities, and release scope are in the *Project Scope* document and are not repeated here except where a behaviour depends directly on them.

This document does not prescribe the wireless link, wire protocol, message framing, or transport mechanisms. Sections 7 and 8 state required behaviour of the communication layer and scoreboard application, against which implementation can be evaluated.

**Principles referenced throughout** (defined in *Project Scope* §7): operable by feel; the referee owns rule application; pure front-end and offline; ruleset logic lives in one place; simplicity over coverage.

---

## 2. Division of Responsibility

### 2.1 The Remote

**The remote is stateless with respect to the match.** It reports which button was pressed on which wrist and with what gesture, and renders whatever LED and haptic output it is commanded to render. It holds no match state, no ruleset knowledge, no clock, and no ownership tracking.

The only state a remote holds is device-local:

| Remote-held state | Purpose |
|---|---|
| Current LED render | So indicators persist between commands |
| Link status | Locally detected; drives `LED_LINK` and the link-lost haptic |
| Battery state of charge | Locally measured; drives `LED_PWR` and the low-battery haptic |

### 2.2 The Scoreboard

The scoreboard holds all ruleset logic and all match state. Its role in each case is to compute, track and display, never to act:

| Responsibility | What it does | What it does not do |
|---|---|---|
| Point values and totals | Maintains and displays each athlete's score | Never awards a point |
| Penalty, caution, warning and advantage counts | Maintains and displays counts and ladder position | Never applies the escalation consequence |
| Secondary clock | Tracks ownership, accrues time, resets at configured boundaries, commands the heartbeat | Never awards the resulting point |
| Activity clock | Runs the count-down, signals expiry | Never awards the passivity point |
| Phase and period structure | Advances phases with the clock, notifies at boundaries | Never gates or rejects input |
| Freestyle criteria | Groups scoring actions, evaluates and displays criteria | Never declares a winner |
| Pending-choice flag | Displays the referee-set state | Never sets or changes it |

### 2.3 Officiating Sets

A dongle and its two remotes constitute an **officiating set**, paired in firmware at manufacture or provisioning. The binding is fixed.

This supports parallel operation at multi-mat events: sets cannot drift out of association, no field pairing procedure exists to be performed incorrectly, and there is no state in which a remote is unsure which dongle it belongs to. Each set carries matching serial-number identification across dongle and both remotes.

**Cross-system association would be a scoring-integrity failure**, not an inconvenience — a remote accepting commands from, or delivering input to, a neighbouring mat would corrupt two matches at once and would not necessarily be obvious to either referee. Firmware pairing must be cryptographically enforced rather than proximity-based, and the bound set identity is displayed at the scoreboard before each match (§12.3).

---

## 3. Physical Interface

### 3.1 Button Layout and Tactile Discrimination

```
              [ADD_POINT]
        [FORWARD]        [BACKWARD]
             [TOGGLE_CLOCK]
      (outer)              (inner)
        [F2]                 [F1]     <- Red remote (left wrist)
        [F1]                 [F2]     <- Green remote (right wrist)
            [REMOVE_POINT]
```

A **vertical centre column flanked by two lateral pairs**:

- **Centre column, top to bottom:** `ADD_POINT`, `TOGGLE_CLOCK`, `REMOVE_POINT`.
- **Upper lateral pair:** `FORWARD` left, `BACKWARD` right.
- **Lower lateral pair:** `F1` inside, `F2` outside. Mirrored across the two remotes so both sit at symmetric anatomical positions on either wrist.

Tactile discrimination is achieved through **button shape and position relative to the central `TOGGLE_CLOCK` button**:

| Button | Shape | Size |
|---|---|---|
| `TOGGLE_CLOCK` | Circular | Larger than all others |
| `ADD_POINT`, `REMOVE_POINT` | Circular | Standard |
| `FORWARD`, `BACKWARD`, `F1`, `F2` | Vertically elliptical | Standard |

The oversized circular centre button is the tactile datum. From it the referee resolves the remaining six by direction and by the circular-versus-elliptical distinction.

**Accidental actuation.** All buttons are slightly recessed. Every clearing and resetting action requires a hold rather than a press.

### 3.2 LED Layout

| ID | Type | Ownership | Function |
|---|---|---|---|
| `LED_PWR` | RGB | Ruleset-agnostic | Power on, battery state of charge, charging state |
| `LED_LINK` | RGB | Ruleset-agnostic | This remote's link state |
| `LED_F1` | RGB | Ruleset-specific | State of the F1-tracked item for this athlete |
| `LED_F2` | RGB | Ruleset-specific | State of the F2-tracked item for this athlete |

**All four indicators are RGB**, exceeding initial firmware requirements to preserve flexibility after the hardware is fixed, at negligible incremental cost.

`LED_F1` and `LED_F2` are physically adjacent to their corresponding buttons.

### 3.3 Haptic

One ERM per remote, driven by a haptic driver IC.

The ERM is selected primarily for **amplitude**: time expiration must be unmistakable to a referee engaged with athletes on the mat, and that notification is the direct replacement for the towel tapper. It is a stated assumption that the ERM, with the driver IC, is also capable of the lower-amplitude functions — per-press acknowledgement within the latency budget, and a per-second heartbeat that is both countable and clearly weaker in amplitude than an acknowledgement tap (§11.1).

**Contingency:** if that assumption fails in prototyping, an LRA or dual-motor design will be revisited.

### 3.4 Charging

Each remote charges via **USB-C**. Charging state is reflected in `LED_PWR`.

---

## 4. Input Model

### 4.1 Gesture Model

| Gesture | Definition |
|---|---|
| `PRESS` | Press and release before the hold threshold |
| `HOLD` | Continuous depression beyond the hold threshold |
| `HOLD_REPEAT` | Continued repetition while held (clock adjust only) |

No multi-tap. No chording. No modal remapping. A referee should never have to think about *how* they pressed a button.

### 4.2 Where Input Timing Lives

**Gesture discrimination timing lives in the remote's firmware.** The thresholds separating press from hold, and governing hold-repeat, are firmware constants — not scoreboard-configurable. Classification must be immediate and identical regardless of link state.

**Defaults:** debounce 15 ms; hold threshold 600 ms; hold-repeat interval 150 ms.

### 4.3 Scoring Action Grouping

The **scoreboard** applies timing-based logic to consecutive presses arriving in close succession on the same remote, grouping them into a single scoring *action*. This is scoreboard-side interpretation of already-classified presses; it changes nothing about remote behaviour and nothing about the resulting score.

**Purpose — freestyle.** Freestyle tiebreak criteria turn on the value of the single highest technical action, not the point total. Without grouping, a four-point throw entered as four presses is indistinguishable from four separate one-point actions.

**Best-effort, referee-verified.** Grouping is a heuristic and will sometimes be wrong — a referee who pauses mid-sequence produces two actions where one was intended; two rapid separate scores may merge. The remote interface offers no intuitive way to correct grouping mid-match, and adding one would violate the operable-by-feel principle. The referee reviews the action log at match end, when criteria decisions are made and there is time to check.

**Explicitly not used in folkstyle.** No inference about riding time, control, or any other tracked state is ever drawn from scoring input.

**General rule:** action grouping may inform display, logging and criteria evaluation. It may never actuate a tracked state or alter a score.

---

## 5. Button Functions

### 5.1 Universal Functions

Identical across every ruleset. These are the referee's muscle memory.

| Button | Remote | `PRESS` | `HOLD` |
|---|---|---|---|
| `TOGGLE_CLOCK` | Both | Start / stop main match clock | Reset current period clock to full duration |
| `ADD_POINT` | Both | +1 point to this remote's athlete | — |
| `REMOVE_POINT` | Both | −1 point to this remote's athlete | — |
| `FORWARD` | Red | Main clock −1 second | −1 s repeating |
| `BACKWARD` | Red | Main clock +1 second | +1 s repeating |
| `FORWARD` | Green | Next period / phase | Jump to last period |
| `BACKWARD` | Green | Previous period / phase | Jump to first period |

`TOGGLE_CLOCK` is present on both remotes and acts on the same single main clock, for convenience regardless of hand position.

**Clock-state awareness.** The referee is the only agent that starts or stops the main clock, and every press returns an acknowledgement tap. That is sufficient awareness; no separate wrist indication of clock state is required, in any ruleset.

**Score floor.** `REMOVE_POINT` will not drive a score below the ruleset's configured minimum. Presses beyond the floor are acknowledged but have no effect.

**Direction convention.** The match clock counts down, so `FORWARD` — advancing through match time — subtracts from it, and `BACKWARD` — rewinding — adds to it. This was inverted in earlier revisions of this document, which is why it was inverted in the implementation; both are corrected together.

**Secondary clock follows the main clock.** The one situation this control exists for is the referee who is late stopping the clock and needs to wind it back — and, if they overcorrect, wind it forward again. Whenever `FORWARD`/`BACKWARD` adjusts the main clock and the secondary clock is currently owned, the same correction is applied to it, because the interval being corrected is one the secondary clock was also live for: a count-down secondary clock (the activity clock) is adjusted by the same signed amount as the main clock; a count-up secondary clock (riding time) is adjusted by the opposite sign, applied to whichever athlete currently owns it, and floored at zero. An unowned secondary clock is untouched, since it was not accruing during the interval being corrected.

### 5.2 Default Assignment Rationale

Two defaults are intuitions rather than tested findings, both marked for post-MVP remapping: clock adjustment on the red remote with period navigation on the green, so two similar navigation functions do not compete for the same finger positions; and F1/F2 mirrored rather than fixed by absolute position.

### 5.3 Repeated-Press Scoring

Scoring is single-increment. Multi-point actions are entered as repeated presses — a three-point takedown is three presses, a four-point near fall four, a five-point throw five.

The alternative — dedicated buttons or gesture-encoded magnitude — either exceeds the button budget or violates the operable-by-feel principle.

Viability rests on immediate per-press haptic confirmation: each press produces a distinct tap within the acknowledgement latency budget (§7.3), so the referee feels the count accumulate without looking. **Acknowledgement fidelity is a functional requirement of the scoring interface, not a convenience feature**, and is protected against interference from the heartbeat by §11.1.

### 5.4 Function Button Roles

**Shape A — Athlete-attributed secondary clock (wrestling).** A per-athlete clock, owned by one athlete at a time, with a per-second heartbeat on the owning athlete's remote (§6).

**Shape B — Counter (BJJ, freestyle cautions).** A per-athlete integer counter. The scoreboard displays count and ladder position; the referee applies the consequence.

**Shape C — Tri-state flag (folkstyle).** A state owned by one athlete, the opposing athlete, or neither. Set only by the referee.

### 5.5 Function Button Behaviour by Shape

| Shape | `PRESS` (on an athlete's remote) | `HOLD` |
|---|---|---|
| Secondary clock | If unowned or owned by the opponent: assign to this athlete and begin accrual. If already owned by this athlete: deassign — accrual stops, accumulated value retained | Reset accumulated value and deassign |
| Counter | +1 to this athlete's counter | −1 to this athlete's counter (correction) |
| Tri-state flag | If unowned or owned by the opponent: assign to this athlete. If already owned by this athlete: clear to unowned | — |

Pressing on the opposite remote transfers ownership directly, with no need to deassign first. This is the most important interaction in folkstyle riding time, where control changes hands repeatedly and rapidly.

**Deassign versus pause.** Deassign is a referee action meaning no athlete currently holds the state — wrestlers have returned to neutral. Pause is automatic and happens whenever the main clock stops while ownership is retained.

### 5.6 Function Button Assignment by Ruleset

| Ruleset | F1 | F2 |
|---|---|---|
| **Folkstyle — NCAA (riding time)** | Riding time (secondary clock) | Pending choice (tri-state flag) |
| **Folkstyle — NFHS (no riding time)** | Inert | Pending choice (tri-state flag) |
| **Freestyle / Greco-Roman** | Activity clock (secondary clock) | Caution count (counter) |
| **IBJJF / SJJIF** | Advantages (counter) | Penalties (counter) |
| **ADCC** | Warnings (counter) | Negative points (counter) |
| **Other no-gi (NAGA, JJWL etc.)** | Advantages (counter, if used) | Penalties (counter) |

**Assignment convention.** F1 and F2 are equally accessible; assignment is governed by semantic consistency rather than reach:

- **F1 carries what accrues *to* an athlete's benefit** — riding time, advantages, and the activity clock assigned to the athlete under obligation.
- **F2 carries what counts *against* an athlete, or what they are owed** — cautions, penalties, negative points, warnings, and the folkstyle pending choice.

**Inert buttons are fully inert.** No action, no haptic, no LED response. A rejection signal would be more confusing than silence: a referee knows which ruleset they are officiating.

Folkstyle NFHS leaves F1 inert deliberately rather than assigning it to warning counts, so that folkstyle behaves identically across variants.

---

## 6. The Secondary Athlete Clock

Riding time and the freestyle activity clock are one mechanism with opposite polarity, which is what allows a single function button and a single LED to serve both.

### 6.1 Common Behaviour

- Exactly one athlete owns the clock at a time, or neither. **Ownership is held by the scoreboard**, not the remote.
- Ownership is assigned by pressing F1 on that athlete's remote, transferred by pressing F1 on the other, deassigned by pressing F1 again on the owning athlete's remote, and the accumulated value reset by holding F1.
- **`LED_F1` on both remotes is lit, in the owning athlete's colour, whenever that athlete owns the clock**, accruing or paused; off on both when unowned (§10.3). Ownership is the question the LED answers, and either wrist answers it.
- **The per-second heartbeat fires only while the clock is actually accruing.** Its presence or absence tells the referee whether the clock is running; the LED is not asked to carry that distinction.
- The clock pauses automatically whenever the main match clock stops, and resumes when it restarts. Ownership is retained across the pause.
- The system never awards the resulting point.

### 6.2 Heartbeat Generation

**Each heartbeat tap is commanded individually by the scoreboard.** The remote holds no ownership state and runs no timer; it taps when told to.

The heartbeat's function is to tell the referee that *a* secondary clock is running and that *this* athlete owns it. It is a state indicator, not a metronome — nothing in officiating depends on a beat landing at a precise offset from the match clock, and a referee cannot detect tens of milliseconds of jitter in a one-second interval. Architectures that hold ownership state on the remote or dongle to improve beat regularity solve a problem the product does not have, while introducing one it cannot tolerate: a remote beating on stale state reports one thing on the referee's wrist while the scoreboard reports another, quietly and with no self-correcting mechanism.

Per-beat commanding eliminates that failure mode and carries two secondary benefits:

- **The heartbeat doubles as continuous end-to-end liveness proof**, active during exactly the periods when the referee relies on the system most. If the link drops, the heartbeat stops — the correct and immediately legible failure behaviour.
- **The remote stays fully stateless with respect to the match.**

Message cost is modest and largely absorbed by connection activity already scheduled to meet the acknowledgement latency budget.

### 6.3 Riding Time — Count-Up Polarity (folkstyle, NCAA)

**Accrual and ownership**

- Time accrues to the athlete in control. Ownership transfer moves accrual to the other athlete. Deassignment stops accrual and retains the accumulated differential, for when wrestlers return to neutral.
- **State changes only on referee input.** Nothing is inferred from scoring, position, or elapsed time.
- **Heartbeat:** one reduced-amplitude tap per second of accrual on the controlling athlete's remote, letting the referee confirm without looking that time is accruing to the correct athlete.
- **No threshold notification.** A mid-match signal would fire repeatedly and meaninglessly where control changes hands near the threshold.

**Behaviour across period boundaries**

| Boundary | Accumulated differential | Ownership |
|---|---|---|
| Between regulation periods | **Carries over** — the differential is cumulative across the whole of regulation | **Deassigns** — the referee re-establishes it after the restart |
| Regulation into overtime | **Resets to zero** | Deassigns |
| Between periods within an overtime round | Carries over | Deassigns |

Ownership deassigns at every period boundary because periods restart from a chosen position, and which athlete has control at the restart is a fresh determination the referee makes and enters. Carrying ownership across a restart would risk accruing silently to the wrong wrestler.

The regulation-to-overtime reset reflects current NCAA rules, under which riding time accrued in regulation does not carry into overtime although all other accumulated points, penalties, cautions, warnings, timeouts and injury time do. The riding-time point itself is awarded at the conclusion of regulation and cannot be awarded before then.

**Riding time continues to be kept in overtime**, accruing fresh from zero. Its consequence there differs from regulation — a net advantage of as little as one second can decide a tied overtime round, rather than the one-minute threshold that earns a point in regulation. **The system applies neither consequence.** It displays the differential; the referee applies the rule in force.

Because these thresholds and carry-over rules have changed across NCAA rules cycles, they are expressed as per-period configuration (§12.2) rather than encoded behaviour.

**Display**

Favour and ownership are two different facts and the display answers them separately, because they are not always the same athlete — the wrestler ahead on accumulated riding time is not necessarily the wrestler currently accruing it.

- The net differential is displayed **centred beneath the main clock**, not beside either athlete's score. Positioning it under one score answered "who does this favour" at the cost of "who is it counting for right now" — unreadable exactly when the clock was stopped and no accrual could be observed to infer direction from.
- **The readout is coloured in the athlete it currently favours** — athlete red or athlete green — so favour is still read at a glance, from the centre rather than from a side.
- **An independent coloured arrow, in the owning athlete's colour, points to that athlete's side of the board**, whether or not that athlete is the one currently favoured. This is the one place the two facts can disagree: the arrow can point one way while the readout is coloured for the other athlete, and both are simultaneously true.
- Net effect while an athlete accrues against a standing deficit: the readout counts down in the *other* athlete's colour as the deficit closes, then — the instant accrual overtakes it — switches colour to the accruing athlete and counts up from zero. The arrow does not move or change colour through that transition; only the readout does, because only favour changed.

The display persists after time expires. No reminder is issued and none is needed — a match ending on the clock always ends with the referee reading the scoreboard, and a match ending by fall ends the bout outright, where riding time is not a factor.

The freestyle/Greco-Roman activity clock (§6.4) is unaffected by this: it already reads unambiguously, since it is shown only on the remote's side that currently owns it and there is no favour/ownership distinction to separate.

### 6.4 Activity Clock — Count-Down Polarity (freestyle / Greco-Roman)

- Counts down from the configured duration (30 seconds default).
- Assigned to the athlete placed on activity time — the athlete under obligation to score.
- **Heartbeat:** one reduced-amplitude tap per second remaining on the obligated athlete's remote. The heartbeat is itself the countdown — the referee can feel the time remaining — so no separate advance warning is provided.
- If the obligated athlete scores during the window, the referee deassigns by pressing F1 again on that athlete's remote. Deassignment resets a count-down clock to its full configured duration.
- **On expiry without a score**, both remotes receive a distinct expiry buzz. The referee awards the point to the opponent manually. The clock deassigns and resets on expiry.
- **At period end**, an activity clock still running is deassigned and reset. The obligation does not carry across a period boundary.

### 6.5 BJJ

No secondary clock. The three-second positional control requirement is counted mentally by the referee. F1 is instead assigned to a counter (§5.6).

---

## 7. Communication Layer Requirements

States **what must be supported**, not how.

### 7.1 Remote → Scoreboard

| Information | Purpose |
|---|---|
| Button identity and gesture | The only match-affecting input path |
| Originating remote identity | Determines which athlete the input applies to |
| Battery state of charge | Drives the scoreboard battery indicator |
| Periodic liveness signal | Link supervision |

### 7.2 Scoreboard → Remote

| Information | Purpose |
|---|---|
| Input acknowledgement | Drives the confirmation haptic tap |
| Indicator state | Function-indicator ownership or count; drives `LED_F1` and `LED_F2` |
| Heartbeat command | One per beat, addressed to the owning athlete's remote (§6.2) |
| Discrete haptic commands | Activity clock expiry, main clock warning, period and match expiry, phase boundary |
| Periodic liveness signal | Link supervision |
| Configuration | Haptic intensity, LED brightness |

### 7.3 Functional Requirements

- **Ruleset-agnostic.** No message carries ruleset meaning. A new ruleset must require no change to the communication layer.
- **No lost or duplicated inputs.** A silently dropped or doubled scoring input corrupts the match score with no external indication. The layer must guarantee exactly-once delivery of match-affecting inputs, or surface a failure the referee can see. This is heightened by repeated-press scoring, where one dropped press in a sequence of four produces a plausible wrong score rather than an obvious fault.
- **Heartbeat commands are best-effort.** An occasional dropped heartbeat is tolerable and must not be retried at the expense of latency for anything else. A *sustained* absence is meaningful and correct (§6.2).
- **Order preservation.** Scoring events are attributed in the order the scoreboard receives them. Remotes do not timestamp. See §15.1.
- **Acknowledgement latency.** The confirmation haptic must fire within approximately 120 ms of the press, **inclusive of any retries**. Where interference prevents delivery within budget, the failure must be surfaced rather than absorbed by extended retry.
- **State is idempotent, events are not.** Indicator state may be re-sent freely; button events must be delivered exactly once.
- **Full indicator state must be assertable on demand**, so that any remote coming onto the link — reconnecting after a transient interruption, or newly substituted (§8.6) — can be brought to correct rendering in one operation without waiting for the next state change.
- **Link loss must be unmistakable.** Both a remote-side indication and a scoreboard-side per-remote indication.

### 7.4 Radio Performance

**Range.** Reliable operation to 40 ft between either remote and the dongle. The link budget must account for **body shadowing**: the remotes are worn on the wrists of a referee circling the mat, so for much of a match one remote has the referee's torso between it and a dongle at the scoreboard table. Attenuation through the body at 2.4 GHz is the governing case, not free-space distance.

**Density.** Up to 30 systems — 90 devices — simultaneously, plus spectator devices and venue Wi-Fi:

- No degradation of the latency budget attributable to neighbouring systems.
- **No cross-system association under any circumstance** (§2.3).

**Interference tolerance.** The layer must maintain latency and reliability under sustained interference, and where it cannot, surface degradation to both referee and scoreboard rather than silently dropping or delaying input.

**Degradation must be visible before it is total.** A link deteriorating but not yet failed is the most dangerous state, because inputs may be delayed or lost while the referee still believes the system is working.

---

## 8. Scoreboard Application Requirements

### 8.1 Clock Integrity

- **The clock must be derived from monotonic elapsed-time measurement, never from accumulated timer ticks.** Displayed time is computed by subtraction from a recorded start reference on each render. Rendering may stutter; the clock value must never drift.
- **The time reference must be immune to wall-clock changes** — time-synchronisation corrections, daylight-saving transitions, or a user adjusting the system clock must not affect a running match.
- **Discontinuities must be detected and surfaced.** If the application observes an implausible gap in elapsed time, it must halt the clock and require explicit referee confirmation before resuming rather than silently absorbing the gap.

### 8.2 Match State Durability

The application holds the only copy of match state.

- Match state must be **persisted locally** as it changes, using storage that survives page reload and browser restart.
- On load, if an unfinished match is found, the application must offer to restore it, showing enough detail for the operator to confirm it is the right match.
- Persistence must not depend on network availability.

### 8.3 Fault Detection and Self-Reporting

Link loss is well covered: the remote detects it locally and signals unmistakably. The dangerous case is an application **running but wedged** — an unhandled fault leaving the interface frozen while the serial connection remains nominally alive, so the remotes see only an ambiguous silence.

- The application must run an **internal watchdog**. On detecting that its own match-state processing has stalled, it must **deliberately drop the serial connection**, converting an ambiguous silence into an unambiguous link-loss indication on both remotes.
- The referee thereby receives the same clear signal for an application fault as for a radio fault: `LED_LINK` off and the repeating link-lost buzz.
- The fault must also be surfaced visibly where the display is still rendering.

### 8.4 Display

The display serves the referee, athletes, coaches and spectators simultaneously, so it must be legible from across a competition hall while still carrying the operational detail the referee needs at specific moments. This is resolved by tiering.

**Primary tier — always visible, legible at distance:**

- Athlete names and colours
- Score
- Period and match clock
- Secondary clock, shown only when active, positioned per §6.3
- Link and battery status for both remotes

**Secondary tier — collapsible detail panel, summoned on demand:**

- Penalty, caution, warning and advantage counts, with ladder position
- Pending-choice flag state
- Phase
- Scoring action log
- Criteria evaluation
- Set identity and diagnostic detail

Collapsing the detail panel must never reduce the primary tier's legibility, and no primary-tier element may be relegated to the panel.

Link and battery status sit in the primary tier despite not being of interest to spectators, because their absence is what the referee needs to notice immediately and without prompting.

### 8.5 Host Platform

- Chromium-based browser, desktop.
- Local serial access to the dongle, granted once per machine and persisted.
- Display sleep and machine suspend disabled for the session.

The serial transport should be implemented behind a thin abstraction so that a desktop-packaged build remains an inexpensive future option.

### 8.6 Officiating Set Substitution

A remote failure mid-match — depleted battery, hardware fault, physical damage — must not end the bout. The procedure is defined in *Project Scope* §8.6. Requirements on the application:

- Match state must be **fully retained across a change of connected dongle**: score, clock position, period, secondary-clock ownership and accumulated value, counter values, pending-choice flag, and the action log. The match is not restarted, re-entered, or reconstructed.
- The application must accept a different dongle mid-match and re-establish operation without operator reconfiguration of the match.
- **On connection, the application asserts full indicator state to both new remotes** (§7.3), so `LED_F1` and `LED_F2` render current match state immediately rather than waiting for the next state change. The referee re-enters nothing.
- The substitution must be visible in the match record.

Asserting rather than clearing follows directly from where state lives (§2.1, §2.2). The scoreboard is the sole authority on secondary-clock ownership and flag state; the remotes are render surfaces for it. A substituted remote is in exactly the position of a remote that has just reconnected — it has no state of its own to be stale, and the correct state is already held one node away. Requiring manual re-entry would add a clearing behaviour, an extra step in a time-pressured procedure, and an interval in which the wrist indicators contradict the scoreboard.

Substitution is by complete set because sets are firmware-paired (§2.3).

---

## 9. Folkstyle Pending-Choice Flag

### 9.1 What It Is

A referee-set memory aid indicating which wrestler is expected to have choice of starting position in the third period. That is the entire scope: a tri-state flag — red, green, or unset.

### 9.2 Behaviour

- Pressing F2 on a wrestler's remote sets the flag to that wrestler and lights `LED_F2` on that remote.
- Pressing F2 on the other remote moves the flag and the indicator there.
- Pressing F2 again on the remote holding the flag clears it.
- The flag persists until the referee changes it.
- The scoreboard displays the flag state in the detail panel (§8.4).

### 9.3 What It Deliberately Does Not Do

**The system never sets, moves, clears, or infers this flag.** No state machine, no period-transition logic, no overtime derivation, no divergence warning.

Encoding the choice sequence would mean branching logic across NFHS and NCAA procedures, state-association variants, deferral, bad-time corrections, and unsportsmanlike-conduct overrides. The determining criterion has itself changed across NCAA rules cycles — recent rulebooks have granted tiebreaker choice on the basis of who scored the first offensive points in regulation, which is a different rule from earlier third-period-choice derivations. Encoding a rule that moves between cycles, in exchange for automating a determination the referee is already making, is a poor trade. Worse, an automatically-actuated flag that is wrong is more dangerous than no flag at all, because the referee may trust it.

The exact use of the flag is at the referee's discretion.

---

## 10. Indicators

### 10.1 Battery — `LED_PWR`

| State of charge | Indication |
|---|---|
| 66–100% | Green |
| 33–66% | Yellow |
| 0–33% | Red |

Three bands, no separate low-battery blink state — a referee glances at colour, not a blink rate. `LED_PWR` is always solid; there is no `OFF` state for it, since a battery reading always has some value. Charging state is also reflected here.

### 10.2 Link — `LED_LINK`

| State | Indication |
|---|---|
| Not connected to dongle | Red solid |
| Connected to dongle, dongle not connected to scoreboard | Yellow solid |
| Connected to dongle and dongle connected to scoreboard | Green solid |

Always solid — no `OFF` state. The repeating double buzz (§11: "Link lost") still fires whenever the indicator is not green — red or yellow both count as lost for the buzz's purposes — unchanged from the two-state version this replaces.

### 10.3 Function Indicators — `LED_F1`, `LED_F2`

| Shape | Rendering |
|---|---|
| Secondary clock | Lit on **both remotes** whenever an athlete owns the clock, in that athlete's colour (athlete red or athlete green); off on both when unowned. Running state is carried by the heartbeat, not the LED |
| Counter | Off at zero; solid in the role's configured colour when non-zero. This remote's own count only — no cross-remote rendering |
| Tri-state flag | Lit on **both remotes** in the holding athlete's colour when held; off on both when unowned |
| Inert | Always off |

Counter rendering is deliberately binary. The exact count is on the scoreboard, and a referee mid-match is looking at the mat rather than their wrist. The wrist LEDs answer only: *does this athlete currently hold this state?*

**Secondary clock and tri-state flag render on both wrists identically, in the holder's athlete colour — not the per-role colour.** A referee glancing at either wrist sees the same thing: who currently holds this state, by the same red/green they already use to identify corners everywhere else. This replaces the earlier "each remote shows only its own athlete's ownership" behaviour — a referee previously had to check the *other* wrist to learn who held a contested state; now either wrist answers it.

**Counter is the one shape colour-per-role (§12.2) still governs.** It has no cross-remote holder — each remote tracks its own athlete's count independently — so there is nothing for an athlete colour to represent, and the ruleset-configured role colour (distinguishing, say, a caution counter from an advantage counter) is still what a referee needs there.

---

## 11. Haptic Feedback

| Event | Pattern | Source |
|---|---|---|
| Input registered | Single short tap, **full amplitude** | Scoreboard |
| Secondary clock accrual or countdown | Single tap per second, **distinctly reduced amplitude**, on the owning athlete's remote | Scoreboard, per beat |
| Activity clock expiry | Distinct buzz, both remotes | Scoreboard |
| Main clock, five seconds remaining | Single distinct warning | Scoreboard |
| Period or match clock expiry | Long buzz, both remotes | Scoreboard |
| ADCC phase boundary | Distinct signal, both remotes | Scoreboard |
| Link lost | Repeating double buzz every three seconds | Remote, locally |
| Low battery threshold crossed | Triple buzz, once only | Remote, locally |

**On distinguishability.** Distinct waveforms are applied, but the design does not depend on the referee telling them apart by feel alone. Context disambiguates: the referee knows whether they have started an activity clock, and the activity clock runs down before the period it sits inside.

**No advance warning on the secondary clock.** The count-down heartbeat is itself a countdown — one tap per second remaining — so the referee already feels the time running out. An additional warning would add a pattern to distinguish without adding information.

**Low battery** is a single triple-buzz at threshold crossing, not repeated. A periodic warning accelerates the drain it warns about, and a triple pattern avoids confusion with single-buzz notifications.

### 11.1 Protecting Acknowledgement from the Heartbeat

Repeated-press scoring depends on the referee counting acknowledgement taps by feel (§5.3). During folkstyle riding time the same motor delivers a heartbeat every second, and a four-press sequence takes roughly a second — so a heartbeat will frequently fall inside a scoring burst. A miscounted near fall is precisely the failure the acknowledgement design exists to prevent.

Two mitigations apply together:

**Amplitude separation.** The heartbeat is rendered at a distinctly lower amplitude than an acknowledgement tap — enough that the two are unambiguously different sensations, not merely different in principle. The heartbeat need only be perceptible; the acknowledgement must be unmistakable.

**Burst suppression.** The heartbeat is suppressed for a short window following any button press, resuming once input activity ceases. During a scoring burst the referee therefore feels only acknowledgement taps.

Suppression is bounded so that a prolonged input sequence — a held clock-adjust repeating at 150 ms, for instance — cannot silence the heartbeat indefinitely, which would falsely indicate that accrual had stopped. Window duration is a tuning parameter (§15.6).

### 11.2 Power Budget

**Target:** a full ten-hour tournament day on a single charge with heartbeat enabled, assuming charging between sessions.

The two significant consumers are the haptic motor and the radio; the motor is expected to dominate. Amplitude separation helps here — a reduced-amplitude heartbeat draws meaningfully less than a full-amplitude tap, and the heartbeat is by far the most repeated haptic event. This must be measured on prototype hardware (§15.2).

Heartbeat suppression is **not** available as an unconditional mitigation, because the heartbeat carries the running/paused distinction (§6.1). Any reduction in heartbeat density must be accompanied by the LED taking over that distinction — the basis of the planned power-saving mode.

---

## 12. Ruleset Selection and Configuration

### 12.1 Selection Model

The scoreboard ships with a library of **preconfigured rulesets**. Before each match the operator selects one. Each exposes customisable settings so that state, league and rules-cycle variations are accommodated without authoring a ruleset from scratch.

### 12.2 Configuration Model

```
ruleset:
  id, name, version, derived_from
  periods:
    - label, duration_s
      secondary_clock:
        active                          # does the clock run in this period
        reset_at_start                  # zero accumulation on entry
        threshold_s                     # value the referee is watching for
        threshold_note                  # displayed text, e.g. "1 pt" / "decides bout"
  overtime:
    - label, duration_s, type
      secondary_clock: { as above }
  phases:             [ {label, boundary_s, notify_on_entry} ]
  scoring:            {min, max, allow_negative_total}
  main_clock:
    warning_at_s                        # default 5
  secondary_clock:
    enabled
    polarity:         count_up | count_down
    duration_s                          # count_down only
    notify_on_expiry                    # notification only; never awards
    display_anchor:   favoured_athlete   # count_up only, per §6.3
    clear_at_period_end                 # count_down only, per §6.4
    deassign_at_period_start            # count_up only, per §6.3
  action_grouping:
    enabled                             # freestyle by default
    window_ms
  f1: {role, label, led_colour}
  f2: {role, label, led_colour}
  counters:           [ {id, ladder_steps, display_only: true} ]
  tiebreak_criteria:  [ ordered list, display_only: true ]
```

**`f1.led_colour`/`f2.led_colour` are read only when that slot's `role` is `COUNTER`** (§10.3) — a secondary clock or tri-state flag renders in the holding athlete's colour instead, on both remotes, regardless of what `led_colour` is set to. The field stays in the schema for the counter case rather than being made conditional on role at the schema level, since a ruleset can still reassign a slot's role.

**Secondary-clock behaviour is declared per period**, not per ruleset. This is what allows the NCAA riding-time rules to be expressed as data: the clock runs in all regulation periods with a one-minute threshold and accumulation carrying between them, resets on entry to overtime, then runs again through the overtime periods with a one-second threshold and a different consequence. Because these rules have changed across rules cycles, encoding them as configuration means a rules change is a settings edit rather than a code change.

`threshold_s` and `threshold_note` are **display aids only**. The system never applies a threshold consequence.

Adding or amending a ruleset requires no firmware change, no interface change, and no communication-layer change.

### 12.3 Pre-Match Confirmation

On selection the scoreboard displays a confirmation summary:

- Ruleset name and period structure
- A legend of what F1 and F2 are bound to
- Athlete-to-colour assignment
- The bound officiating set by serial number, with link and battery status for both remotes

This is the only point at which the referee learns the function-button bindings, since the remotes carry no labelling and bindings differ across rulesets.

### 12.4 Match Lifecycle

| Stage | Where |
|---|---|
| Ruleset selection and customisation | Scoreboard |
| Athlete and bout identification, colour assignment | Scoreboard |
| Pre-match confirmation | Scoreboard, verified by referee |
| Match operation | Remotes |
| Result and riding-time reading | Scoreboard |
| Result entry to tournament platform | External |
| Reset for next match | Scoreboard |

The remotes are used only during match operation. No setup, teardown, or reset is performed from the wrist.

---

## 13. Ruleset Conformance Matrix

| Requirement | Folkstyle NFHS | Folkstyle NCAA | Freestyle / GR | IBJJF | ADCC |
|---|---|---|---|---|---|
| Main clock control | ✓ | ✓ | ✓ | ✓ | ✓ |
| Increment / decrement scoring | ✓ | ✓ | ✓ | ✓ | ✓ |
| Period / phase navigation | ✓ | ✓ | ✓ | ✓ | ✓ |
| Clock adjustment | ✓ | ✓ | ✓ | ✓ | ✓ |
| Secondary athlete clock | — | ✓ riding time | ✓ activity clock | — | — |
| Per-period clock behaviour | — | ✓ carry / OT reset | ✓ period-end clear | — | — |
| Heartbeat haptic | — | ✓ | ✓ | — | — |
| Expiry / boundary notification | — | — | ✓ activity clock | — | ✓ phase |
| Tri-state flag | ✓ pending choice | ✓ pending choice | — | — | — |
| Counter — F1 | — | — | — | ✓ advantages | ✓ warnings |
| Counter — F2 | — | — | ✓ cautions | ✓ penalties | ✓ negative points |
| Scoring action grouping | — | — | ✓ criteria | — | — |
| Phased scoring | — | — | — | — | ✓ |
| Function buttons consumed | 1 of 2 | 2 of 2 | 2 of 2 | 2 of 2 | 2 of 2 |

Every ruleset in scope is served by seven buttons, two gestures, and four indicators.

---

## 14. Resolved Design Decisions

Recorded so that settled questions are not reopened without cause.

| Decision | Resolution | Where |
|---|---|---|
| **Heartbeat generation** | Commanded per beat by the scoreboard. The remote holds no ownership state and runs no timer. Beat regularity is not an officiating requirement; eliminating stale-state divergence is. | §6.2 |
| **Remote statefulness** | Stateless with respect to the match. Only device-local state is held. | §2.1 |
| **Acknowledgement vs heartbeat collision** | Both mitigations applied: amplitude separation and bounded burst suppression. | §11.1 |
| **Riding time across boundaries** | Accumulation carries between regulation periods, resets on entry to overtime. Ownership deassigns at every period boundary. Expressed as per-period configuration. | §6.3, §12.2 |
| **Secondary clock advance warning** | None. The count-down heartbeat already conveys time remaining. | §11 |
| **Lockout mode** | Not implemented. Recessed buttons and hold thresholds are sufficient against accidental actuation, and a lockout mode adds a device state the referee must track. | §3.1 |
| **Clock derivation** | Monotonic elapsed-time subtraction, never accumulated ticks. | §8.1 |
| **Application fault detection** | Internal watchdog deliberately drops the serial link on stall, converting ambiguous silence into unambiguous link loss. | §8.3 |
| **Display tiering** | Primary tier — score, time, secondary clock when active, link and battery — always visible and legible at distance. Everything else in a collapsible detail panel. | §8.4 |
| **Officiating set pairing** | Firmware-paired sets, serial-labelled. No field pairing procedure. | §2.3 |
| **Set substitution state** | Match state fully retained, including indicator ownership. The scoreboard asserts full indicator state to the new remotes on connection; the referee re-enters nothing. | §8.6 |
| **Repeated-press scoring** | Accepted as unavoidable within the button budget; viability rests on protected acknowledgement fidelity. | §5.3, §11.1 |
| **Automatic point awards** | Rejected throughout. The system notifies; the referee awards. | *Scope* §7.2 |
| **Input blocking** | Rejected. The system never rejects an input on ruleset grounds. | *Scope* §7.2 |
| **Penalty escalation** | Scoreboard displays counts and ladder position; the referee applies every consequence. | §2.2 |
| **Pending-choice flag** | A manually-set tri-state memory aid. No state machine. | §9 |
| **Riding-time notification** | None. Differential displayed adjacent to the favoured athlete's score. | §6.3 |
| **Scoring action grouping** | Scoreboard-side, best-effort, freestyle-focused. Never actuates state or alters score. | §4.3 |
| **Counter LED rendering** | Binary — off at zero, solid when non-zero. | §10.3 |
| **Main clock state on the wrist** | No indication needed; acknowledgement on each toggle press suffices. | §5.1 |
| **Gesture timing location** | Press/hold/repeat discrimination lives in remote firmware. | §4.2 |
| **Tactile discrimination** | Shape and position relative to the oversized circular centre button. | §3.1 |
| **LED specification** | All four RGB, for firmware flexibility. | §3.2 |
| **Haptic motor** | ERM plus driver IC, selected for expiry amplitude. | §3.3 |
| **Inert buttons** | Fully inert — no action, no haptic, no LED. | §5.6 |
| **Low battery notification** | Triple buzz, once at threshold crossing only. | §11 |
| **Event ordering** | Order of receipt at the scoreboard is authoritative. | §7.3 |
| **Score floor** | Point removal clamps at the ruleset minimum. | §5.1 |
| **NFHS spare function button** | Left inert, preserving an identical folkstyle mental model. | §5.6 |
| **F1 vs F2 accessibility** | Both equally accessible; assignment governed by semantic convention. | §5.6 |

---

## 15. Validation Items

Assumptions accepted as working positions, to be confirmed with measurement during prototyping rather than discovered in a disputed match. Project-level risk framing is in *Project Scope* §10.

### 15.1 Event ordering under rapid exchange
Order of receipt is authoritative (§7.3), assuming referee input intervals comfortably exceed transit variance. Log inter-press intervals during live matches against measured transit jitter under the interference conditions of §7.4.

### 15.2 Battery life against the ten-hour target
Measure average current attributable to the radio at the connection cadence required by the 120 ms acknowledgement budget, and to a reduced-amplitude 1 Hz heartbeat over a representative folkstyle match.

### 15.3 ERM adequacy across the full haptic range
Whether one ERM plus driver IC delivers unmistakable expiry amplitude, a countable reduced-amplitude heartbeat, and acknowledgement inside 120 ms (§3.3).

### 15.4 Link performance at range under load
Whether latency and reliability budgets hold at 40 ft with body shadowing, 30 systems active, in congested 2.4 GHz. Test the combination — shadowing at range and channel congestion compound.

### 15.5 Amplitude separation is perceptually sufficient
Whether the amplitude difference between heartbeat and acknowledgement is reliably distinguishable on the wrist, in motion, through a wristband, by a referee not attending to it. This is the assumption §11.1 rests on and cannot be settled from datasheet figures.

### 15.6 Burst suppression window duration
Long enough to cover a five-press scoring sequence, short enough that the heartbeat's absence never reads as accrual having stopped. Interacts with hold-repeat, which generates sustained input at 150 ms intervals.

---

*Rule details reflect NFHS 2025-26, NCAA 2025-27, UWW rules effective January 2026, IBJJF Rule Book v6.1, and published ADCC rules and regulations. NCAA riding-time and overtime provisions have changed across recent rules cycles and are expressed as configuration for that reason; verify against the current rulebook at implementation and at each rules cycle.*