# Wireless Single-Operator Officiating System
# Project Scope

**Version 1.1**
**Companion document:** *Functional Specification v2.1*

---

## 1. Purpose

### 1.1 Problem

Youth and amateur grappling competitions are chronically short of table personnel. A conventional mat requires a referee plus a scoreboard operator, and often a towel tapper to alert the referee when time expires, since a referee engaged with the athletes cannot reliably hear the horn. Small clubs, school programs, and local tournaments frequently cannot staff this, resulting in delayed matches, untrained volunteers operating scoreboards, and scoring errors.

### 1.2 Solution

A wrist-worn remote pair that allows one referee to run a complete match — clock, score, and the one or two ruleset-specific states that matter — without any table support.

The haptic time-expiration notification replaces the towel tapper directly: the referee feels expiry on the wrist regardless of crowd noise or position on the mat.

### 1.3 System Composition

| Node | Role |
|---|---|
| **wrsl-app** | Browser-based scoreboard application, served once and run locally. Owns all ruleset logic, match state, clocks of record, and the match event log. |
| **Dongle** | USB-C bridge between the remote pair and the host. Isolates wireless performance from host hardware variability. |
| **Remote (Red)** | Worn on the referee's red (left) wristband. |
| **Remote (Green)** | Worn on the referee's green (right) wristband. |

A dongle plus its two remotes constitute an **officiating set** — the unit of pairing, labelling, deployment, and field replacement. Sets are paired in firmware and labelled by serial number.

---

## 2. Market

### 2.1 Target

Youth, scholastic, collegiate, and amateur competition where table manpower is the binding constraint.

### 2.2 Explicitly Not Targeted

International and professional competition operating with multi-official crews — mat chairman, judges, video review. Those events have the manpower the product exists to substitute for, and their scoring authority is distributed across a crew rather than held by the mat referee. Designing for them would compromise the interface simplicity that serves the actual target market.

---

## 3. Rulesets

### 3.1 In Scope

| Family | Rulesets | Notes |
|---|---|---|
| Folkstyle wrestling | NFHS, NCAA (men's and women's) | NCAA variant includes riding time |
| Freestyle / Greco-Roman | UWW / USA Wrestling | Includes activity ("shot") clock |
| Brazilian Jiu-Jitsu (gi) | IBJJF, SJJIF | Points + advantages + penalties |
| Submission grappling (no-gi) | ADCC, NAGA, JJWL and similar | ADCC adds negative points and phased scoring |

All rulesets in scope use straightforward numerical scoring, which is what allows a single increment/decrement interface to serve all of them.

### 3.2 Judo — Out of Scope

Judo's scoring model is non-scalar. Ippon, waza-ari and yuko are discrete ranked counters rather than an accumulating total, with yuko never summing into a waza-ari however many are scored. This cannot be expressed through a symmetric increment/decrement interface without either distorting the model or adding inputs that would degrade the interface for every other ruleset.

---

## 4. Deployment Envelope

| Parameter | Requirement |
|---|---|
| Referee-to-dongle range | Reliable operation to **40 ft (12 m)** — the diagonal of an NCAA mat plus working margin |
| Concurrent systems | Up to **30 independent systems** in parallel in one venue without interference or cross-association |
| RF environment | Reliable operation in a crowded 2.4 GHz environment: venue Wi-Fi, hundreds of spectator phones, other Bluetooth devices |
| Session duration | A full **ten-hour** tournament day |

---

## 5. Operating Assumptions

The system is specified against a controlled host environment. These are stated assumptions, not aspirations — the design does not attempt to function outside them, and deployment documentation must establish them before an event.

| Assumption | Consequence if violated |
|---|---|
| **A Chromium-based browser is available** on the host machine | The application will not run. Firefox and Safari do not support the serial access the dongle requires. |
| **Serial device permissions are grantable** to the application | The dongle cannot be reached. Permission is granted once per machine and persists. |
| **The scoreboard window remains in the foreground for the duration of a match** | Browser background throttling would delay timer-driven events including expiry notification. Easy to hold in practice: the scoreboard is the mat's public state indicator, read by referee, athletes, coaches and spectators, so there is no reason to cover it. |
| **The host machine is configured not to sleep or suspend mid-match** | Suspension halts the application and the clock. Sleep and screen blanking must be disabled at setup. |

Establishing these is a pre-tournament setup responsibility (§8.1).

---

## 6. Explicit Exclusions

### 6.1 Product Boundary

**The system runs the match. It does not manage the event.**

At conclusion the referee reads the final score from the scoreboard and enters the result manually into whatever tournament management platform is in use — Trackwrestling, Smoothcomp, FloArena, or equivalent. There is consequently no terminal-event input, no win-condition input, and no referee-decision input on the wearable. Once the match is over the referee has no further use for the remotes.

This removes an entire class of high-consequence inputs from the interface.

### 6.2 Excluded Officiating Functions

The system deliberately does not implement every function a referee might want automated. A function is excluded when it would require additional buttons, gesture complexity, or modal remapping, **and** referees already manage it competently unaided:

- Folkstyle caution and stalling-warning tallies
- Injury, blood, and recovery time
- Terminal events — fall, submission, technical superiority
- Referee decisions on tied matches
- Blocking or rejecting invalid input

Every excluded function is documented in §8 as an explicit external responsibility. Exclusion is a design decision, not an oversight, and referee-facing documentation must make that unambiguous.

### 6.3 Excluded from the Product Entirely

| Excluded | Rationale |
|---|---|
| **Event logistics** | Charging schedules, equipment distribution across mats, staffing, and spare-set inventory are the event operator's concern. |
| **Component-level set reassignment** | Officiating sets are firmware-paired. Re-pairing a replacement remote to an existing dongle, or vice versa, is deferred to future work. The supported field remedy for a hardware failure is substituting a complete set. |
| **Results reporting or export** | See §6.1. |
| **Athlete or bracket management** | Handled by the tournament management platform. |

---

## 7. Design Principles

These govern every design decision in the functional specification and should not be overridden without deliberate reconsideration at this level.

### 7.1 Operable by Feel

> **The referee must be able to find and operate every button by feel, without looking at their wrists, while performing rule-mandated hand signals and managing athletes on the mat.**

This constraint outranks feature coverage. A referee glancing at their wrist to locate a control is a referee not watching the match.

### 7.2 The Referee Owns Rule Application

The system is an instrument, not an arbiter. It records what the referee tells it, computes and displays derived values, and notifies the referee of conditions worth their attention. It does not decide anything.

- **Scoring is never automatic.** Every score change originates from a deliberate referee press. Where a ruleset condition would earn a point, the system notifies; the referee decides and enters.
- **Penalty escalation is never automatic.** The scoreboard displays counts and ladder position; applying the consequence is the referee's action.
- **Tracked flags are never automatically actuated.**
- **Input is never blocked.** The system never rejects an input on ruleset grounds.

Clock behaviour is the one category of automatic action, because clocks are mechanisms rather than judgements. Clocks run, periods advance, and secondary clocks reset at configured boundaries without referee input — but no clock ever converts its own state into a score.

**Rationale.** Automatic awards invite two failure modes worse than the labour they save: false awards where the referee had judged the condition unmet, and redundant awards where the referee enters the point as well. Both silently corrupt the score and neither is easy to notice mid-match. Input blocking, symmetrically, adds modal behaviour to guard errors the official is already positioned to catch.

### 7.3 Pure Front-End, Offline Operation

**The scoreboard application is a pure front-end application with no server dependency during a match.**

Served from a website and loaded once, it then runs entirely within the browser on the local machine, communicating only with the dongle over the local serial connection. **No data is sent or received over the internet while a match is running**, and a match can be run start to finish with the venue's network entirely absent.

- Tournament venues have unreliable networks. A scoring system that degrades with connectivity is unusable in exactly the conditions it is built for.
- Latency and clock behaviour become properties of the local machine alone.
- There is no per-event infrastructure, no accounts, and no server to operate — which matters for a product aimed at organisations that cannot staff a scoring table.

### 7.4 Ruleset Logic Lives in One Place

The remote is stateless with respect to the match; the scoreboard holds all ruleset logic and all match state. Adding or amending a ruleset must therefore be a scoreboard change only — no firmware update, no interface change, no communication-layer change, and no retraining on button locations.

Rulesets change between competition cycles. The system must absorb those changes as configuration data rather than code.

### 7.5 Simplicity Over Coverage

Where a feature would add buttons, gestures, modes, or state that referees must hold in mind, and the function can be reasonably managed by the referee unaided, the feature is excluded. The interface budget — seven buttons, two gestures, four indicators — is treated as fixed.

---

## 8. External Responsibilities

**This section must be reproduced in referee- and operator-facing documentation and training materials.** These functions are not tracked, timed, or enforced by the system.

### 8.1 Pre-Tournament Setup — Operator

| Responsibility | Notes |
|---|---|
| **Host environment** | Chromium browser installed; serial permission granted; sleep, suspend and screen blanking disabled (§5). |
| **Application pre-load** | Each host loads the application at least once while connectivity is available, so it is cached and available offline at the venue. |
| **Set distribution** | Each officiating set kept together and matched to its mat by serial number. |
| **Charging and spares** | Remotes charged before the session; spare sets available for field substitution. Logistics are the operator's concern (§6.3). |
| **Display placement** | Scoreboard positioned to be legible to the referee, athletes and spectators. |

### 8.2 All Rulesets — Referee

| Responsibility | Notes |
|---|---|
| **Match result entry** | Final score read from the scoreboard and entered manually into the tournament management platform. |
| **All point awards** | The system never scores automatically. |
| **All penalty escalation** | The scoreboard displays counts and ladder position; applying the consequence is the referee's action. |
| **Recognising invalid input** | The system accepts any input at any time and never rejects one on ruleset grounds. Noticing that an entry should not have been made is the referee's responsibility. |
| **Athlete colour assignment** | Confirm at pre-match that the athlete entered as red is the athlete wearing red. Every subsequent input depends on it. |
| **Injury, blood, and recovery time** | External stopwatch. Stop the main match clock for these periods. |
| **Terminal events** | Fall, pin, submission, technical fall/superiority, disqualification, default and forfeit are declared verbally and by signal. Stop the main clock; the scoreboard is not told the match ended. |
| **Equipment, uniform, hygiene, coach conduct** | Entirely external. |
| **Video review / challenge procedures** | Conducted externally; score changes entered by repeated point presses. |

### 8.3 Folkstyle

| Responsibility | Notes |
|---|---|
| **Stalling warning count** | Tracked mentally; resulting points entered manually. |
| **Technical violation and caution counts** | Tracked mentally. |
| **Unsportsmanlike conduct tally** | Tracked mentally, including any effect on choice. |
| **Riding-time state** | The referee alone assigns, transfers and deassigns riding time as control changes. The system infers nothing from scoring or position. |
| **Riding-time consequence** | The system displays the differential and resets it at the overtime boundary. Whether the differential earns a point in regulation, or decides a tied overtime round, is the referee's determination under the rules in force. |
| **Pending-choice tracking** | A referee-set memory aid only. Determining entitlement to choice, including overtime and tiebreaker sequences, is entirely the referee's responsibility. |
| **Injury and blood time** | External stopwatch. |

### 8.4 Freestyle / Greco-Roman

| Responsibility | Notes |
|---|---|
| **Verbal passivity stimulation** | Referee vocabulary and warnings before activity time is initiated. |
| **Activity clock point entry** | The system signals expiry; the referee judges whether a score occurred and enters the point. |
| **Par terre position and placement** | Entirely external. |
| **Caution entry discipline** | Every caution entered as issued, for the loss-by-cautions rule and the caution criterion to evaluate correctly. |
| **Verifying scoring action grouping** | Before a criteria decision, review the action log and account for grouping that does not match what occurred. |
| **Criteria decision** | The scoreboard evaluates and displays criteria for reference; declaring the winner is the referee's decision. |
| **Injury and blood time** | External stopwatch. |

### 8.5 BJJ

| Responsibility | Notes |
|---|---|
| **Three-second control counting** | Counted mentally for every scoring position. No timer provided. |
| **Penalty and advantage escalation** | The counter is a tally. Awarding what a penalty step calls for is a separate manual entry. |
| **Position legality by belt and age division** | Entirely external. |
| **Submission recognition and stoppage** | Referee stops the main clock; no terminal input exists. |
| **ADCC phase awareness** | Haptic notification on entering the full-scoring phase. Positive-point input remains live beforehand; officials are expected to know positive points do not count in the first half. |
| **Referee decision on a tie** | Declared verbally, recorded at result entry. |

### 8.6 Hardware Failure Mid-Match

A remote failure — depleted battery, hardware fault, physical damage — must not end the bout.

**Procedure:**

1. Referee stops the match clock.
2. Operator substitutes a complete spare officiating set.
3. Match resumes.

**No state is re-entered.** All match state — score, clock position, period, secondary-clock ownership and accumulation, counters, pending-choice flag, and the action log — is held by the scoreboard and survives the substitution. The scoreboard renders current indicator state to the new remotes on connection, so the wrist indicators are correct before the clock restarts.

This follows from the architecture: the remotes hold no match state, so a substituted remote has nothing to restore and nothing stale to correct. The only equipment-specific step is the physical swap.

---

## 9. Release Scope

### 9.1 Initial Version

Everything specified in the *Functional Specification* without a "planned" marker. In summary: full match operation for all rulesets in §3.1, seven buttons, two gestures, four indicators, offline browser scoreboard, firmware-paired officiating sets, complete-set field substitution.

### 9.2 Planned — Post-MVP

| Feature | Rationale for deferral |
|---|---|
| **Customisable button mapping** | Default assignments are reasonable intuitions; remapping is a refinement once field preference data exists. |
| **Extended link-state signalling** | Hardware supports RGB throughout specifically to leave this open. Intermediate connectivity states and link-quality indication are valuable but not blocking. |
| **Power-saving mode** | Needed only if measured battery life falls short of the ten-hour target. |
| **Dual-haptic hardware revision** | Contingent on the single-ERM assumption failing in prototyping. |
| **Swappable battery pack** | Contingent on battery optimisation proving insufficient. |
| **Desktop-packaged build** | Removes dependence on browser serial-access policy and simplifies venue setup. The serial transport should be built behind a thin abstraction from the outset so this path stays inexpensive. |
| **Counter count encoding on wrist indicators** | Only if field use shows referees want counts on the wrist rather than the scoreboard. |
| **Component-level set reassignment** | Requires a field pairing procedure with safeguards against mis-association at multi-mat events. |

### 9.3 Explicitly Not Planned

Judo support; multi-official crew workflows; results reporting or platform integration; event and bracket management; automatic scoring or rule enforcement of any kind.

---

## 10. Project Risks and Validation Targets

Assumptions the project rests on that cannot be settled from specification alone. Each is expanded in the *Functional Specification*, §15.

| Risk | If it fails |
|---|---|
| **Single ERM cannot serve both expiry amplitude and a low-amplitude countable heartbeat** | Dual-haptic hardware revision required; affects enclosure and cost. |
| **Amplitude separation between heartbeat and acknowledgement is not perceptually reliable** | Repeated-press scoring loses its verification mechanism; the core input model is undermined. |
| **Ten-hour battery life not achievable** | Power-saving mode or swappable battery required; the latter affects enclosure, sealing and cost. |
| **Latency budget not holdable at 40 ft with body shadowing under 30-system density** | Acknowledgement fidelity degrades, which the input model depends on. May require higher transmit power or dongle placement constraints. |
| **Event ordering by receipt proves unreliable under rapid exchange** | Requires remote-side sequencing, adding protocol complexity. |

---

*Rule details reflect NFHS 2025-26, NCAA 2025-27, UWW rules effective January 2026, IBJJF Rule Book v6.1, and published ADCC rules and regulations. NCAA riding-time and overtime provisions have changed across recent rules cycles and are expressed as configuration for that reason; verify against the current rulebook at implementation and at each rules cycle.*