# blockchain/include

Public headers for the active qMEMO blockchain implementation (PQC-enabled path).

## Key Interfaces

- transaction model and validation-facing structures
- wallet and cryptographic key/signature APIs
- blockchain ledger and consensus-facing types
- crypto backend abstraction for `SIG_SCHEME=1/2/3/4`

Use this folder as the contract layer for code in `blockchain/src/`.
