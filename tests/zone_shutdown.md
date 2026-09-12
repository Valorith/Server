# Test zone process shutdown

`zone_shutdown.gdb` interrupts a real zone process at a known execution point. It checks that the process exits with code zero and destroys the zone exactly once. A sleeping process must exit without destroying a zone. The test returns nonzero if a crash occurs or the expected execution point is never reached.

Run this integration test on Linux with GDB's Python support and a zone binary that retains its function symbols. Prepare a disposable server directory, database, and shared memory files. Use a valid private zone instance, enable `Zone:StateSavingOnShutdown`, and keep its network separate from running servers. The test launches a new process and can save or change its database state.

From that private server directory, run:

```sh
timeout 45s gdb -q -batch -x /path/to/source/tests/zone_shutdown.gdb \
	--args /path/to/zone misty shutdown_test 65000
```

The default case delivers SIGTERM during `Zone::Process()` after 90 calls. The original unsafe signal handler saves and destroys the zone, resumes the interrupted function, and crashes. The fixed handler records a request and lets the main loop finish the callback before teardown.

Repeat the command with these environment variables:

| Variables | Behavior checked |
| --- | --- |
| `EQEMU_SHUTDOWN_SIGNAL=SIGINT` | Interrupt during gameplay |
| `EQEMU_SHUTDOWN_THREAD=worker` | Deliver SIGTERM to a worker while the main thread is processing |
| `EQEMU_SHUTDOWN_REPEAT=1` | Second SIGTERM during the zone destructor |
| `EQEMU_SHUTDOWN_SIGNAL=request` | `Shutdown()` entry point used by world messages |
| `EQEMU_SHUTDOWN_PHASE=startup` | Signal after handlers are installed, before zone boot |
| `EQEMU_SHUTDOWN_PHASE=sleeping` | Signal with no loaded zone; replace `misty` with `.` |

Check the final `SHUTDOWN_RESULT` JSON and process status. For save-state coverage, inspect `zone_state_spawns` after shutdown, restart the same instance, and verify that saved NPC state is restored. World zone-list disappearance alone does not establish a clean process exit.

Repeat with `Zone:StateSavingOnShutdown` disabled and confirm that shutdown creates no saved-state rows for the private instance.

The worker case requires the process to have started at least one worker thread. Invalid test options, unavailable breakpoints, and debugger errors also produce a nonzero test status.
