# blockchain/proto

Protocol Buffers schema and generated bindings for blockchain wire messages.

## Notes

- Defines transaction/block batch payload formats exchanged over ZMQ.
- `*_PB` commands in runtime components depend on these message definitions.
- Keep schema and generated C bindings synchronized when changing protocol fields.
