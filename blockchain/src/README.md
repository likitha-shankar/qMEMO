# blockchain/src

Core implementation of the active blockchain runtime used in qMEMO experiments.

## Responsibilities

- process entry points (`main_*`) for blockchain, pool, metronome, validator, wallet
- transaction creation/signing/verification and protobuf serialization
- runtime dispatch across signature schemes via typed crypto backend
- state transitions, confirmation flow, and benchmark integration

This is the primary code path for PQC and hybrid experiments.
