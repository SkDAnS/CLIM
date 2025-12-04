# Copilot instructions for CLIM-jan_test (Projet1.0)

This file summarizes key architectural and implementation details for the *ISY* chat system and suggests focused guidance for AI coding agents working in this repository.

## Big picture
- Components:
  - `ServeurISY` (server): UDP server that manages groups (creation, listing, merging, deletion) and spawns `GroupeISY` processes.
  - `GroupeISY` (group): one UDP process by group that receives client registrations (CON) and broadcasts messages to group members.
  - `ClientISY` (client): CLI program sending server commands and group messages; starts an `AffichageISY` (display) process to receive group messages.
  - `AffichageISY` (display): listens on a UDP port provided by `ClientISY` to display incoming messages for that client.

- Communication: All inter-process communication uses UDP with a common `ISYMessage` structure in `include/Commun.h`.

## Key files & patterns
- `include/Commun.h`: central message struct and shared constants (ports, sizes, types). This now includes:
  - `ISYMessage` with fields: `ordre`, `emetteur`, `logo` (new), `groupe`, `texte`.
  - Orders: `ORDRE_CMD`, `ORDRE_RPL`, `ORDRE_CON`, `ORDRE_MSG`, `ORDRE_MGR` (management).
  - `GroupeInfo` has an added `pid` to track the child process running the group.
  - Helper functions like `create_udp_socket`, `fill_sockaddr`, and `choose_logo_from_username`.

- `src/ClientISY.c`:
  - Sends `ORDRE_CMD` commands such as `JOIN`, `CREATE`, `LIST`, `MERGE` to `ServeurISY`.
  - The client assigns a deterministic ASCII logo to the messages sent via `msg.logo = choose_logo_from_username(username)`.
  - Broadcast-based server discovery has been removed — the server IP is taken from the configuration file `config/client_template.conf`.
  - `ClientISY` starts `AffichageISY` (display) in a separate process to receive group messages.
  - A new menu option (4) allows sending `MERGE g1 g2 newname` to the server to request group merging.

- `src/ServeurISY.c`:
  - Keeps `GroupeInfo` array, starts `GroupeISY` processes with `fork` + `execl`.
  - Stores `pid` of the child process in `GroupeInfo` so it can kill or signal them (e.g., for `MERGE` or `DELETE`).
  - `MERGE` command: create a new group process, notify the two groups via `ORDRE_MGR` so they broadcast migration notice to clients, and then terminate the old group processes.
  - Broadcast discovery code has been removed (no `sock_broadcast` usage or `DISCOVERY_PORT`).

- `src/GroupeISY.c`:
  - Manages clients per group and broadcasts `ORDRE_MSG` messages to their `AffichageISY` ports.
  - Stores `logo` from `CON` messages for bookkeeping; broadcast keeps sender `msg.logo`.
  - Handles management messages (`ORDRE_MGR`) and broadcasts migration notices to clients.

- `src/AffichageISY.c`:
  - Prints incoming messages in the format: `[group] <logo> username : text`.
  - Continues receiving `ORDRE_MSG` messages; migration notices are displayed as messages from `SERVER`.

## Project-specific conventions
- UDP with `ISYMessage` struct for all communication; all `sendto`/`recvfrom` use `sizeof(ISYMessage)`.
- Deterministic ASCII logo: `choose_logo_from_username()` in `Commun.h` provides an easily understood token for each user.
- Merge command is implemented as server-side orchestration: `MERGE g1 g2 newname` creates a new group and asks old groups to inform their clients — clients are notified but do not auto-rejoin. Auto-join is a separate feature to implement.
- The repository uses POSIX/JNIC APIs: `fork`, `execl`, `shmget`, `shmat`, `kill`, `waitpid`.
- SHM (shared memory) is optional for stats (`GroupStats`) and client/affichage coordination. Group SHM is created on group creation and closed when group terminates.

## Developer workflows
- Build all binaries:

```bash
# from workspace root
make
```

- Run the server then client(s):

```bash
# start server
./bin/ServeurISY &
# start a client (each client will start its display process)
./bin/ClientISY
```

- Test merging scenario:
  1. Create two groups through two different clients (or the same client, one after the other): `CREATE group1`, `CREATE group2`.
  2. Ensure clients join both groups.
  3. From a client, request `MERGE group1 group2 newgroup`.
  4. The server creates `newgroup` and tells `group1` and `group2` processes to broadcast a migration notice to their connected clients. The old group processes are then terminated.
  5. Clients will see the migration notices; they can rejoin `newgroup` from their menu.

## Implementation notes and recommended improvements
- Client auto-join during merge (optional): To enable automatic rejoin when a user receives a migration notice, there are a few options:
  - Modify `AffichageISY` to forward migration notices to `ClientISY` (e.g., via a local socket, pipe, or running ClientAPI), then trigger a `JOIN newgroup` automatically.
  - Let `ClientISY` listen on its local display port as well as the interactive menu (use `select()` or threads) and perform `JOIN` when receiving a migration notice.
- Merging clients programmatically in the server would require the server to know client addresses and display ports — currently the server delegates that to `GroupeISY` processes. The chosen approach is simpler (notify clients via group processes) and avoids global cross-process client tracking.
- `ISYMessage` changes require all users of the struct to handle the new `logo` field — the codebase sets and forwards it, but any third-party integration must also handle it.
- The `choose_logo_from_username` helper in `Commun.h` is small and deterministic, making it easy to change logo selection strategy centrally.

## Tests to add
- Integration tests to verify merging and forward notification flow:
  - Start a server, create groups, join a few clients, MERGE groups and check that old groups die and new group is created.
  - Confirm clients receive migration notice (as display messages) and that clients can rejoin the new group.
- Unit tests for `choose_logo_from_username()` to ensure logos are deterministic and printable.

## Glossary & Commands
- Server Commands (client -> server): `LIST`, `CREATE <name>`, `JOIN <name>`, `DELETE <name>`, `MERGE <g1> <g2> <newname>`
- Group Messages (client->group): `CON` to connect, `MES` to send messages
- Management: `MGR` (server->group) used for migration notifications from server to group processes

---

If you want this document expanded or adapted to include additional developer details (e.g., CONTRIBUTING, test harness examples, simplified diagrams), tell me which sections to expand and I’ll iterate further.
