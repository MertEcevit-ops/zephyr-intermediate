# L6-TASK1: Latency investigation and tracing

## Summary

| Run    | Control prio | Maintenance prio | Start latency per event | Deadline misses |
|--------|-------------:|-----------------:|------------------------:|----------------:|
| before |            7 |                4 |                   45 ms |     15/15       |
| after  |            3 |                4 |                    0 ms |      0/15       |

Both runs use the same source, workload (event every 250 ms, 45 ms
maintenance busy wait) and detector. The only difference is
`CONFIG_APP_CONTROL_PRIORITY` (`app/Kconfig`, default 3); the `before.conf`
overlay sets it back to the original 7. Platform: native_sim, Zephyr 4.2.0.

## What is measured

The timer ISR stamps each control event with `ready_ms = k_uptime_get_32()`
when it puts the event in `control_queue`. At that moment the control thread,
blocked in `k_msgq_get()`, becomes ready. When control actually gets the CPU
and takes the event, it computes

```
start latency = k_uptime_get_32() - event.ready_ms
```

That is how long control sat ready but not running. The guarantee is that
control work starts within `DEADLINE_MS` = 10 ms; any event with a larger
latency counts as a miss. `CONFIG_SYS_CLOCK_TICKS_PER_SEC=1000` gives the
millisecond uptime 1 ms resolution. native_sim's default of 100 ticks/s is as
coarse as the deadline itself.

## Evidence

All files below come from one run per configuration. Each run captured the
console, piped `kernel thread list` into the shell at about t = 2.1 s, and
wrote a CTF trace:

```sh
west build -p -b native_sim app [-- -DEXTRA_CONF_FILE=before.conf]
mkdir -p trace && cp $ZEPHYR_BASE/subsys/tracing/ctf/tsdl/metadata trace/
( sleep 2.1; printf 'kernel thread list\r\n'; sleep 3 ) | \
  ./build/zephyr/zephyr.exe -uart_stdinout -trace-file=trace/channel0_0 -stop_at=4.25
babeltrace2 --clock-seconds trace
```

The run stops at 4.25 s because the CTF timestamp is a 32-bit nanosecond
counter, which wraps at 4.29 s.

### 1. Latency output

- [latency-before.log](latency-before.log): every event starts 45 ms late,
  `misses=15/15`. The `deadline miss` warning is rate-limited to one per
  second.
- [latency-after.log](latency-after.log): every event starts at 0 ms,
  `misses=0/15`.

### 2. CTF trace around a delayed sequence number (seq=5)

Raw traces: [trace-before/](trace-before/), [trace-after/](trace-after/)
(`channel0_0` + `metadata`). Decoded excerpts:
[trace-before-seq5.txt](trace-before-seq5.txt),
[trace-after-seq5.txt](trace-after-seq5.txt).

Thread IDs: control = 134621024 (0x8062760), maintenance = 134620864
(0x80626c0), the same addresses `kernel thread list` prints. The semaphore
134620660 is `maintenance_start`.

Before (control priority 7), shortened and annotated. The full excerpt is in
the file linked above:

```
[1.750000000] isr_enter:                                    timer ISR fires
[1.750000000] semaphore_give_enter: { id = 134620660 }      maintenance_start given -> maintenance ready
[1.750000000] named_event: { name = "ctrl_ready", arg0 = 5, arg1 = 1750 }
                                                            seq 5 queued -> control ready, ready_ms = 1750
[1.750000000] isr_exit:
[1.750000000] thread_switched_in: { name = "maintenance" }  scheduler picks maintenance (prio 4 beats 7)
[1.760000000] isr_enter: / isr_exit:                        10 ms shell RX poll timer interrupts;
[1.770000000] isr_enter: / isr_exit:                        after each one the scheduler returns
[1.780000000] isr_enter: / isr_exit:                        to maintenance, control is still
[1.790000000] isr_enter: / isr_exit:                        ready and waiting
[1.795000000] semaphore_take_blocking: { id = 134620660 }   maintenance finishes its 45 ms busy wait and blocks
[1.795000000] thread_switched_out: { name = "maintenance" }
[1.795000000] thread_switched_in: { name = "control" }      control finally runs, 45 ms after ready
[1.795000000] named_event: { name = "ctrl_done", arg0 = 5, arg1 = 45 }   latency = 45 ms -> miss
```

For 45 ms, from 1.750 to 1.795, maintenance is running while control is ready.
Control is not blocked on anything; it is simply not chosen, because a
higher-priority thread is ready.

After (control priority 3), same instant, shortened:

```
[1.750000000] named_event: { name = "ctrl_ready", arg0 = 5, arg1 = 1750 }
[1.750000000] thread_switched_in: { name = "control" }      control is picked first
[1.750000000] named_event: { name = "ctrl_done", arg0 = 5, arg1 = 0 }    latency = 0 ms
[1.750000000] thread_switched_in: { name = "maintenance" }  maintenance runs right after
[1.795000000] semaphore_take_blocking: { id = 134620660 }   and still finishes at 1.795
```

### 3. Kernel shell output

- [thread-list-before.txt](thread-list-before.txt): `control` priority **7**,
  `maintenance` priority **4**, so control is the lower-priority thread.
- [thread-list-after.txt](thread-list-after.txt): `control` priority **3**,
  `maintenance` priority **4**, so control is now the higher-priority thread.

In both lists the two threads are in state `pending`: control is blocked in
`k_msgq_get()` and maintenance in `k_sem_take()`, waiting for the next event.
The shell thread (`shell_uart`, priority 14) can only run when both of them
are blocked, so the shell never sees the "control ready, maintenance running"
window. The trace above shows that window.

## Root cause

Each timer event wakes both threads at the same instant: `k_msgq_put()`
readies control, and `k_sem_give(&maintenance_start)` readies maintenance.
Zephyr's scheduler always runs the highest-priority ready thread. With control
at 7 and maintenance at 4, maintenance wins every time and runs its 45 ms
`k_busy_wait()` to completion. Control stays ready in the run queue for that
whole time, so its start latency is 45 ms against a 10 ms deadline, on every
event (15/15 misses).

Nothing is blocking control and the CPU is not overloaded. Maintenance uses
45 ms out of every 250 ms, about 18 %. The priorities are simply inverted
relative to the deadlines: the thread with the 10 ms deadline has a lower
priority than the thread with no short deadline.

## Why changing CONTROL_PRIORITY fixes it

With `CONTROL_PRIORITY` = 3, control has a higher priority than maintenance
(4). When the ISR readies both threads, the scheduler runs control first, and
if maintenance were already running, control would preempt it. So the start
latency drops to the time needed to switch to control: 0 ms at 1 ms resolution
(15/15 met). Maintenance loses nothing that it needs. It has no short
deadline, starts after control's short burst of work, and still completes at
the same time (1.795 s in both traces). The total work is unchanged; only the
order changed.
