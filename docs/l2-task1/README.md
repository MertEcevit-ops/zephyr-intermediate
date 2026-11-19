# L2-TASK1: Protecting shared state with a mutex

Two threads at equal priority (5) each increment a shared `counter` 1000
times. Each increment is a read-modify-write with a 100 us busy wait between
the read and the write. Time slicing is set explicitly in `app/prj.conf`
(`CONFIG_TIMESLICE_SIZE=10`), so the scheduler switches between the two
threads every 10 ms, usually in the middle of an increment.

The mutex is controlled by `CONFIG_APP_COUNTER_USE_MUTEX` (`app/Kconfig`,
default `y`), so you can switch it without editing code:

```sh
# mutex-protected (default)
west build -p -b native_sim app
./build/zephyr/zephyr.exe -stop_at=1

# unprotected
west build -p -b native_sim app -- -DEXTRA_CONF_FILE=unprotected.conf
./build/zephyr/zephyr.exe -stop_at=1
```

## Results (native_sim, Zephyr 4.2.0)

| Mode            | Final counter | Expected | Lost         | Log                                          |
|-----------------|--------------:|---------:|-------------:|----------------------------------------------|
| mutex-protected |          2000 |     2000 |            0 | [run-mutex.log](run-mutex.log)               |
| unprotected     |          1099 |     2000 | 901 (45 %)   | [run-unprotected.log](run-unprotected.log)   |

## Why the unprotected version loses updates

`counter = counter + 1` is three steps: read, modify, write. The threads have
the same priority, so they never preempt each other directly. The only switch
point is the time-slice expiry in the timer interrupt, and that almost always
lands inside the busy wait, after a thread has read `counter` but before it
has written it back:

```
A: tmp = counter              (reads 500)
   -- A's slice expires, B runs --
B: increments 100 times       (counter 500 -> 600)
   -- B's slice expires, A runs --
A: counter = tmp + 1          (writes 501: B's 100 increments are lost)
```

This repeats at almost every switch, so about 100 increments are lost each
time over the ~20 slices of the run.

With the mutex, a slice can still expire inside the critical section, but the
other thread then blocks in `k_mutex_lock()` until the holder has written and
unlocked. No thread can read `counter` while another holds an unwritten copy
of it, so every increment is kept and the result matches the expected 2000.

native_sim is deterministic: simulated code takes zero time, so the slice
always expires inside `k_busy_wait()` and the unprotected result is 1099 on
every run. On real hardware the value changes between runs, but stays well
below 2000.

The same explanation is in the comments in `app/src/main.c`, at the
read/write in `counter_fn()` and at the final comparison in `main()`.
