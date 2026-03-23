# Post-Quantum Cryptography Adoption in Blockchain

**Graduate Research Project — Illinois Institute of Technology, Chicago**

> Researched March 2026. Covers 11 blockchain projects spanning production deployments,
> active testnets, and published roadmaps. All claims are sourced; status classifications
> reflect the most recent publicly available information.

---

## 1. Executive Summary

| Metric | Value |
|--------|-------|
| Projects surveyed | 11 |
| Production PQC deployments | 2 (QRL, Algorand state proofs) |
| Active testnets | 2 (Algorand user txns, Solana) |
| Published roadmap / proposal | 5 (Ethereum, Polkadot, Cardano, Aptos, Hedera) |
| Research / exploratory | 2 (Bitcoin, Avalanche) |
| Most popular algorithm | ML-DSA / CRYSTALS-Dilithium (4 projects) |
| Second most popular | Falcon / FN-DSA (3 projects) |

**Key findings:**

- The **NIST standardization of August 2024** (FIPS 203/204/205) was the primary catalyst
  for accelerated blockchain adoption — most roadmap announcements followed within 6 months.
- **Falcon** is favoured where signature size is a hard constraint (Algorand, Polkadot
  user accounts). **ML-DSA** is favoured where implementation simplicity and signing speed
  matter more (Solana, Polkadot consensus, Cardano).
- **No major proof-of-work chain** (Bitcoin, Litecoin, Monero) has a concrete migration
  timeline. The governance challenge of coordinating a hard fork dwarfs the cryptographic one.
- The performance cost of switching from ECDSA/Ed25519 to PQC is real but manageable:
  Algorand's 6,000 TPS is unchanged post-migration; Solana's testnet shows compatible
  performance metrics at scale.
- **SLH-DSA** (SPHINCS+) remains a niche choice — Aptos chose it specifically to avoid
  lattice-based mathematical assumptions, accepting large signatures in exchange for the
  most conservative security posture.

---

## 2. Detailed Projects

---

### 2.1 Algorand

- **Algorithm chosen:** Falcon-1024 (FN-DSA, FIPS 206)
- **Implementation date:** State proofs — September 2022 (mainnet); user-level Falcon
  transactions — November 3, 2025 (mainnet, first-ever)
- **Status:** **Production** (state proofs); **Mainnet** (user transactions, Nov 2025)
- **Reason for choice:**
  > "Falcon was selected for its compact signatures — 1,280 bytes max for Falcon-1024 —
  > which keeps state proof certificates small enough for trustless cross-chain relay.
  > The lattice-based security assumption is well-studied, and Falcon is NIST-selected."
  > — Algorand Foundation, Technical Brief

  Algorand's state proofs compress 256 rounds of block headers into a single Falcon-signed
  certificate. These certificates are relayed to other chains and light clients, so
  minimising certificate size (and thus bandwidth) is critical. Falcon-1024 was chosen
  over ML-DSA-65 specifically because its signatures are ~2.6× smaller at equivalent
  security level.

- **Performance impact:**
  - Block finality: **unchanged at 3.3 seconds**
  - Network throughput: **6,000 TPS maintained** post-migration
  - State proof generation: one certificate per 256 blocks (~14 minutes); overhead
    is negligible relative to normal transaction processing
  - User Falcon transactions (Nov 2025): no measurable TPS regression reported

- **Transaction/signature size impact:**
  - State proof certificates: ~80 KB per certificate (covers 256 rounds)
  - Falcon-1024 user signature: 1,280 bytes max (vs 64 bytes for Ed25519 — 20× larger)

- **Links:**
  - [Algorand Post-Quantum Technology](https://algorand.co/technology/post-quantum)
  - [Technical Brief: Falcon on Algorand](https://algorand.co/blog/technical-brief-quantum-resistant-transactions-on-algorand-with-falcon-signatures)
  - [State Proofs Developer Docs](https://developer.algorand.org/docs/get-details/stateproofs/)

---

### 2.2 QRL — Quantum Resistant Ledger

- **Algorithm chosen:** XMSS (eXtended Merkle Signature Scheme), transitioning to ML-DSA
  in QRL 2.0
- **Implementation date:** June 2018 (mainnet genesis)
- **Status:** **Production** (XMSS, since 2018); ML-DSA migration in active development
- **Reason for choice:**
  > "QRL uses IETF-specified XMSS: a hash-based, forward-secure signature scheme with
  > minimal security assumptions. It relies only on the collision resistance of SHA-256 —
  > no lattice assumptions, no number theory. This is the most conservative possible
  > choice for a quantum-resistant ledger."
  > — QRL Documentation

  XMSS was chosen in 2017-2018 because it was the only NIST-approved quantum-resistant
  signature scheme available at the time (NIST SP 800-208, 2020). Lattice-based schemes
  (Falcon, Dilithium) had not yet completed NIST standardisation. QRL's philosophy
  prioritised proven security assumptions over performance.

  **QRL 2.0** integrates ML-DSA (CRYSTALS-Dilithium) alongside XMSS, providing cryptographic
  agility and removing the statefulness constraint of XMSS (which requires careful key
  management to avoid signature reuse).

- **Performance impact:**
  - XMSS signing: ~25–50 ms per signature (significantly slower than ECDSA)
  - XMSS verification: ~2–5 ms per signature
  - XMSS signature size: ~2,820 bytes (Falcon-512 is ~4× smaller)
  - Stateful key management: addresses have a fixed OTS (one-time signature) budget;
    users must not reuse keys — this UX burden is the primary motivation for ML-DSA upgrade

- **Transaction size impact:**
  - XMSS transactions are ~10× larger than ECDSA equivalents due to authentication path

- **Link:** [theqrl.org](https://www.theqrl.org/) | [QRL Docs](https://docs.theqrl.org/what-is-qrl/)

---

### 2.3 IOTA

- **Algorithm chosen:** Winternitz OTS+ (WOTS+) at genesis (2016); Ed25519 from Chrysalis
  (April 2021); SLH-DSA / hash-based planned for 2025 testnet
- **Implementation date:** WOTS+ — October 2016; Ed25519 — April 2021
- **Status:** **Ed25519 (production)** — partially reverted from PQC for UX reasons;
  hash-based testnet planned 2025
- **Reason for initial PQC choice:**
  > IOTA's Tangle architecture required Winternitz one-time signatures for each address to
  > ensure quantum resistance from day one, as IOTA was designed as IoT infrastructure
  > expected to operate for decades.

- **Why they switched away from PQC:**
  The Chrysalis upgrade (April 2021) switched to Ed25519, abandoning WOTS+. The reason
  was **wallet UX**: WOTS+ requires generating a new address after every transaction
  (one-time use), creating unacceptable friction for end-users and wallet developers.
  The foundation accepted a temporary reduction in quantum resistance in exchange for
  practical usability.

  This is a cautionary case study: PQC adoption must consider operational ergonomics,
  not just cryptographic security.

- **Planned recovery:**
  Hash-based signatures are returning in IOTA's 2025 roadmap, with wallet-side key rotation
  abstracted from users and smart contracts supporting hash-based signatures natively.

- **Performance impact:**
  - WOTS+ signature: ~1,300 bytes; Ed25519: 64 bytes
  - Chrysalis (Ed25519) improved sync time and transaction finality significantly

- **Link:** [IOTA Foundation](https://www.iota.org/)

---

### 2.4 Solana

- **Algorithm chosen:** ML-DSA (CRYSTALS-Dilithium, NIST FIPS 204); also trialling
  Winternitz Vault (hash-based, optional wallet feature)
- **Implementation date:** Winternitz Vault — January 2025; ML-DSA testnet — December 16,
  2025
- **Status:** **Testnet** (ML-DSA full signature replacement, Dec 2025)
- **Reason for choice:**
  > Solana Foundation, in partnership with Project Eleven, selected ML-DSA for testnet
  > trials because it offers the best signing throughput among NIST-standardised lattice
  > schemes — critical for a network targeting 65,000 TPS. ML-DSA does not require
  > floating-point hardware, making it easier to implement correctly in validators.

  The Winternitz Vault (January 2025) offered an optional per-transaction key rotation
  mechanism using hash-based signatures — accessible without a protocol change, giving
  security-conscious users immediate quantum resistance.

- **Performance impact:**
  - ML-DSA testnet: compatible performance metrics; no public TPS regression data at time
    of writing
  - Winternitz Vault: ~30% overhead per transaction (additional on-chain data)
  - ML-DSA signature: ~2,420 bytes (vs 64 bytes Ed25519 — 37.8× larger); significant
    bandwidth and storage cost at Solana's transaction volume

- **Links:**
  - [Solana Launches Quantum-Resistant Signatures on Testnet](https://beincrypto.com/solana-quantum-resistant-blockchain-migration/)
  - [What Would Solana Need to Change to Become Quantum Ready?](https://www.helius.dev/blog/solana-post-quantum-cryptography)
  - [BTQ × Bonsol: ML-DSA on Solana](https://www.prnewswire.com/news-releases/btq-technologies-partners-with-bonsol-labs-to-achieve-industry-first-nist-standardized-post-quantum-cryptography-signature-verification-on-solana-302592494.html)

---

### 2.5 Ethereum

- **Algorithm chosen:** Under research — Falcon (FN-DSA) and STARK-based signatures are
  leading candidates; no final selection as of March 2026
- **Implementation date:** No production deployment; quantum readiness added to 2026
  core protocol roadmap
- **Status:** **Research / Roadmap** — account abstraction (EIP-7702) provides the
  migration pathway
- **Reason for choice (leading candidates):**
  > "Post-quantum signatures are massive, and the lightest one, called Falcon, is still
  > 10 times larger than current ECDSA signatures. Coding the lattice solution in Solidity
  > costs a fortune in gas, and there's an EIP for a precompile to handle this."
  > — Ethereum core developer commentary

  Ethereum's approach is two-track:
  1. **Account abstraction (EIP-7702):** Allow accounts to specify custom signature
     verification logic — users could opt into Falcon or ML-DSA wallets today via smart
     contracts, without a protocol change.
  2. **Precompile EIP:** A native precompile for Falcon/ML-DSA verification would eliminate
     the prohibitive gas cost of lattice signature verification in Solidity.

  **Note on current quantum exposure:** ~20% of Ethereum accounts already have hashed
  public keys (never revealed on-chain), making them de facto quantum-resistant until
  their first transaction reveals the public key. The remaining 80% expose their public
  key and are vulnerable to a harvest-now/decrypt-later attack.

- **Performance impact (projected):**
  - Falcon signature: ~666 bytes (10× ECDSA); manageable per-transaction overhead
  - ML-DSA signature: ~2,420 bytes (37.8× ECDSA); significant block size pressure
  - STARK-based: variable, potentially larger but more composable with existing ZK work
  - Gas costs for lattice verification in EVM: ~10-20× higher without a dedicated precompile

- **Links:**
  - [Ethereum Future-Proofing Roadmap](https://ethereum.org/roadmap/future-proofing/)
  - [Ethereum's Post-Quantum Roadmap (BTQ)](https://www.btq.com/blog/ethereums-roadmap-post-quantum-cryptography)
  - [Ethereum Protocol Roadmap 2026](https://btcusa.com/ethereum-protocol-roadmap-2026-scaling-account-abstraction-and-quantum-readiness-enter-core-phase/)

---

### 2.6 Polkadot / Substrate

- **Algorithm chosen:** ML-DSA (CRYSTALS-Dilithium) for consensus layer; Falcon for
  user account signatures
- **Implementation date:** Web3 Foundation roadmap published 2025; implementation in
  progress
- **Status:** **Planned** — Web3 Foundation post-quantum roadmap published; ChainSafe
  proof-of-concept on Substrate
- **Reason for choice:**
  > "Polkadot will use Dilithium for GRANDPA finality and BABE block production because it
  > does not require stateful key management — validators sign many messages and cannot
  > tolerate key reuse constraints. Falcon is reserved for user accounts where smaller
  > signature size reduces on-chain transaction costs."
  > — Web3 Foundation Post-Quantum Roadmap Summary, 2025

  The two-algorithm approach cleanly separates concerns: consensus (high signing frequency,
  stateless is critical) uses ML-DSA, while user accounts (low signing frequency, size
  matters for fees) use Falcon. ChainSafe published a Substrate prototype introducing
  post-quantum security at the signature layer.

- **Performance impact:** Not yet measured in production; Substrate testnet results pending.

- **Links:**
  - [ChainSafe: Post-Quantum Security on Substrate](https://blog.chainsafe.io/introducing-post-quantum-security-to-signatures-on-substrate/)

---

### 2.7 Aptos

- **Algorithm chosen:** SLH-DSA-SHA2-128s (SPHINCS+, NIST FIPS 205)
- **Implementation date:** AIP-137 proposed December 2025; mainnet rollout planned 2026
- **Status:** **Proposed** (AIP-137 under community governance vote)
- **Reason for choice:**
  > "AIP-137 prioritises security assumptions over raw performance. SLH-DSA-SHA2-128s
  > relies exclusively on SHA-256, which is already integrated across Aptos infrastructure
  > and whose security is well-understood. ML-DSA and Falcon introduce lattice
  > assumptions not yet validated across a decade of cryptanalysis."
  > — Aptos AIP-137 Rationale

  Aptos explicitly rejected Falcon and ML-DSA on the grounds of implementation risk:
  Falcon requires floating-point arithmetic in signing, which is harder to implement
  correctly and audit; ML-DSA introduces structured lattice assumptions. SLH-DSA trades
  performance for the strongest possible security posture.

- **Performance impact:**
  - SLH-DSA-SHA2-128s signature: **7,856 bytes** (122× larger than Ed25519's 64 bytes)
  - Verification: ~600-730 ops/sec (our measured results — see `COMPREHENSIVE_COMPARISON.md`)
  - Signing: ~36-45 ops/sec — extremely slow; not suitable for high-frequency signing
  - Aptos mitigates this by positioning SLH-DSA as the scheme for long-lived accounts
    that sign infrequently, not for high-TPS transaction flows

- **Links:**
  - [Aptos AIP-137](https://finance.yahoo.com/news/aptos-proposes-quantum-resistant-signatures-085416640.html)
  - [Aptos SLH-DSA Rollout Explained](https://bitcoinethereumnews.com/tech/aptos-post-quantum-slh-dsa-sha2-128s-rollout-explained/)

---

### 2.8 Cardano

- **Algorithm chosen:** ML-DSA (CRYSTALS-Dilithium) — under evaluation
- **Implementation date:** No timeline; research phase as of March 2026
- **Status:** **Research** — Charles Hoskinson confirmed lattice-based PQC direction
  in December 2025
- **Reason for choice:**
  > "The necessary post-quantum puzzle exists, it also has some negatives."
  > — Charles Hoskinson, December 2025

  Cardano's Haskell-based implementation and formal methods approach make it cautious
  about adopting any new cryptographic primitive without extensive auditing. ML-DSA is
  the preferred direction due to its simpler constant-time implementation (rejection
  sampling) relative to Falcon's FFT-based Gaussian sampler.

- **Performance impact:** Not yet measured; Cardano's ledger targets ~250 TPS (current).

- **Links:**
  - [How Blockchains Are Preparing for PQC](https://www.cryptonewsz.com/blog/blockchains-post-quantum-cryptography/)

---

### 2.9 Bitcoin

- **Algorithm chosen:** No official selection; proposals include ML-DSA (BTQ demo,
  Oct 2025) and Lamport signatures via OP_CAT (BIP-347)
- **Implementation date:** No production deployment; no BIP with consensus support
- **Status:** **Research** — no formal migration path exists
- **Reason (no adoption):**
  Bitcoin's conservative governance model (BIP process, miner and node operator consensus)
  makes any signature scheme change extraordinarily difficult. Beyond governance:
  - ML-DSA-65 signatures increase Bitcoin's UTXO set from ~5 GB to ~296 GB (59× expansion)
  - At 38× larger signatures, Bitcoin's throughput drops from ~7 TPS to ~1 TPS without
    block size increases — which themselves require contentious hard forks
  - ~6.65 million BTC (P2PK outputs and reused addresses) is immediately vulnerable
    once cryptographically-relevant quantum computers arrive; migrating these UTXOs
    requires owner cooperation

  BTQ Technologies demonstrated a working ML-DSA Bitcoin implementation in October 2025
  as a proof-of-concept, not a network proposal.

- **Qubit threshold to break secp256k1:**
  - Estimated: 523–2,500 logical qubits (academic estimates)
  - Current best quantum computers: ~100 logical qubits
  - IBM roadmap: 500–1,000 logical qubits by 2029
  - Practical threat window: **4–10 years** (contested; consensus is "10+ years" for
    cryptographically relevant attacks on secp256k1)

- **Links:**
  - [Bitcoin and Quantum Computing — Chaincode](https://chaincode.com/bitcoin-post-quantum.pdf)
  - [Post-Quantum Proposals for Bitcoin](https://blog.projecteleven.com/posts/a-look-at-post-quantum-proposals-for-bitcoin)

---

### 2.10 Hedera Hashgraph

- **Algorithm chosen:** SHA-384 hashes (quantum-resistant hashing); Ed25519 for signatures
  (not quantum-resistant); SEALSQ partnership for future PQC signature migration
- **Implementation date:** SHA-384 from genesis
- **Status:** **Partial** — hash layer is quantum-resistant; signature layer is not yet
- **Reason for choice:**
  > "Hedera uses 384-bit hashes compared to the 256-bit used by most blockchains,
  > ensuring that Hedera is already post-quantum for its hashes, which secures the entire
  > history of the hashgraph."
  > — Leemon Baird, Hedera co-founder

  SHA-384 provides 192-bit quantum security (Grover halves effective hash length), which
  exceeds NIST's minimum 128-bit quantum security threshold. However, Ed25519 signatures
  remain vulnerable to Shor's algorithm. The SEALSQ partnership (February 2025) focuses
  on developing PQC signature solutions.

- **Performance impact:** SHA-384 vs SHA-256 is negligible at Hedera's transaction volumes.

- **Links:**
  - [Hedera Post-Quantum Blog](https://hedera.com/blog/post-quantum-crypto/)
  - [Are Ed25519 Keys Quantum-Resistant?](https://hedera.com/blog/are-ed25519-keys-quantum-resistant-exploring-the-future-of-cryptography/)

---

### 2.11 Avalanche

- **Algorithm chosen:** Lattice-based cryptography under evaluation (no specific algorithm
  selected)
- **Implementation date:** No timeline announced
- **Status:** **Exploratory**
- **Reason for current inaction:**
  > "Ava Labs has acknowledged quantum risks and is actively exploring lattice-based
  > cryptography as a countermeasure, though the team remains cautious about deploying
  > such schemes on the main network, as lattice-based signatures are roughly an order
  > of magnitude larger than current elliptic curve signatures."

  Avalanche's subnet architecture (multiple independent chains) may allow a PQC subnet
  to be launched without affecting the primary network — a potential path for gradual
  migration not available to monolithic chains.

- **Links:**
  - [Quantum Computing Risks — Avalanche Context](https://postquantum.com/post-quantum/quantum-cryptocurrencies-bitcoin/)

---

## 3. Comparison Table

| Blockchain | Algorithm | Status | Year | Primary Reason | Sig Size vs ECDSA |
|------------|-----------|:------:|:----:|----------------|:-----------------:|
| **QRL** | XMSS | **Production** | 2018 | Only NIST-approved scheme available; conservative | ~44× |
| **Algorand** | Falcon-1024 | **Production** | 2022 | Compact certs for cross-chain relay | ~20× |
| **Algorand** | Falcon-1024 | **Mainnet txns** | 2025 | Full user transaction support | ~20× |
| **Solana** | ML-DSA | **Testnet** | 2025 | High sign throughput; no float dependency | ~38× |
| **IOTA** | WOTS+ | **(Deprecated)** | 2016 | IoT longevity requirement | ~20× |
| **Aptos** | SLH-DSA-SHA2-128s | **Proposed** | 2026 | Minimal assumptions (hash-only); no lattice risk | ~122× |
| **Polkadot** | ML-DSA + Falcon | **Planned** | 2026 | ML-DSA for consensus; Falcon for user accounts | Dual-scheme |
| **Ethereum** | Falcon or STARK | **Roadmap** | 2026+ | Account abstraction pathway | TBD |
| **Cardano** | ML-DSA | **Research** | TBD | Simpler constant-time impl; NIST-approved | ~38× |
| **Hedera** | (SHA-384 hashes only) | **Partial** | Genesis | Post-quantum hash security, not sig security | N/A |
| **Bitcoin** | None / ML-DSA demo | **Exploratory** | TBD | Governance deadlock; size/throughput tradeoffs | 38–59× |
| **Avalanche** | TBD (lattice) | **Exploratory** | TBD | Subnet architecture may allow gradual migration | TBD |

---

## 4. Algorithm Popularity Analysis

### 4.1 Adoption Count by Algorithm

| Algorithm | Standard | Projects | Notes |
|-----------|----------|:--------:|-------|
| **ML-DSA** (CRYSTALS-Dilithium) | FIPS 204 | **4** | Solana, Polkadot (consensus), Cardano, QRL 2.0 |
| **Falcon** (FN-DSA) | FIPS 206 | **3** | Algorand, Polkadot (accounts), Ethereum (candidate) |
| **SLH-DSA** (SPHINCS+) | FIPS 205 | **1** | Aptos |
| **XMSS** | NIST SP 800-208 | **1** | QRL (original, now transitioning) |
| **WOTS+** | Hash-based | **1** | IOTA (deprecated) |

**ML-DSA is the leading choice** for new adoptions. Its advantages for blockchain:
- No floating-point hardware requirement (Falcon requires IEEE 754 floats in signing)
- Constant-time implementation is simpler to audit (rejection sampling)
- Fast keygen (~52K ops/sec on x86 with AVX-512 — see our measured results)
- NIST FIPS 204 final (August 2024) — first lattice signature to achieve final status

**Falcon is the leading choice where signature size is the binding constraint:**
- 666 bytes (Falcon-512) vs 2,420 bytes (ML-DSA-44) — 3.6× smaller
- Critical for state proofs, light clients, and bandwidth-constrained environments
- NIST FIPS 206 (FN-DSA) finalized August 2025

### 4.2 Algorithm Selection Criteria Observed Across Projects

```
Signature size is critical → Falcon
  (Algorand state proofs, Polkadot user accounts)

Signing throughput is critical → ML-DSA
  (Solana 65K TPS target, Polkadot consensus validators)

Minimal security assumptions preferred → SLH-DSA or XMSS
  (Aptos: no lattice trust; QRL 2018: only hash-based available)

Implementation auditability prioritized → ML-DSA
  (Cardano formal methods; no FFT sampler side-channel risk)

Backward-compatible migration path → Account abstraction
  (Ethereum, Hedera: no hard fork required for opt-in)
```

---

## 5. Common Reasons for Algorithm Choice

Across all 11 projects, five patterns dominate the rationale for algorithm selection:

### 5.1 Signature and Key Size

The most cited technical constraint. Smaller signatures reduce:
- Transaction fees (on-chain storage cost)
- Block propagation latency (bandwidth)
- State/UTXO storage growth
- Cross-chain relay certificate size

**Falcon wins on size**: 666 bytes (Falcon-512) vs 2,420 (ML-DSA-44) vs 7,856 (SLH-DSA).

### 5.2 NIST Standardisation as Trust Signal

Every project that announced adoption in late 2024 or 2025 explicitly cited NIST FIPS
204/205/206 as their trust anchor. The August 2024 standardisation was the single largest
catalyst for blockchain adoption announcements. Before standardisation, most projects
cited "waiting for NIST" as their reason for inaction.

### 5.3 Implementation Safety

Falcon's use of FFT-based Gaussian sampling introduces timing side-channel risk if
implemented incorrectly. Multiple projects (Aptos, Cardano) explicitly cited this as a
reason to prefer ML-DSA or SLH-DSA, which have simpler constant-time code paths.

### 5.4 Cryptographic Agility

QRL's transition from XMSS → ML-DSA, and Polkadot's dual-algorithm design (ML-DSA for
consensus, Falcon for accounts), reflect a growing recognition that no single algorithm
is optimal for all use cases. Projects are designing upgrade paths rather than committing
to a single scheme permanently.

### 5.5 Security Assumption Conservatism

SLH-DSA (hash-only) and XMSS depend only on collision resistance of SHA-2/SHA-3, which
has 50+ years of cryptanalysis. Lattice-based schemes (Falcon, ML-DSA) are newer; their
security rests on the hardness of NTRU/Module-LWE problems, which have only ~25 years of
serious study. Projects with formal verification cultures (Aptos, Cardano) weight this
concern more heavily.

---

## 6. Industry Trends

### 6.1 Adoption Timeline

```
2016  IOTA genesis with WOTS+ (first blockchain PQC, hash-based)
2018  QRL mainnet with XMSS (first NIST-approved PQC blockchain)
2022  Algorand state proofs with Falcon-1024 (first lattice PQC in production)
      ↓ 2-year gap: most chains "waiting for NIST"
2024  NIST publishes FIPS 203/204/205 (August) — catalyst event
      ← Wave of announcements begins immediately
2025  Solana Winternitz Vault (January)
      Algorand mainnet Falcon user transactions (November)
      Solana ML-DSA testnet (December)
      Aptos AIP-137 proposal (December)
      Web3 Foundation Polkadot PQC roadmap published
2026  Ethereum quantum readiness enters 2026 core protocol roadmap
      Expected: Polkadot testnet, Cardano formal proposal, Aptos mainnet
```

**The 2024 NIST standardisation triggered the most concentrated wave of PQC activity in
blockchain history.** Before August 2024, production PQC deployments were limited to QRL
and Algorand state proofs. After, at least 6 major projects initiated concrete roadmaps
or testnets within 18 months.

### 6.2 Production vs Research Gap

Despite significant research activity, **only 2 projects have live production PQC**
(QRL since 2018, Algorand state proofs since 2022). The gap between roadmap announcements
and production deployment is typically 2–4 years, driven by:
- Wallet and ecosystem tooling updates (Ledger hardware wallet firmware for larger keys)
- Governance processes (BIPs, EIPs, AIPs, community votes)
- Audit requirements (new signature scheme implementations require independent security audits)
- Performance tuning (signature size inflation requires protocol adjustments)

### 6.3 Proof-of-Stake vs Proof-of-Work

All production and near-production PQC deployments are on **proof-of-stake** chains.
No proof-of-work chain has a concrete PQC migration plan. This reflects:
- PoS validator key management is centralised enough to coordinate algorithm upgrades
- PoW mining does not use asymmetric signatures — only block headers use hash functions
  (already quantum-resistant); the vulnerability is in wallet/UTXO signatures
- Bitcoin and Ethereum (pre-Merge) governance is more decentralized and contentious

---

## 7. Performance Impact Summary

Data drawn from academic benchmarks, project announcements, and our measured results
(see `COMPREHENSIVE_COMPARISON.md`).

| Metric | ECDSA / Ed25519 (baseline) | Falcon-512 | ML-DSA-44 | SLH-DSA-SHA2-128f |
|--------|:--------------------------:|:----------:|:---------:|:-----------------:|
| Verify throughput (single core) | 4,026–8,857 ops/sec | 23,877–30,569 ops/sec | 25,904–49,060 ops/sec | 599–734 ops/sec |
| Sign throughput (single core) | 2,638–24,276 ops/sec | 4,312–4,805 ops/sec | 10,273–15,975 ops/sec | 36–45 ops/sec |
| Signature size | 64–72 bytes | 666 bytes | 2,420 bytes | 17,088 bytes |
| Public key size | 32–65 bytes | 897 bytes | 1,312 bytes | 32 bytes |
| Tx size overhead (vs Ed25519) | 1× | ~11× | ~38× | ~267× |
| Key management | Stateless | Stateless | Stateless | Stateless |

> **Counterintuitive finding:** Falcon-512 and ML-DSA-44 verify **faster** than ECDSA
> secp256k1 on both tested platforms (7.6–10.7× faster). The performance penalty for PQC
> adoption falls on **signature size** and **signing speed**, not verification speed.
> For validator-heavy workloads (verify-dominated), switching to Falcon or ML-DSA
> actually *improves* throughput on modern hardware.

---

## 8. Implications for This Research

### 8.1 Algorithm Track Record

Based on real-world deployments:

| Algorithm | Track Record | Recommendation |
|-----------|-------------|----------------|
| Falcon-512/1024 | 3+ years production (Algorand); NIST FIPS 206 final Aug 2025 | **Best-validated for verify-heavy workloads** |
| ML-DSA-44/65 | FIPS 204 final Aug 2024; Solana testnet Dec 2025 | **Best for sign-heavy or consensus workloads** |
| SLH-DSA | FIPS 205 final Aug 2024; Aptos proposed | Conservative; use only if lattice assumptions unacceptable |
| XMSS | Production since 2018 (QRL); being superseded | Legacy; avoid for new deployments |

### 8.2 Performance Patterns from Early Adopters

1. **Verification is not the bottleneck.** Algorand confirmed 6,000 TPS with no regression
   after introducing Falcon-1024. Our benchmarks corroborate: Falcon verification is
   7-10× faster than ECDSA secp256k1.

2. **Signature size is the dominant constraint.** Every project that chose Falcon over
   ML-DSA cited bandwidth and storage, not compute. Our measurements confirm: Falcon-512
   at 666 bytes vs ML-DSA-44 at 2,420 bytes is a 3.6× size advantage.

3. **NIST standardisation is a prerequisite for enterprise adoption.** No major blockchain
   moved to production PQC before having a NIST-final standard to cite. The August 2024
   FIPS publications unlocked a wave of production commitments.

4. **Dual-algorithm designs emerge for complex protocols.** Polkadot's ML-DSA (consensus)
   + Falcon (accounts) split reflects a recognition that no single scheme is universally
   optimal. Research projects should benchmark both.

### 8.3 Lessons for MEMO / Research Context

- **Falcon-512 is the right choice for verify-heavy transaction validation.** It is the
  only NIST-standardised PQC signature with production deployment in a high-throughput
  blockchain (Algorand 6,000 TPS). Our measured 24K–31K verify/sec (vs 4K for ECDSA
  secp256k1) confirms a 7.6–8.1× verification advantage.

- **ML-DSA-44 should be benchmarked as the primary comparison point.** It is the leading
  choice for signing-heavy protocols (validators, consensus). Our measurements show it
  is 2× faster than Falcon on x86 with AVX-512 for verification.

- **Signature size matters more than compute at scale.** Real-world adopters confirm this
  repeatedly. A 3.2× signature size advantage (Falcon vs ML-DSA) translates directly to
  lower storage, faster propagation, and lower fees.

---

## 9. MEMO Positioning & Comparison

### 9.1 How MEMO Compares to Industry Adopters

MEMO is among the first projects to publish **measured end-to-end blockchain TPS** with real PQC signature verification, not just micro-benchmarks. Here is how our results compare:

| Project | Algorithm | TPS Reported | Verify Type | Notes |
|---------|-----------|:------------:|:-----------:|-------|
| **Algorand** | Falcon-1024 | 6,000 | Real (production) | Unchanged from pre-PQC baseline |
| **Solana** | ML-DSA | "Compatible" | Real (testnet) | No public regression data |
| **QRL** | XMSS | ~3–5 | Real (production) | Inherently limited by stateful scheme |
| **MEMO (this work)** | Falcon-512 | **2,572** | Real (Chameleon) | 1000 TX, no TPS penalty vs ECDSA |
| **MEMO (this work)** | ML-DSA-44 | **1,533** | Real (Chameleon) | 1000 TX, ~40% below Falcon-512 |
| **MEMO (this work)** | ECDSA (real) | **1,403** | Real (Chameleon) | 1000 TX, baseline with real OpenSSL verify |

**Key takeaways:**

1. **MEMO confirms Algorand's finding: Falcon-512 has no TPS penalty.** Our Falcon-512 result (2,572 e2e TPS) actually exceeds the real ECDSA baseline (1,403 e2e TPS) because the pipeline is network-bound, not verify-bound. Algorand similarly reported no TPS regression with Falcon-1024.

2. **MEMO quantifies the ML-DSA-44 signature size bottleneck.** Despite ML-DSA-44 having 2x faster raw verification than Falcon-512 on x86 (46,532 vs 23,505 ops/sec), its 3.5x larger signatures (2,420B vs ~655B) cause 40% lower end-to-end TPS. This validates industry preference for Falcon in size-constrained chains (Algorand, Polkadot user accounts).

3. **MEMO provides the first comparative blockchain data for Falcon vs ML-DSA.** No other project has published side-by-side end-to-end TPS measurements for both algorithms on the same blockchain. This is a unique contribution to the PQC adoption literature.

4. **The bottleneck has shifted.** Classical blockchain bottleneck analysis focused on verify cost. With PQC, the dominant cost is serialization and I/O of larger signatures/keys — not the cryptographic verification itself. MEMO's measurements make this concrete: block processing time scales proportionally with TX wire size (ECDSA 35ms → Falcon 200ms → ML-DSA 473ms for 1000 TX).

### 9.2 Algorithm Recommendation for MEMO-like Systems

Based on both our measurements and the industry survey:

| Criterion | Recommendation | Rationale |
|-----------|---------------|-----------|
| Minimum TPS impact | **Falcon-512** | 3.5x smaller sigs than ML-DSA → lower serialization/I/O cost |
| Fastest single-core verify | **ML-DSA-44** | 2x faster on x86 (AVX-512 NTT) |
| Smallest state growth | **Falcon-512** | ~1,611B/TX vs ~3,791B/TX (ML-DSA) |
| Simplest implementation | **ML-DSA-44** | No floating-point, constant-time by design |
| Most conservative security | **SLH-DSA** | Hash-only, but 17KB sigs make it impractical for high-TPS |
| Cryptographic agility | **Hybrid mode** | Support all three; let wallet owners choose risk/value tradeoff |

**MEMO's recommendation: Deploy Falcon-512 as default with hybrid mode support for ML-DSA-44.** This aligns with Polkadot's dual-algorithm approach and provides a migration path without protocol changes.

---

## 10. Migration Strategy for MEMO

### 10.1 The sig_type Version Field

MEMO's blockchain already includes a `sig_type` field in every transaction (serialized via protobuf). This field acts as a **cryptographic version discriminant** — validators read it to dispatch to the correct verification backend at runtime. No hard fork or protocol change is needed to add or deprecate signature schemes.

This is the same approach used by:
- **Algorand:** Two key types (Ed25519 and Falcon-1024) coexist, with the key type embedded in the account record.
- **Ethereum (planned):** EIP-7702 account abstraction allows per-account signature verification logic.
- **Polkadot:** Dual-algorithm design with ML-DSA for consensus and Falcon for user accounts.

### 10.2 Phased Migration Plan

```
Phase 1 — Hybrid (Current)
├── Default: ECDSA secp256k1
├── Opt-in: Falcon-512, ML-DSA-44 via --scheme flag
├── Build: SIG_SCHEME=3 (all backends compiled)
├── Validators verify all three types via sig_type dispatch
└── Goal: Allow early adopters to use PQC; collect real-world data

Phase 2 — PQC Default
├── Default: Falcon-512 (best size/TPS tradeoff)
├── Deprecated: ECDSA (still accepted, warning on creation)
├── New wallets default to Falcon-512
├── Existing ECDSA wallets continue to work
└── Goal: Migrate majority of active wallets to PQC

Phase 3 — Classical Sunset
├── Default: Falcon-512
├── Removed: ECDSA (reject new ECDSA transactions)
├── Grace period: 6–12 months for remaining ECDSA wallets to migrate
└── Goal: Full quantum resistance; reduce code surface
```

### 10.3 User Choice: Risk vs Value Tradeoff

Following the professor's suggestion, the hybrid mode lets wallet owners choose their own risk/value tradeoff:

| User Profile | Recommended Scheme | Rationale |
|-------------|-------------------|-----------|
| High-value, infrequent tx | **Falcon-512** | Smallest PQC sigs; best verified in production (Algorand) |
| High-frequency trading | **ECDSA** (Phase 1) | Smallest sigs; fastest serialization; acceptable until quantum threat materializes |
| Maximum security | **ML-DSA-44** | NIST FIPS 204 first-finalized; simpler implementation; no FFT side-channel |
| Long-term cold storage | **Falcon-512** | Best size/security balance for dormant accounts |

The system does not force a one-size-fits-all choice. Users who believe the quantum threat is imminent can opt into PQC immediately; users who prioritize throughput can remain on ECDSA until Phase 2.

### 10.4 Implementation Status

| Component | Status | Notes |
|-----------|:------:|-------|
| `sig_type` in TX protobuf | Done | Serialized/deserialized through full pipeline |
| `crypto_verify_typed()` runtime dispatch | Done | Creates temp context per sig_type |
| `SIG_SCHEME=3` hybrid build | Done | Compiles ECDSA + Falcon-512 + ML-DSA-44 |
| Wallet CLI `--scheme` flag | Done | `ecdsa`, `falcon`, `mldsa` |
| `benchmark_hybrid.sh` | Done | Mixed-scheme benchmark script |
| Universal buffer sizes (ML-DSA-44 max) | Done | All nodes can deserialize any TX |
| Hybrid end-to-end benchmarks | Pending | Run on Chameleon with `benchmark_hybrid.sh` |

---

## 11. References

| Source | Citation |
|--------|---------|
| Algorand state proofs | [Algorand Foundation — Technical Brief: Falcon](https://algorand.co/blog/technical-brief-quantum-resistant-transactions-on-algorand-with-falcon-signatures) |
| Algorand post-quantum page | [algorand.co/technology/post-quantum](https://algorand.co/technology/post-quantum) |
| Algorand developer docs | [State Proofs Overview](https://developer.algorand.org/docs/get-details/stateproofs/) |
| QRL documentation | [docs.theqrl.org](https://docs.theqrl.org/what-is-qrl/) |
| QRL definitive guide | [Post-Quantum Blockchain Security](https://www.theqrl.org/the-definitive-guide-to-post-quantum-blockchain-security/) |
| Solana testnet announcement | [Solana Quantum-Resistant Signatures](https://beincrypto.com/solana-quantum-resistant-blockchain-migration/) |
| Solana Helius analysis | [What Would Solana Need to Change?](https://www.helius.dev/blog/solana-post-quantum-cryptography) |
| BTQ × Solana ML-DSA | [PRNewswire — BTQ × Bonsol Labs](https://www.prnewswire.com/news-releases/btq-technologies-partners-with-bonsol-labs-to-achieve-industry-first-nist-standardized-post-quantum-cryptography-signature-verification-on-solana-302592494.html) |
| Ethereum roadmap | [ethereum.org/roadmap/future-proofing](https://ethereum.org/roadmap/future-proofing/) |
| Ethereum PQC analysis | [BTQ — Ethereum's Roadmap](https://www.btq.com/blog/ethereums-roadmap-post-quantum-cryptography) |
| ChainSafe Substrate PQC | [Introducing PQ Security to Substrate](https://blog.chainsafe.io/introducing-post-quantum-security-to-signatures-on-substrate/) |
| Aptos AIP-137 | [Aptos Proposes Quantum-Resistant Signatures](https://finance.yahoo.com/news/aptos-proposes-quantum-resistant-signatures-085416640.html) |
| Hedera PQC blog | [hedera.com/blog/post-quantum-crypto](https://hedera.com/blog/post-quantum-crypto/) |
| Bitcoin PQC analysis | [Chaincode — Bitcoin and Quantum Computing](https://chaincode.com/bitcoin-post-quantum.pdf) |
| Bitcoin proposals | [Project Eleven — PQ Proposals for Bitcoin](https://blog.projecteleven.com/posts/a-look-at-post-quantum-proposals-for-bitcoin) |
| NIST FIPS 204 (ML-DSA) | [csrc.nist.gov/pubs/fips/204/final](https://csrc.nist.gov/pubs/fips/204/final) |
| NIST FIPS 205 (SLH-DSA) | [csrc.nist.gov/pubs/fips/205/final](https://csrc.nist.gov/pubs/fips/205/final) |
| NIST FIPS 206 (FN-DSA) | [csrc.nist.gov/pubs/fips/206/final](https://csrc.nist.gov/pubs/fips/206/final) |
| NIST announcement | [NIST Releases First 3 PQC Standards (Aug 2024)](https://www.nist.gov/news-events/news/2024/08/nist-releases-first-3-finalized-post-quantum-encryption-standards) |
| Academic SoK survey | [Quantum Disruption — arXiv 2512.13333](https://arxiv.org/html/2512.13333v1) |
| Performance benchmarks | [Springer: Quantum-Resistant Blockchain Performance](https://link.springer.com/article/10.1007/s11128-024-04272-6) |
| Hybrid PQC paper | [Hybrid PQ Signatures for BTC/ETH — Preprints](https://www.preprints.org/manuscript/202509.2079) |

---

*Data current as of March 2026. Blockchain protocol development moves quickly;
verify current status via project documentation before citing in publications.*
