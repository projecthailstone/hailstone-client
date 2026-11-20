# Network protocol ownership

The numbered network patches through `0185` retain a protocol-independent
connection layer. This is a source-level status description, not a claim that
the resulting tree has been compiled or validated against a live server.

- `connection/` owns lifecycle, request identity, UI state, gameplay state and
  admitted-versus-rejected command handling. It submits semantic messages.
- `messages/` owns typed application values, semantic opcode names and request
  results, never RoF2 numeric opcode tables.
- `protocol/rof2/` owns RoF2 wire opcode mappings, decoding and encoding. A
  supported wire opcode must have an implemented codec operation; an opcode
  table entry alone does not make a message supported.
- `session/` owns SOE establishment, reliability and framing independently of
  which application protocol is selected.
- `ProtocolEndpoint` selects the codec and session, preserves plugin hook
  boundaries and admits encoded packets to the session. Admission is not a
  server acknowledgment. Rejected packets are not reported as admitted.

## Functional completion work through `0185`

The earlier migration moved login, world, zone, gameplay, social, inventory,
commerce, task, adventure and service operations to semantic message handling
and protocol-owned codecs. Patches `0176–0185` refine the remaining lifecycle
and transaction boundaries:

- One-use resurrection/translocation replies are consumed only after complete
  packet admission, rather than during speculative encoding.
- Malformed required zone-entry and initial spawn packets are classified while
  optional later spawns do not terminate zone initialization.
- Login-server list refreshes, player command submissions, inventory moves,
  banking, merchant/trade, marketplace, group/search, task, adventure and
  tribute operations preserve relevant pending or local state when the send
  is rejected.
- Reward, tracking and find-person requests preserve request-lifetime checks
  while using the semantic zone send API, rather than obsolete sender overloads.
- RoF2 serialization remains the only implemented game codec. Future game
  protocols can supply separate codecs without changing connection-owned
  gameplay state.

## Remaining review and validation

Do not equate clean patch application with functional completeness. Remaining
work includes a broader audit of outbound commands and source-defined inbound
messages, confirmation that state transitions match supported server versions,
and capture-dependent behavior documented in `docs/unknowns.md`. Optional
native plugins remain external and are not implemented in the core source.

Do not retain a legacy raw gameplay path alongside an implemented semantic
operation. Remove superseded senders and serializers as each replacement is
completed. Compilation, automated tests and live-session validation remain
deferred until explicitly authorized.

## Compile-memory refactor through `0220` (source-level)

The prior 412-alternative `GamePayload` `std::variant` is replaced by an owning,
checked semantic `Payload` in `net/messages/payload.h`. Its copy/move and
destructor implementations live in `payload.cpp`; small nothrow-movable values
can be stored inline. There is no new global domain-variant wrapper or raw
packet fallback. ProtocolEndpoint still controls admission and extension
precedence; the gameplay connection still handles zone lifecycle and gameplay
state rather than RoF2 packet bytes.

RoF2's former ~2,700-line codec implementation is now split into independently
compiled encode and decode domain units. `codec_dispatch.cpp` routes semantic
opcodes to them; `codec_support.cpp` owns supported-opcode cases;
`codec_validation.cpp` checks multi-shape outgoing commands;
`codec_encode.cpp` and `codec_decode.cpp` own the special, stateful zone paths;
`codec_mapping.cpp` instantiates the canonical `semantic_map.inc` once for
shared mapping lookup. Only RoF2 packet modules know numeric wire layouts.

The source-level structure is intended to prevent a single compile unit from
instantiating every semantic message and all of the RoF2 decoders/encoders.
The precise compiler-memory benefit is **not measured**. In particular,
`GameConnection` still has substantial state and header dependencies, and
GCC internal compiler errors must be independently checked if they recur.
No compilation, automated test execution, or live-session verification has
been authorized as part of generating these patches.
