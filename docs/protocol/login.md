# Login server protocol (SoD/RoF2 port)

Implementation: `src/net/login.{h,cpp}` (`LoginClient`, `login_codec`), `src/net/des.{h,cpp}`. Tool: `netprobe [-v] login HOST[:PORT] USER PASS [SERVER_NAME]`. Tests: `tests/net_login_test.cpp`; the live check runs only with `HAILSTONE_LIVE_LOGIN=host:port`.

Server side: EQEmu `loginserver/client.cpp`, `login_types.h`, `world_server.cpp`, `encryption.cpp`, `login_util/login_opcodes_sod.conf` (read as a spec, nothing copied).

EQEmu's login server listens on separate ports per client family; the RoF2 client uses the "SoD" port (`client_configuration.sod_port`, default 5999), whose opcodes come from `login_opcodes_sod.conf`. Encode passes are none, CRC 2 bytes (see `session.md`). The UDP session options (`LoginClient::Options::session`): max packet 512, no-data timeout 14.8 s, multi-buffer hold 0, no stat requests, resend adjust 2000 (T = min(1.25 rtt + 2000, 8000) ms), 100 reliable slots.

## eqhost.txt

A server's patcher ships `eqhost.txt` in the game folder to name its login server. Hailstone reads it once, to give a
fresh install its first server (`profile_from_eqhost`, `ui::load_login_settings`): when the asset folder has one and
there is no saved server, the login server it names becomes a saved server, named after its address, and is picked.
`login.eqhost_imported` in `hailstone.ini` then records that it was done (or that the player already had servers when
the file was first seen), so deleting every server does not bring it back; the game folder is never written.

Only the first `Host` line of `[LoginServer]` (or one before any section) is read: `Host=name:port`, `Host=name`,
`Host=[v6]:port` or a bare IPv6 address. `Host1`, `Host2` and other keys are not, nor the older `[Login Servers]` block
format. A line without a usable host, or with a port outside 1 to 65535, is skipped for the next. A UTF-8 byte order
mark is skipped, and at most 64 KiB of the file is read. Without a port the server gets the login screen's default
for a new server (`ServerProfile::port`, 5997); what the original client uses then is not confirmed
(docs/unknowns.md).

## Opcodes

| Opcode | Name | Direction |
|---|---|---|
| 0x0001 | OP_SessionReady | → |
| 0x0002 | OP_Login | → |
| 0x0003 | OP_Logout (unused here) | → |
| 0x0004 | OP_ServerListRequest | → |
| 0x000d | OP_PlayRequest | → |
| 0x0016 | kick request (remove the logged in character; not in EQEmu) | → |
| 0x0017 | OP_ChatMessage | ← |
| 0x0018 | OP_LoginAccepted | ← |
| 0x0019 | OP_ServerListResponse | ← |
| 0x0022 | OP_PlayResponse | ← |
| 0x002f | order-expansion reply (DES-encrypted; ignored) | ← |
| 0x0025–0x002d, 0x0030, 0x0031 | server pushes (chat, GM list, banner, poll, game features, OP_LoginExpansionPacketData; ignored) | ← |
| 0x0032 | the kick request's reply (base reply; not in EQEmu) | ← |

All messages go on the reliable channel.

## Message format

Every message is one application packet, little-endian: **opcode u16 | sequence u32 | compressed u8 | encrypt type u8 (2 = DES) | unused u32 | body** (a 12-byte header; in `AppPacket` the opcode is separate and the data starts with the sequence). Replies begin their body with **success u8 | error id u32 | error text\0** (eqlsstr_us.txt ids; 101 = no error).

- **Encryption.** Type 2 is DES-CBC with an all-zero key and an all-zero IV, fresh for every message; the body is zero-padded to a multiple of 8. The client encrypts only OP_Login. It decrypts only replies 0x18 and 0x2f, whatever their header says (the reply class sets the type), over the body rounded up to a multiple of 8. The all-zero key is a DES weak key, so encrypting twice gives the plaintext back.
- **Compression.** The client never compresses. A reply whose byte 6 (the compressed flag) is 1 is inflated with zlib before it is decrypted.

## Flow

1. **Session** handshake (`session.md`). No session within 10 s, or a failed one: the next host (eqhost.txt `Host`, `Host1`, …; `LoginClient::start` takes a list); none left: **6011**.
2. **OP_SessionReady**: header + u16 **11** (14 bytes). Reply **OP_ChatMessage**: base reply and one more string (`SCHandshakeReply` +0x20; EQEmu sends it empty); only the success byte is checked. EQEmu hard-codes sequence 2 in it.
3. **OP_Login** (encrypt type 2): DES of **user\0 password\0 token\0 flag**. A password login is `user 00 password 00 00 00` (the token empty, flag 0); a token login is `00 00 token 00 01`. Interactive and saved-account logins use the password form; an explicit launcher ticket uses the token form, with no username/password fallback. Those two extra zero bytes can add an 8-byte block compared with a plain `user\0password\0`.
4. Reply **OP_LoginAccepted** (decrypted): success u8 | error id u32 | text\0 | u8 | u8 | **account id** u32 | **session key**\0 (read to its NUL) | failed attempts u32 | show player count u8 | 7 × u32 offer fields | token string | commerce string. EQEmu writes the key as a fixed 11-byte field (10 characters and a NUL), which reads the same. Failure: the reply's error text (EQEmu sends **105**; login.eqemulator.net sends **107** for a wrong password); the password box is cleared. Only the success byte, error id and text are read from a failure: login.eqemulator.net's is 39 bytes, which end inside the offer fields. Failed attempts > 0 after a success: **6018** "There have been %1 failed login attempts…" (`LoginClient::notice()`).
5. **OP_ServerListRequest**: header only. Reply **OP_ServerListResponse**: base reply | count u32 | per server: address\0, type u32 (1 standard, 8 preferred, 16 legends), id u32, name\0, country\0, string\0 (EQEmu: language), status u32 (0 up, 1 down, 4 locked; flags), players u32. The status shown is eqmain's server row (@0x10036750): the locked bit wins, so 4 and 5 read **Locked** (6007), else the down bit **Down** (6006), else **Up** (6005); with show-player-count a locked and down world reads "Locked (Down)" and an up one its player count, which Hailstone does not show. Stock EQEmu sends 4 when the world's status is below 0 with zones booted, 1 when none is booted, and never 5. The status does not stop a play request (below). While the list is shown it is requested again once more than **60 s** have passed since the last request, and the answer replaces it.
6. **OP_PlayRequest**: header + server id u32 (16 bytes). Refused locally with **6028** (nothing sent) when the server's country, mapped through the locale table (first two characters: us kr tw jp de fr cn, anything else us), is not the client's locale (us), unless the login reply set show-player-count. That is the only local refusal (`ServerSelectWnd::WndNotification` @0x10038310 checks no status): a down or locked world is asked like any other, and the server decides. EQEmu's world admits an account whose status is at least `GM:MinStatusToBypassLockedServer` to a locked world (Down is a locked world with no zone booted) and answers **326** to anyone else. Reply **OP_PlayResponse**: base reply + server id u32; the id is looked up in the current list for the world address. Errors: EQEmu's 111 (already online), 326 (unavailable), 337/338 (suspended/banned), 339 (full); **302/114** mean a character is still logged in and the server offers to remove it (`PlayResult::offer_kick()`), below.
7. **The kick** (eqmain: ServerSelectWnd::OnJoinServer @0x10037350, the request at @0x100388dc, its reply's handler @0x10037740). After 302 or 114 the login stays up and the player is asked the reply's text (Yes / No; `LoginState::KickOffered`, the client's own box over the server select). No: back to the list. Yes: the kick request **0x16**, header only (its class at 0x100209b0 writes no body), pending **60 s** with the server's id; "Please wait while the currently logged in player is removed from the world." (**6075**) shows meanwhile (`LoginState::Kicking`). Its reply **0x32** is a base reply (LoginClient callback +0x78): success sends the play request for the same server again (10 s); failure shows the reply's error. A timeout is the canned reply: success 0, **103**. EQEmu's login server sends neither 302 nor 114 (its "already online" is 111, a plain refusal, sent only with the rule `World:EnforceCharacterLimitAtLogin`) and has no handler for 0x16, so on EQEmu this path is never taken; a kick request it got would time out with 103.

## Sequence numbers and pending requests

One counter per login connection (a new one for each connect attempt) starts at 1 and is incremented **before** each request is built, wrapping to 1 at 0xffffff: handshake 2, login 3, list 4, play 5; each list refresh takes the next number, so a play request after one refresh carries 6. Every request is pending with a timeout: handshake 10 s (nothing happens when it runs out), login 20 s and list 120 s (**103** "A timeout occurred"), play 10 s (**324**). A reply whose sequence is below 0xffffff must match a pending request, which it then removes, or it is dropped, unless its opcode is a push (0x25–0x2d, 0x30, 0x31); the reply's opcode is not compared with the request's. EQEmu echoes the request's sequence (and hard-codes 2 in the handshake reply, which a fresh connection always uses). A connection the server drops after the login: **112**.

Error texts come from eqlsstr_us.txt (`load_login_strings`, loaded by the client at start-up), with Hailstone's own English wording (not the stock file's text) for the ids the client uses when the file is missing; an id with no text reads "Login message <id> (no text for it).".

The `LoginClient` state machine: `start()` → Connecting → Handshake → Authenticating → **LoggedIn**; `request_server_list()` → RequestingList → **ServerList**; `request_play(id)` → RequestingPlay → **PlayAccepted**; any error → Failed with `error()` and `error_id()`.

## Hand-off to the world server

`LoginClient::ticket()` returns a `WorldTicket` once the play request is accepted:

- `world_host`: the IP string of the chosen server from the server list;
- `world_port`: 9000 (the RoF2 client always connects to world on 9000);
- `account_id`: the login account id from OP_LoginAccepted;
- `session_key`: the 10-character key from OP_LoginAccepted.

The world connection's first packet is OP_SendLoginInfo (0x7a09, 464 bytes): the account id as **decimal text**, `00`, the session key, `00`, …. EQEmu's world parses the name with `Strings::ToInt` and checks the pair with `ClientList::CheckAuth(id, key)`. The login session can be closed once the ticket is in hand.

## Launcher tickets

The playable client accepts a ticket supplied by a launcher:

```text
hailstone --game-dir /path/to/EverQuest patchme /ticket:ISSUED_TICKET
hailstone --server example --ticket-env LAUNCH_TICKET --world "Example World" --character Example
```

`--ticket T` is the explicit equivalent of `/ticket:T`. `--ticket-env VAR` reads the named environment variable
once and removes it from the client's environment (`platform::take_environment_variable`), so the box windows,
add-on tools and helpers the client starts do not inherit it; it keeps the ticket out of the command line, where
argument tickets can be seen. Supply exactly one ticket source. As in RoF2, a ticket takes precedence over `--user`,
`--password` and `--account`: those legacy arguments are cleared before client startup, so an existing launcher
template works without reading or sending password credentials. Tickets cannot be used with a viewer mode. The
ticket is opaque: the client does not interpret an issuer's prefix, claims, expiry, account or world restrictions.
The issuer and login server enforce those. While a ticket is on the command line, arguments the client does not
know are counted in the log but not named: a ticket split at a space would otherwise be logged.

The endpoint is the explicit saved login-server profile `--server`, the explicit `--host`/`--port`, or the
`eqhost.txt` of a game folder named for this run (`--game-dir` or `EQ_GAME_DIR`), in that order; explicit host/port
override the chosen endpoint. The asset folder remembered in `hailstone.ini` does not count: it may be another
server's, so without one of those the client refuses the ticket and sends nothing ("A login ticket needs its server
named for this run: --server, --host, or --game-dir"). An unknown named profile also fails before sending anything.
A profile matching the endpoint supplies the normal server assets and UI. With no profile, login is transient. The
default port for an explicit host remains `ServerProfile`'s 5997; specify `--port` when another port is required.

Without `--world`, ticket authentication stops at server select. `--world NAME|ID` asks for exactly one world by
case-insensitive full name or decimal login-server world id (a Hailstone option; RoF2 has no world switch). Missing
or ambiguous matches leave the list available with a message; another world is never substituted. `--character
NAME`, or RoF2's `/name:NAME`, enters that character at character select without a click; otherwise the player
chooses. Normal world admission, locked-server permissions and character checks apply.

RoF2's own switches (eqgame `InitGameInstance` @005fdec0, rof2decomp notes/connection-and-network.md §2.2):

| Switch | RoF2 | Hailstone |
|---|---|---|
| `/ticket:<t>` | @005fe56a: at most 63 bytes (strncpy 0x3f) to 0x00e666f8, one trailing `"` stripped | the ticket, one trailing `"` stripped; longer than 63 bytes is refused, not cut (a deliberate change) |
| `/name:<char>` | @005fe5e6: 63 bytes to 0x00e66738; `StartNetworkGame` selects that character, writes `Defaults/LastCharSel` and sends OP_EnterWorld without a click (@0053c0a5) | `--character` (at most 63 bytes); `LastCharSel` is not written |
| `/server:<host>` | @005fe5ba: the world host into `EQLogin` +0x58c, which eqmain may overwrite | taken and ignored: the world's address comes from the login server's play reply |
| `patchme` | @005fe54c: the started-by-the-patcher flag | taken and ignored |

`LoginClient::start_token` performs the regular session handshake, sends the token form of `OP_Login`, and follows
the ordinary accepted-login, server-list, play and world-ticket flow. Authentication starts while the application's
assets initialize so a short-lived ticket can be redeemed promptly. A ticket launch keeps the updater and add-on
fetching on, but does not let the updater act on the install at start (`update_at_start`, which can roll an update
back and start the other version without the command line): the ticket would be lost with that restart. The next
start without a ticket does that work; a release downloaded meanwhile is offered as on any run.

No ticket is saved to settings or the keyring, and no password is read, saved or substituted for that attempt. The
application and login client wipe their own copies of the ticket (the argument strings, the login client's buffers)
after sending and on cancellation, failure, replacement and destruction, and the decrypted login reply and token
result fields when released. Not everything is wiped: the encrypted OP_Login (DES with RoF2's fixed key, so as good
as plaintext to anyone who has the packet) stays in the session's reliable and fragment buffers until they are
reused, the process's argument strings stay as the system made them, and the system may keep copies of its own. A
reliable resend of the same encrypted request is part of the session protocol, not a second attempt.

A rejection, timeout or disconnect requires a fresh launcher ticket or an explicit interactive password login. Back
at the login screen the view is the normal one again: the saved server the ticket went to (else the one picked
before it) with its own remember, account and saved password, so the server list works and a password login saves
the profile as it was. A successful reply can supply a replacement token: Hailstone keeps that token only in memory,
bound to the issuing login host and port. After a normal logout the login screen offers **Sign in with session**
when that endpoint is selected (a ticket's server no profile has stays in the fields as a new, unsaved server for
it), in both UI packs and the fallback screen. This explicit action consumes the returned token once. Each
successful reply replaces it, including an empty replacement which clears it. A failure, a manual account/password
login or application exit forgets it. Changing endpoints cannot consume it. The original launcher ticket is never
put in this store or replayed.

Checked against the RoF2 client (eqgame `0x5fdec0`, eqmain token API `0x10013850`, password API `0x10013720`,
serializer `0x10020510`): case-insensitive `/ticket:`, a 64-byte launcher slot holding at most 63 ticket bytes, and
empty username/password strings followed by ticket, NUL and flag 1. The explicit `--ticket` and `--ticket-env`
transports and network API permit up to 4096 opaque bytes as a Hailstone extension; embedded NULs and empty tickets
are rejected. `--world` accepts 256 bytes. The token handshake has a bounded timeout as Hailstone policy; the
password path retains the native handshake behavior. Native login success also replaces the stored ticket with the
reply token (eqmain `0x10009950`); Hailstone requires an explicit session-sign-in action to use a replacement,
rather than automatically looping through failed token attempts.

Validation covers packet bytes, launcher parsing and endpoint/world selection, mock token authentication through
world character select, rejection and timeout without password fallback, cancellation/replacement, returned-token
rotation and endpoint binding, the login view after a failed ticket, and larger fragmented tickets. Live
launcher/server acceptance remains separate. The launcher remains responsible for ticket issuance and selecting the
Hailstone executable; native-client verification and injection hooks do not apply.

## Live check (2026-10-21, EQEmu login server on 127.0.0.1:5997)

`HAILSTONE_LIVE_LOGIN=127.0.0.1:5997 HAILSTONE_LIVE_REFRESH=1 hailstone_tests -tc="live login*"` (account nmscgrp1): login (sequence 3), list (4), the list again after 60 s (5), play (6). The login server logged `Play received from client [nmscgrp1] server number [2] sequence [6]` and answered with sequence 6; the client then entered the world and zoned in as before.

## Live check (2026-10-20)

```
$ netprobe login 192.0.2.10:5997 nmsctest1 nmsctest1 "Dev"
connecting to 192.0.2.10 port 5997 as nmsctest1
login accepted: account id 2, session key ##########, failed attempts 0, show player count no
1 server(s):
  id   name                           address          status         players type
  2    Dev                            192.0.2.10   up             76      1
requesting play on 2 (Dev)
play accepted (server 2)
world connection: 192.0.2.10:9000, OP_SendLoginInfo login "2" password "##########"
```

The login server logged `New SoD+ client connection`, created the account, `Successful login for user id [2]`, the play request with sequence 5, and `Sending play response`.
