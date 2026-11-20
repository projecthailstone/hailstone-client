# Gameplay values versus RoF2 wire layouts

`src/net/messages/` owns gameplay values shared by the connection and game state.
`src/net/protocol/rof2/packets/` owns binary opcode-specific encoders, decoders,
record sizes and conversions, and includes the shared values as inputs/outputs.
No `src/game/*.h` imports a RoF2 packet-family header after this extraction.

Existing values retain their source names and namespace during this structural
phase so that downstream gameplay state is not replaced or duplicated. Several
fields that mirror RoF2 (notably `ItemSlot` coordinates and profile unknowns)
require semantic normalization during their specific functional codec migration.
They are not a mandate that future codecs reuse the RoF2 layouts.

The codec will ultimately own all slot-to-wire conversions; no RoF2 protocol
implementation should leak into the generic connection or domain API.
