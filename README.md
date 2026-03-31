# Distributed Elevator Control System (C, POSIX IPC & TCP)

## Overview

This project simulates a **distributed elevator control system** using low-level C programming, combining **TCP networking** with **POSIX shared memory and synchronization**.

The system models real-world elevator behaviour including:

* Car movement between floors
* Door state transitions
* Call handling (external and internal)
* Safety system monitoring

---

## Architecture

The system is composed of multiple interacting processes:

* **Controller (TCP Server)**
  Manages elevator logic and assigns floor requests

* **Car Process**
  Simulates elevator state (position, doors, movement)

* **Call System**
  Sends floor requests to the controller

* **Internal Controls**
  Simulates button presses inside the elevator

* **Shared Memory Interface**
  Enables real-time communication between processes using:

  * `pthread_mutex`
  * `pthread_cond`

---

## Technologies Used

* C (low-level systems programming)
* POSIX Shared Memory (`shm_open`)
* Multithreading (`pthread`)
* TCP/IP Networking
* Inter-process communication (IPC)

---

## Key Features

* Real-time process communication using shared memory
* TCP-based command protocol between controller and elevator
* Thread-safe synchronization using mutexes and condition variables
* Simulation of realistic elevator states:

  * Opening / Closing / Moving / Idle
* Safety system integration

---

## How to Run

```bash
make
./controller
./car
```

Run additional components in separate terminals as needed.

---

## Learning Outcomes

This project demonstrates:

* Systems programming in C
* Concurrent programming and synchronization
* Network communication protocols
* Real-time system design concepts

---

## Future Improvements

* GUI visualisation of elevator movement
* Multi-elevator coordination system
* Improved scheduling algorithms
* Fault detection and recovery

---


