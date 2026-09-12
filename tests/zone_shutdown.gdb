# Run only against a disposable server directory and database:
# gdb -q -batch -x /path/to/tests/zone_shutdown.gdb --args /path/to/zone misty test 65000
# Environment: EQEMU_SHUTDOWN_PHASE=process|startup|sleeping,
# EQEMU_SHUTDOWN_SIGNAL=SIGTERM|SIGINT|request, EQEMU_SHUTDOWN_REPEAT=0|1.
# EQEMU_SHUTDOWN_THREAD=main|worker selects the signal recipient during processing.
# For sleeping, pass "." as the zone name. The test returns nonzero on failure.
set pagination off
set confirm off
set print thread-events off
handle SIGTERM nostop noprint pass
handle SIGINT nostop noprint pass
handle SIGSEGV stop print nopass
handle SIGABRT stop print nopass
python
import gdb
import json
import os

phase = os.environ.get("EQEMU_SHUTDOWN_PHASE", "process")
signal = os.environ.get("EQEMU_SHUTDOWN_SIGNAL", "SIGTERM")
repeat = os.environ.get("EQEMU_SHUTDOWN_REPEAT", "0")
recipient = os.environ.get("EQEMU_SHUTDOWN_THREAD", "main")
assert phase in ("process", "startup", "sleeping")
assert signal in ("SIGTERM", "SIGINT", "request")
assert repeat in ("0", "1"), "EQEMU_SHUTDOWN_REPEAT must be 0 or 1"
repeat = repeat == "1"
assert not (repeat and phase == "sleeping")
assert recipient in ("main", "worker")
assert recipient != "worker" or (phase == "process" and signal != "request" and not repeat)
result = dict(phase=phase, signal=signal, repeat=repeat, reached=False,
              recipient=recipient, destructors=0, repeated=False, exit_code=None, crash=None)

def exited(event):
    result["exit_code"] = getattr(event, "exit_code", None)

def stopped(event):
    if isinstance(event, gdb.SignalEvent):
        result["crash"] = event.stop_signal

gdb.events.exited.connect(exited)
gdb.events.stop.connect(stopped)

class TeardownBreakpoint(gdb.Breakpoint):
    def stop(self):
        result["destructors"] += 1
        return repeat and not result["repeated"]

class TriggerBreakpoint(gdb.Breakpoint):
    hits = 0
    def stop(self):
        self.hits += 1
        # Allow spawn processing before interrupting an active zone frame.
        if phase == "process" and self.hits < 90:
            return False
        result["reached"] = True
        return True

# An address breakpoint avoids also matching BaseZoneRepository::Zone::~Zone().
TeardownBreakpoint("*'_ZN4ZoneD2Ev'")
trigger = TriggerBreakpoint({"process": "Zone::Process()",
                            "startup": "CheckForCompatibleQuestPlugins()",
                            "sleeping": "uv_run"}[phase])
gdb.execute("run")
if result["reached"]:
    trigger.delete()
    if recipient == "worker":
        main_thread = gdb.selected_thread()
        workers = [t for t in gdb.selected_inferior().threads() if t.num != main_thread.num]
        assert workers, "No worker thread is available for signal delivery"
        workers[0].switch()
    if signal == "request":
        # Exercise the entry point used by world shutdown messages.
        gdb.execute("call (void)Shutdown()")
        gdb.execute("continue")
    else:
        gdb.execute("signal " + signal)
    if repeat and result["destructors"] == 1 and result["crash"] is None:
        result["repeated"] = True
        # Advance past the breakpoint instruction so signal return cannot recount it.
        gdb.execute("stepi")
        # A second signal during teardown must not reenter the destructor.
        gdb.execute("signal SIGTERM")

if gdb.selected_inferior().pid:
    gdb.execute("bt 15")
    gdb.execute("kill")

expected_destructors = 0 if phase == "sleeping" else 1
passed = (result["reached"] and result["exit_code"] == 0
          and result["crash"] is None
          and result["destructors"] == expected_destructors
          and result["repeated"] == repeat)
result["passed"] = passed
print("SHUTDOWN_RESULT=" + json.dumps(result, sort_keys=True))
gdb.execute("quit " + ("0" if passed else "1"))
end
