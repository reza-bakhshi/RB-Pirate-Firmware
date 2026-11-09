# Spec Delta

## Purpose

The protocol job runtime enables Core 0 to asynchronously dispatch hardware protocol operations to Core 1, stream output and progress events across lock-free queues, enforce hard execution deadlines, and guarantee bounded cancellation latency under 100 milliseconds.

## ADDED Requirements

### Requirement: Asynchronous Job Submission and Dispatch
The system SHALL allow Core 0 to submit protocol jobs to Core 1 asynchronously without blocking the user terminal interface or prompt loop.

#### Scenario: Non-blocking submission
- **WHEN** Core 0 submits a protocol job to the job queue
- **THEN** Core 0 immediately continues executing the prompt loop while Core 1 transitions the job to RUNNING state

#### Scenario: Queue busy rejection
- **WHEN** Core 0 attempts to submit a job while an active job is already running
- **THEN** the submission returns a busy status error and does not overwrite the active job context

### Requirement: Progress and Output Event Streaming
The system SHALL stream job progress percentages and output records from Core 1 to Core 0 across lock-free rings without using printf from Core 1.

#### Scenario: Periodic progress streaming
- **WHEN** Core 1 updates the progress percentage of an executing protocol job
- **THEN** a progress event is delivered to Core 0 and displayed on the active terminal without blocking Core 1

#### Scenario: Output record emission
- **WHEN** a protocol job produces output bytes or diagnostic text
- **THEN** output records are transferred to Core 0 in strict FIFO order for console presentation

### Requirement: Deterministic Deadline and Timeout Enforcement
The system SHALL enforce hard deadlines on running protocol jobs and terminate overdue jobs cleanly without hanging the system.

#### Scenario: Job deadline expiration
- **WHEN** a running protocol job exceeds its configured timeout duration
- **THEN** Core 1 aborts the job execution, sets status to TIMEOUT, and reports completion to Core 0

### Requirement: Bounded Cancellation Latency
The system SHALL guarantee that an active protocol job terminates within 100 milliseconds upon cancellation request via Ctrl+C, user button, or USB disconnect.

#### Scenario: Ctrl+C aborts active job
- **WHEN** a user transmits Ctrl+C while a protocol job is executing
- **THEN** the job aborts within 100 milliseconds, transitions to CANCELLED state, and returns the terminal prompt to IDLE

#### Scenario: USB disconnect aborts active job
- **WHEN** the USB connection is lost during job execution
- **THEN** the active job is immediately cancelled and hardware resources are returned to safe idle states
