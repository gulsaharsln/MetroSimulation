# Metro Tunnel Simulator

## Overview

This project simulates the operation of a metro system's tunnel traffic, incorporating dynamic queue management, signal handling, threading, and logging. Designed to efficiently manage the flow of trains through a critical tunnel section, it features real-time decision-making based on train arrivals, lengths, and system capacity.

## Features

### TrainNode Structure
The TrainNode custom data structure represents each train, encapsulating attributes like train ID and length, and the link to the next train. This structure is the backbone of our dynamic queue management system, allowing efficient handling of train sequences on each metro line.

### Signal Handling
A `signal_handler` function ensures graceful termination of the simulation in response to external signals (e.g., `SIGINT`). It sets a global `shutdown_flag` monitored by other parts of the program, enabling safe operations conclusion before exit.

### Core Functionalities
- **Process Train**: Simulates train journey through the tunnel, determining transit time based on the train's length and tunnel dimensions.
- **Queue Management**: Dynamic queues for each metro line are managed through `enqueue` and `dequeue` functions, facilitating train additions and removals.
- **Queue Size Monitoring**: The `get_queue_size` function provides real-time queue sizes, aiding in decision-making processes.

### Threading and Concurrency Management
Utilizing threading, the simulation handles train generation (`train_generator`) and tunnel traffic management (`tunnel_controller`). Trains are generated with unique IDs and random lengths at varying probabilities, ensuring a realistic flow through the network. The `tunnel_controller` oversees tunnel traffic, prioritizing trains based on a predefined order and managing tunnel occupancy.

### System Overload Management
To prevent system overload, an overload mechanism pauses train generation when more than 10 trains accumulate in the queues. This feature ensures the simulation remains realistic and manageable under high traffic conditions.

### Breakdown Simulation
Enhancing realism, a breakdown probability is factored into each train's journey. Randomly determined, breakdowns add additional seconds to a train's tunnel passage time, simulating real-world unpredictabilities.

### Logging System
Two log files (`train.log` and `control-center.log`) record simulation events and decisions. A separate logging thread, protected by mutex locks, handles log entries to ensure data integrity and provide insights into the simulation's operation and performance.
