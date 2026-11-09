# Spec Delta

## Purpose

The service kernel provides a deterministic, dual-core cooperative execution runtime featuring statically allocated event queues, 1 ms soft timers, deferred work queues, safe inter-core logging, and bounded-latency cancellation.

## ADDED Requirements

### Requirement: Fixed-Size SPSC Event Dispatch
The system SHALL provide fixed-size SPSC event queues for ISR-to-core, core-to-core, and intra-core event posting without dynamic memory allocation.

#### Scenario: Event queue delivery
- **WHEN** a producer enqueues an event into the service event queue
- **THEN** the consumer retrieves the event in FIFO order without blocking when capacity remains

#### Scenario: Event queue overflow handling
- **WHEN** a producer attempts to enqueue an event into a full queue
- **THEN** the enqueue fails with an overflow error code and increments the event drop counter without allocating heap memory

### Requirement: Deterministic 1 ms Soft Timers
The system SHALL provide 1 ms resolution soft timers that execute registered non-blocking callbacks within the cooperative main loop context.

#### Scenario: One-shot timer expiration
- **WHEN** a configured one-shot soft timer duration elapses
- **THEN** the registered callback executes exactly once in the cooperative loop context

#### Scenario: Periodic timer repeat
- **WHEN** a periodic soft timer period elapses
- **THEN** the registered callback executes and the timer automatically re-arms for the next interval

### Requirement: Deferred Work Queue Execution
The system SHALL provide a cooperative deferred work queue enabling background tasks to execute across cooperative kernel slices without starvation.

#### Scenario: Deferred work item dispatch
- **WHEN** a work item is submitted to the deferred work queue
- **THEN** the kernel executes the work handler in the next cooperative slice without blocking higher-priority tasks

#### Scenario: Cooperative work cancellation
- **WHEN** a pending work item is cancelled before execution
- **THEN** the work item is removed from the active queue and its handler does not execute

### Requirement: Cancellable Long Operations
The system SHALL provide cooperative cancellation tokens that abort long-running tasks within 100 milliseconds upon user interrupt, button press, or USB disconnection.

#### Scenario: User Ctrl+C cancellation
- **WHEN** a user transmits Ctrl+C during a cancellable long operation
- **THEN** the operation checks the cancellation token and exits cleanly within 100 milliseconds returning a cancelled status

#### Scenario: USB disconnect abort
- **WHEN** the USB CDC interface disconnects while an operation is running
- **THEN** the operation aborts and the system returns to the idle command prompt state

### Requirement: Safe Asynchronous Log Ring
The system SHALL provide a leveled log ring buffer allowing Core 1 and ISR contexts to record log entries without blocking or invoking printf.

#### Scenario: Core 1 posts log entry
- **WHEN** Core 1 records a diagnostic log entry with level and tag
- **THEN** the entry is safely appended to the ring buffer and Core 0 drains it to the active console

#### Scenario: Log ring overflow policy
- **WHEN** log entries are generated faster than the console drains
- **THEN** the oldest unread log entries are overwritten and an overflow counter is incremented

### Requirement: Kernel Health and Metrics Tracking
The system SHALL maintain real-time health counters tracking event dispatch counts, queue high-water marks, timer overruns, and loop latency.

#### Scenario: Kernel statistics inspection
- **WHEN** the user executes the kernel stats command
- **THEN** the system displays total ticks, event throughput, queue high-water utilization, and timer overrun counts
