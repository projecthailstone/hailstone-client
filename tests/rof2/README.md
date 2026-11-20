# RoF2 packet tests

This directory owns RoF2-specific **raw wire layouts, opcode mappings, encoding,
decoding, and packet round trips**. Its `CMakeLists.txt` declares the sources
compiled into the shared `hailstone_tests` executable.

To add another protocol, create `tests/<protocol>/`, provide its own
`CMakeLists.txt` source list, and adapt each RoF2 wire expectation to that
protocol's opcodes and field layouts. Keep tests for protocol-independent
SOE framing, login-server framing, binary utilities, and client-level game
behavior outside the protocol directory.

Some existing suites exercise game rules together with byte-exact RoF2
packets; when porting a suite, reuse the behavioral expectations but replace
the RoF2 encoder/decoder and wire constants rather than copying those values.

The typed decoder corpus is `fuzz_decoders.inc`; `fuzz_decoders_test.cpp` checks
its coverage and malformed-packet behavior. `fuzz_achievements.h`,
`fuzz_game_decoders.h`, and the `*_test_packets.h` fixtures belong to this
protocol's packet tests. Protocol-neutral fuzz inputs live in
`tests/fuzz_patterns.h` and `tests/fuzz_support.h`.
