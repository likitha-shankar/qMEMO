# benchmarks/src

Source code for cryptographic microbenchmarks and multicore scaling tests.

## Notes

- Each program measures one or more operations such as keygen/sign/verify.
- Timing uses monotonic clocks and reports throughput-oriented metrics.
- Shared helpers/constants are defined in common benchmark utility headers.

Build from `benchmarks/` using `make all`.
