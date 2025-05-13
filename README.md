# Process Scheduling Simulation

A multithreaded OS scheduler simulator written in C that models multiple CPUs and supports four scheduling algorithms. Built on top of the provided **os‑sim** framework, it safely manages concurrent CPU threads and a shared ready queue using POSIX threads, mutexes, and condition variables.

**Repository:**

```bash
git clone https://github.com/Minhong-Portfolio/Process-Scheduling-Simulation.git
cd Process-Scheduling-Simulation
```

## Features

* **FCFS (First‑Come, First‑Served)**
* **Round‑Robin** with configurable timeslice (`-r <ticks>`)
* **Preemptive Priority with Aging** (`-p <age_weight>`)
* **Shortest Remaining Time First (SRTF)**
* Thread‑safe shared ready queue with `pthread_mutex_t` & `pthread_cond_t`
* Context switches via `context_switch()` and `force_preempt()` APIs
* Live Gantt chart output and final statistics for context switches and time-in-state

## Prerequisites

* GCC (C99)
* Make
* POSIX‑compatible OS (Linux, macOS, WSL)

## Building

```bash
# Build in optimized (release) mode
make

# For debugging symbols
make debug

# To run thread‑sanitizer checks
make tsan-debug
```

## Usage

```bash
# FCFS on 4 CPUs
./os-sim 4

# Round‑Robin with a 5‑tick timeslice
./os-sim 4 -r 5

# Priority with aging weight 2
./os-sim 4 -p 2

# Shortest Remaining Time First
./os-sim 4 -s
```

### Command‑Line Options

* `<#CPUs>` (1–16): number of simulated CPUs
* `-r <timeslice>`: Round‑Robin scheduling
* `-p <age_weight>`: Priority scheduling with aging
* `-s`: Shortest Remaining Time First
* `-h`: Display help and usage

## Project Structure

```
├── src/                     # Source code and headers
│   ├── student.c            # Scheduling logic and handlers
│   ├── student.h            # Ready queue, scheduling APIs
│   ├── os-sim.h             # Simulator framework interface
│   ├── process.h            # PCB and op definitions
│   └── Makefile             # Build rules
├── examples/                # Example trace files (if any)
└── README.md                # This file
```

## Example Output

```
Time  Ru Re Wa     CPU0     CPU1     CPU2     CPU3     < I/O Queue <
0.0   0  4  0      (IDLE)   (IDLE)   (IDLE)   (IDLE)   <            <
0.1   1  3  0      Iapache   Ibash    Imozilla  Ccpu    <            <
...

Total Context Switches: 42
Total Execution Time:  3.4 s
Time in READY state:   2.1 s
```

---

*Implemented by Minhong Cho (GTID: 903806863)*
