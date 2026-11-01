# NET-SYNC relay bundle substrate

The branch carries NET-HARD's membership cursor and commit codec prerequisites.
A source-generated seam test sends encoded canonical records through
fragmentation, loss, reordering and duplicate retransmission into that cursor:
lifecycle stays staged until a full frame validates, and the departure delta
appears once. ROUTER-1 dropped the source branch's durable fsync-per-frame
commit journal: the relay's existing OBFMERLG game log already records every
frame command for rejoin and replays, so committed canonical records append
into it as kind-6 records (one per Frame/Membership/End) instead — durable
before any byte is fanned out, read back by `RelayLogFile::parse` and
`RelayLog::assemble`'s canonical branch.

This document describes the bounded byte-channel substrate in
`RelayBundleChannel`. It is not a game-network integration: the caller supplies
an already-authenticated peer binding and an externally injected clock.

## Wire format

The channel packet is little-endian and has a fixed 46-byte header:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 2 | Magic `NS` |
| 2 | 1 | Version `1` |
| 3 | 1 | Kind (`1` data, `2` selective ACK) |
| 4 | 8 | Game ID |
| 12 | 8 | Epoch |
| 20 | 8 | Message sequence |
| 28 | 4 | Fragment index |
| 32 | 4 | Fragment count |
| 36 | 4 | Total message bytes |
| 40 | 2 | Fragment bytes (zero for ACK) |
| 42 | 4 | CRC-32 |
| 46 | variable | Data fragment payload |

CRC-32 covers the header before the CRC field and the payload, excluding the
CRC field itself. Every configured datagram, including its reserved outer
envelope, is at most 1200 bytes. The default outer envelope reservation is
5 bytes, leaving at most 1195 bytes for a channel packet. A fragment is
validated against its message total, index, count, and exact packet length.

## Bounds and delivery

Message sequences are monotonic `uint64_t` values and never wrap. Queue and
receive limits are explicit backpressure: an unsuccessful `queue` or `receive`
does not silently drop an admitted message. The sender and receiver each use a
sliding sequence window of `maxPendingMessages`; the receiver reserves one
message slot and enough bytes for the next expected message when accepting
out-of-order data.

Both ends must be configured with the same fragment sizing and sequence-window
limit by the authenticated session setup; this substrate does not negotiate
them. In particular, a sender window larger than the receiver's retained
history is not a supported binding.

Fragments are selectively ACKed and retried using caller-provided monotonic
time. ACKs are transport receipts only; they do not mean durable commit. The
receiver reassembles and delivers messages in sequence order, with
head-of-line blocking for later complete messages. Delivered bytes remain
accounted until `takeDelivered` drains them.

After draining, the receiver retains at most `maxPendingMessages` retired
records. Each record retains its header metadata and a SHA-256 fingerprint for
every fragment, so an identical retransmission is ACKed without redelivery and
changed metadata or bytes are rejected. The sender retains bounded metadata for
retired sequences so repeated final ACKs are idempotent. Packets and ACKs
outside those retained windows are explicitly stale.

`maxHistoryBytes` defaults to 16 MiB. It covers both directions' retired
metadata and receiver fragment fingerprints, and is validated with checked
arithmetic against the configured window and maximum message size. The
`heldBytes` counter includes queued payloads, receive buffers, and retired
history bytes. History is bounded independently of the lifetime of a process;
it is not a journal.

`poll(now, maxDatagrams)` inspects at most that many fragment slots, with a
persistent rotating cursor. It may return fewer packets (including zero) when
inspected slots are acknowledged or not due; callers must keep servicing the
channel rather than interpret an empty poll as completion. Counters charge
channel bytes only; measurements must separately add the outer envelope and
IP/UDP overhead. The accounting budgets bound retained payload/metadata, not
allocator-specific heap usage or caller-owned output vectors.

## Frame-clock convention

`RelayFrameClock` receives its origin from committed load/resume state. The
first frame closes at `origin + 200 ms`: receipts in
`[origin, origin + 199]` map to the first frame, while a receipt exactly at
the boundary maps to the next frame. There is no added safety frame, and the
clock reads no wall clock itself.

The authority captures and persists its origin before durable append, activates
only after successful completion, and then services elapsed deadlines in order.
It must not reset the origin to disk completion or publication time. The
scheduler and append path are integrated by the authority (ROUTER-1): the scheduler stamps, the builder seals, the committed record appends into the OBFMERLG game log before fanout.

## Canonical frame-budget substrate

`RelayFrameBundle::Builder` is a canonical budgeting component, not a live
adapter or scheduler. Each player's contribution has a 16,384-byte budget,
where bytes are the complete serialized `GameMessage` and exclude the outer
`NetCommand` wrapper and canonical/transport overhead. A 16,385--65,536-byte
order is admitted intact as the only order for that player's contribution in
the frame; later same-player orders are deferred FIFO. Orders over 65,536
bytes and zero-byte orders are refused and recorded in the canonical refusal
audit without copying them. Other players are unaffected. The payload remains
opaque here: a future adapter validates `GameMessage` semantics and binds its
player identity to the authenticated slot.

The OBFB v1 payload starts with `OBFB`, a little-endian version `1`, and three
little-endian `u32` counts (orders, deferrals, refusals), for an 18-byte
header. An order is `slot:u8, id:u64, byteLength:u32, bytes`; a deferral is
`slot:u8, id:u64, fromFrame:u32, toFrame:u32, orderBytes:u32, SHA-256:u8[32],
reason:u8` (54 bytes); and a refusal is `slot:u8, id:u64,
declaredBytes:u64, code:u8` (18 bytes). Each count is at most 4,096, each
declared order is at most 64 KiB, and the encoded frame is at most 2 MiB.
Entries are strictly ordered by `(slot,id)` and IDs cannot overlap across the
three lists. Deferrals advance exactly one frame and carry the digest of the
deferred opaque bytes. Unknown versions/codes, truncation, trailing bytes,
invalid lengths, and budget violations are rejected transactionally.

Frame preparation is immutable staging. The builder copies and budgets
accepted orders, but does not remove queues or increment counters until the
matching `RelayCommit::Kind::Frame` has successfully crossed the game-log
append boundary and the caller invokes `committed` only after actually
observing the append succeed. An append
failure requires the caller to invoke terminal `fail`; there is no abandon or
retry publication path in this component. Counters therefore describe
durably completed frames: ordinary/oversized orders, normal/oversized player
frames, deferred order entries, and refused audit entries. Normal and
oversized player-frame counters count nonempty player contributions; their
denominator is the sum of nonempty player contributions across committed
frames. Each slot is
bounded to 256 pending orders, 1 MiB of pending order bytes, and 256 pending
refusals.

The existing membership validator and cursor guard stamp/membership state and
require an active, fully loaded canonical clock for orders. Membership,
cross-frame digest matching, and `GameMessage` semantic validation remain
integration responsibilities. This substrate is not integrated into step 3 or
step 4 and makes no production scheduling, measurement, game-master,
checkpoint, latency, durability, or actual gameplay-rate claim.
Transport ingress must enforce its own maximum before reassembly; this builder
only bounds and copies payloads at admission and is not that ingress guard.

## Canonical ingress wire and frame scheduler

`RelayCanonicalInput` is the OBCI v1 client->relay order/beat wire. Each
message is one complete reliable-channel delivery to the slot's authenticated
binding; it carries a dense per-slot id and, for an order, the complete
serialized `GameMessage` bytes (opaque here). The payload never identifies the
sender: the binding supplies the slot. The 18-byte header is `OBCI`, version
`1`, kind (1 order / 2 beat), `id:u64`, `declared:u32`; a beat declares zero
bytes and ends the slot's input for its current window (the FRAMEINFO count
equivalent). A declared order size is checked against the 64 KiB budget
before any copy, and the enclosing channel must additionally cap the declared
message total at `kMaxWireBytes` (header + 64 KiB) so oversize ingress is
refused before reassembly, not after.

`RelayFrameScheduler::Scheduler` is the stamping and seal-policy component
between decoded ingress and `RelayFrameBundle::Builder`. It owns the per-slot
beat sequence and measurement counters (orders, late orders, beats); the
authority still owns the builder, clock, the game-log append, and channel. Both policies
publish one canonical bundle per frame and stamp an order for the first frame
boundary still open at admission; they differ in what closes a frame:

- `NextFrame` (policy B, per-player delay): a frame seals at its wall-clock
  `RelayFrameClock` boundary. An order whose stamped frame already closed
  (received past the boundary, or delayed behind an earlier seal) is restamped
  to the next unpublished frame and counted late: only its sender is delayed.
  Beats are validated for sequence but never gate a seal.
- `EqualDelay` (policy A, today's semantics): frame F seals only once every
  required slot has delivered the beat completing its window for F. Required
  slots are `membership.active` minus held and departed, so a held-for-rejoin
  or dropped slot cannot stall the beat; a merely late slot can, matching
  today's lockstep where a straggler delays the frame for everyone. Orders
  stamp to the frame the slot's open window covers, so a late player's window
  still lands together while a fast player's post-beat orders land in the
  next window.

A lobby-side canonical-vs-today mode selection and the authenticated binding
of slots remain integration work; this substrate takes the policy as a
construction argument and is itself schedule-only. Neither mode asserts a
production latency claim before the worldwide matrix measures it.

## Canonical client input

`RelayCanonicalClient::Client` turns the durable record stream into the
lockstep driver's per-frame command list. The `RelayMembership::Cursor`
enforces stamp order and stages each membership before its frame; for every
accepted `Kind::Frame` the adapter decodes the OBFB bundle, validates it
against the staged membership, parses each order's serialized GameMessage,
and binds the message's player to the sender slot's player index. A
GameMessage claiming another player — or one for a slot that has no network
player — rejects the whole record; identity never comes from the payload.
The caller consumes each frame's input exactly once: the departed slots as
`MSG_SELF_DESTRUCT(true)` in ascending slot order, then the frame's commands
in `(slot, id)` order. Membership generations, sequence numbers and the End
record ride the cursor's existing checks; a rejected record is a protocol
error and applies nothing.

## Canonical-mode negotiation (ticket flag + HELLO capabilities)

The canonical mode is a property of the game. The lobby decides it at room
setup (`setup.netMode`: `classic` default, `canonical` for late-order policy
B, `canonical-equal` for policy A) and signs it into every player's START
ticket; nobody can change it alone. A mode-flagged ticket is version 7: the
version-4 layout plus a u8 mode byte at offset 13 (34 bytes, mode bits
`TICKET_MODE_CANONICAL=1`, `TICKET_MODE_EQUAL_DELAY=2`). `signTicket` emits
v7 only when the mode is nonzero; mode 0 signs the unchanged v1/v4 ticket, so
games without the flag take the old path byte for byte.

The envelope is now version 4: a HELLO ends with the client's capability
bits (u8 `CAP_CANONICAL`), after the optional session token. The relay binds
a game's mode from the first player START ticket, then enforces it:

- a HELLO into a canonical game whose capability bits lack `CAP_CANONICAL`
  is refused `REFUSE_CAPABILITY` ("the client lacks the capability the
  game's mode requires");
- a player START ticket whose mode disagrees with the bound mode is refused
  `REFUSE_MODE_MIXED` — this covers a flagless ticket joining a canonical
  game and a flagged ticket joining a classic one;
- rejoin (generation > 0) and spectator tickets carry no mode byte: they
  join whatever the game already is, but the capability check still applies
  to them;
- `Transport::sendHello` declares `CAP_CANONICAL`, so this build can join
  either mode.

Old-envelope clients (v3 HELLOs: no capability byte) keep playing classic
games: the relay answers and forwards to them in version 3 (ONLINE16-FIX,
docs/ONLINE.md "Previous-release clients"). A v3 HELLO declares no
capabilities, so a canonical game refuses it `REFUSE_CAPABILITY`. The C++ and lobby
signers pin identical v7 bytes; the lobby test pins the base64url form.
