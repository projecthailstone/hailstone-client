# Network ownership after patch 0172

The initial relocation checkpoint has been superseded by the source-level RoF2
migration. This document records the actual intended ownership boundaries,
not a promise of build or runtime validation.

| Layer | Source directory | Responsibility |
| --- | --- | --- |
| Game connection and orchestration | `src/net/connection/` | Typed gameplay requests, state, deadlines and session lifecycle |
| Shared messages | `src/net/messages/` | Semantic opcodes, payloads and result types; no wire layout |
| Stock RoF2 codec | `src/net/protocol/rof2/` | Game wire opcodes, encoding and decoding, protocol-owned offers |
| Optional Hailstone overlay | `src/net/protocol/extensions/hailstone/` | Explicitly selected extension messages, without changing stock precedence |
| Login and UCS codecs | `src/net/protocol/login/`, `src/net/protocol/ucs/` | Service-specific application wire format and errors |
| Protocol endpoint | `src/net/connection/protocol_endpoint.*` | Selected codec plus selected session, wire observation and delivery admission |
| Session and transport | `src/net/session/` and `src/platform/` | Pluggable session configuration, SOE session and UDP I/O |
| Low-level helpers | `src/net/wire/`, `src/net/crypto/` | Bounded binary helpers and shared cryptography |

Codec ownership is authoritative for both incoming and outgoing messages. A
successfully decoded stock message cannot be reinterpreted by an optional
overlay; a malformed recognized stock message is an error rather than an
extension fallback. Extensions are opt-in per profile. The authorized native
plugin wire/diagnostic API is independent of game message routing. Plugin
implementations and private formats are not integrated or documented here.

UCS distinguishes malformed known messages from unknown opcodes. A malformed
mail-login reply takes the explicit recovery path instead of silently
consuming the login wait. Login, UCS, world and zone each select their session
configuration independently. Transport admission does not imply a server ACK.

**Validation status:** patches through 0172 are source edits only; patch
application is checked separately. Compilation and tests are not claimed by
this document. Consult the latest build report for compiler status.
