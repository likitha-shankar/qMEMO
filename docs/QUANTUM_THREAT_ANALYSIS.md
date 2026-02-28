# Quantum Threat Analysis for Blockchain

**qMEMO Project -- Illinois Institute of Technology, Chicago**

This document identifies which blockchain components are vulnerable to attacks by large-scale quantum computers and summarizes how MEMO addresses these threats with post-quantum cryptography.

---

## 1. Introduction

Large-scale, fault-tolerant **quantum computers** would be able to run algorithms that break or weaken much of the cryptography used in today's blockchains and PKI. Although such machines do not exist yet, standardization bodies (e.g. NIST) and industry assume a **threat timeline on the order of 5-15 years** for "cryptographically relevant" quantum computers. Migration to **post-quantum cryptography (PQC)** is therefore a medium-term priority.

### Why this matters for blockchain

- **Long-lived secrets:** Addresses and keys may be used or reused for years; data signed today could be attacked once quantum computers are available ("harvest now, decrypt later").
- **Immutability:** On-chain history cannot be patched; if a signature scheme is broken, past transactions could be forged or keys derived from public data.
- **Consensus and identity:** Validator and transaction signatures must remain secure for the lifetime of the chain.

This analysis maps **quantum algorithms** to **blockchain components**, rates **risk levels**, and describes **MEMO's mitigations** (Falcon-512, BLAKE3-512, and related design choices).

---

## 2. Cryptographic Vulnerabilities

### 2.1 Shor's Algorithm -- Breaks Public-Key Crypto

**Shor's algorithm** solves the discrete logarithm and integer factorization problems in polynomial time on a sufficiently large quantum computer. That directly breaks:

| System | Problem Shor exploits | Effect |
|--------|------------------------|--------|
| **ECDSA** (e.g. secp256k1) | Discrete log on elliptic curve | Private key recovered from public key |
| **RSA** | Factorization of modulus | Private key recovered from public key |
| **Diffie-Hellman / DSA** | Discrete log | Key agreement and signatures broken |

**Qubit requirements (approximate):**

| Target | Logical qubits (circuit) | Physical qubits (with error correction) | Notes |
|--------|---------------------------|------------------------------------------|--------|
| **256-bit ECDSA** (Bitcoin/Ethereum style) | ~1,500-2,000 | Millions (surface-code style) | ECC is "first to fall" per bit of security |
| **RSA-2048** | ~1,700-4,100 | ~20 million (noisy) | Recent estimates: factor in ~8 hours with 20M qubits |

Once an attacker has enough fault-tolerant qubits to run Shor's algorithm against 256-bit curves, they can **recover the private key from any exposed public key**. Because blockchain transactions reveal the public key (or it can be derived from the address after the first spend), **all ECDSA-based transaction and validator signatures are at risk**.

---

### 2.2 Grover's Algorithm -- Weakens Symmetric Crypto and Hashing

**Grover's algorithm** gives a quadratic speedup for unstructured search. For cryptography:

- **Symmetric keys (AES) and hash preimages:** Security is effectively **halved** (e.g. 256-bit → 128-bit equivalent). So 256-bit keys and 256-bit hash outputs are no longer "256-bit secure" against a capable quantum attacker.
- **Impact:** Preimage and collision resistance of 256-bit hashes (e.g. SHA-256) are reduced; NIST and PQC guidance recommend **doubling output size** (e.g. 512-bit hashes) for post-quantum security.

**Qubit / cost context (approximate):**

| Target | Effective security after Grover | Rough quantum cost | Recommendation |
|--------|---------------------------------|--------------------|----------------|
| **SHA-256** preimage | ~128-bit | ~2^128 queries; circuit depth ~2^154 (logical-qubit-cycles) | Use **SHA-512** or **BLAKE3-512** |
| **AES-256** key search | ~128-bit | Similar square-root speedup | Acceptable for many use cases; increase key size if needed |

So the threat is **moderate**: hashes and symmetric crypto are not broken in the same way as ECDSA/RSA, but security margins drop. Blockchain designs that rely on 256-bit hashes (Merkle trees, block hashes, address derivation) should plan for **512-bit hashes** in a post-quantum setting.

---

## 3. Blockchain Component Analysis

The following table and sections rate each component's exposure to the quantum threats above.

| Component | Vulnerability | Risk | Reason |
|-----------|----------------|------|--------|
| Transaction signatures | Shor (ECDSA) | **CRITICAL ❌** | Private key recoverable from public key |
| Validator signatures | Shor (if ECDSA) | **CRITICAL ❌** | Same as above |
| Hash functions | Grover | **MODERATE ⚠️** | Security halved; need 2x output (e.g. 512-bit) |
| Merkle trees | Grover (hashing) | **MODERATE ⚠️** | Built from hashes; use 512-bit hashes |
| Addresses | Shor (once key exposed) | **LOW ⚠️** | Safe until public key is revealed (e.g. first spend) |
| Proof-of-Space | Hash-based | **SAFE ✅** | No discrete log / factorization |
| Network protocol | -- | **SAFE ✅** | No long-term public-key crypto in scope |

---

### 3.1 Transaction Signatures: CRITICAL ❌

- **Current (e.g. Bitcoin, Ethereum):** ECDSA (secp256k1 or similar). The signer's **public key** is exposed when they spend (in the transaction or recoverable from signature + message).
- **Quantum threat:** Shor's algorithm on a large enough quantum computer recovers the **private key** from that public key. An attacker could then forge any future transaction from that address and drain funds.
- **Qubit scale:** Breaking 256-bit ECDSA is estimated at **~1,500-2,000 logical qubits** (with millions of physical qubits for fault tolerance). This is often cited as achievable before breaking RSA-2048 at scale.

**Conclusion:** ECDSA-based transaction signatures are **critically vulnerable** and must be replaced by **post-quantum signatures** (e.g. Falcon-512 in MEMO).

---

### 3.2 Validator Signatures: CRITICAL ❌

- **Role:** Validators sign blocks (or block headers) to attest authorship and integrity. If these signatures use ECDSA (or any Shor-vulnerable scheme), the same threat applies.
- **Quantum threat:** Once the validator's public key is known, a quantum attacker can recover the private key and **forge blocks** or impersonate the validator.
- **Conclusion:** Validator signing must also use **post-quantum signatures** (e.g. Falcon-512).

---

### 3.3 Hash Functions: MODERATE ⚠️

- **Uses in blockchain:** Block hashes, transaction IDs, Merkle roots, address derivation, commitment schemes.
- **Quantum threat:** Grover's algorithm reduces effective security by a factor of two. A 256-bit hash provides only **~128 bits of post-quantum security** for preimage resistance.
- **Conclusion:** Move to **512-bit hashes** (e.g. SHA-512, BLAKE3-512) so that post-quantum security remains ~256-bit equivalent. MEMO uses **BLAKE3-512** for this reason.

---

### 3.4 Merkle Trees: MODERATE ⚠️

- **Role:** Merkle trees summarize many transactions (or state) into a single root hash stored in the block header. Integrity of the tree depends on the underlying hash.
- **Quantum threat:** Same as hashing--Grover weakens the hash. Collision or second-preimage attacks on the hash could allow forging tree nodes or proofs.
- **Conclusion:** Use **double-length hashes** (e.g. 512-bit) in Merkle constructions so the tree remains secure against quantum-assisted attacks. MEMO's design uses 512-bit hashing for Merkle and related structures.

---

### 3.5 Addresses: LOW ⚠️

- **Observation:** An address is often a hash of the public key (or derived from it). Until the public key is revealed (e.g. when the owner first spends), Shor's algorithm cannot be applied--there is no public key on the chain yet.
- **Risk:** As soon as the public key is exposed in a transaction, that key becomes a target for a future quantum attack. So "reuse" addresses are at higher long-term risk than "one-time" addresses.
- **Conclusion:** **Low** immediate risk for unspent outputs whose public key is not yet revealed; **critical** once the key is on-chain unless the chain uses PQC signatures (e.g. Falcon-512), in which case the signature scheme itself is safe.

---

### 3.6 Proof-of-Space: SAFE ✅

- **Role:** In MEMO, consensus is based on **Proof-of-Space**: validators prove allocation of disk space. These proofs are typically **hash-based** (e.g. computing many hashes over plots).
- **Quantum threat:** No reliance on discrete log or factorization. Grover only reduces hash security by half; with **512-bit hashes** in the proof construction, Proof-of-Space remains secure.
- **Conclusion:** Proof-of-Space is **safe** in a post-quantum context when built on strong, double-length hashing.

---

### 3.7 Network Protocol: SAFE ✅

- **Role:** P2P messaging, gossip, block and transaction propagation. May use TLS (which has its own PQC migration path) but no long-term blockchain-specific public keys in the protocol itself.
- **Quantum threat:** No direct use of ECDSA/RSA in the blockchain logic; protocol security is about availability and integrity of messages, not breaking keys that protect past blocks.
- **Conclusion:** **Safe** from the perspective of the blockchain crypto analyzed here; TLS and operational keys should follow standard PQC migration.

---

## 4. MEMO's Solution

MEMO is designed for **quantum resistance** from the start. The following mappings address the vulnerabilities above.

| Component | Classical (at-risk) | MEMO (post-quantum) | Status |
|-----------|----------------------|----------------------|--------|
| **Signatures** | ECDSA (secp256k1, etc.) | **Falcon-512** | ✅ PQC signatures |
| **Hashing** | SHA-256 (128-bit PQ security) | **BLAKE3-512** | ✅ 256-bit PQ security |
| **Merkle / structures** | 256-bit hash outputs | **Double-length (512-bit)** | ✅ Stronger integrity |

- **Transaction and validator signatures:** Falcon-512 (NIST PQC standard) replaces ECDSA. No known quantum algorithm breaks lattice-based signatures like Falcon in the same way Shor breaks ECDSA.
- **Hashing and Merkle:** BLAKE3-512 and 512-bit outputs ensure that Grover's algorithm does not reduce security below the desired level (e.g. 256-bit equivalent).

These choices are why qMEMO focuses on **Falcon-512 verification performance**: in MEMO, every transaction and block signature is Falcon-512, so verification throughput directly determines chain capacity.

---

## 5. Attack Scenarios

### Scenario 1: Attacker Steals Private Key via Quantum Computer

- **Setup:** A victim has spent from an address, so their **public key** is on-chain (or recoverable). The chain uses **ECDSA**.
- **Attack:** Attacker runs **Shor's algorithm** on a large quantum computer (~1,500+ logical qubits, millions physical) to recover the **private key** from the public key.
- **Outcome:** Attacker signs transactions as the victim and drains funds. **Permanent** compromise for that key.
- **MEMO mitigation:** Signatures are **Falcon-512**. There is no known polynomial-time quantum algorithm to recover the secret key from a Falcon public key. So this scenario does **not** apply to MEMO's signature layer.

---

### Scenario 2: Attacker Breaks Signature During Broadcast Window

- **Setup:** Victim broadcasts a transaction; for a short window the transaction is in the mempool but not yet in a block. Attacker has a quantum computer.
- **Attack (ECDSA chain):** If the attacker can derive the private key **before** the TX is confirmed, they could try to replace it with a higher-fee or conflicting TX. With Shor, key recovery is feasible once the public key is known (e.g. from the TX).
- **Outcome:** Double-spend or front-running using recovered key. Time window is small but possible in a quantum era.
- **MEMO mitigation:** With **Falcon-512**, the attacker cannot recover the signing key from the broadcast transaction. They cannot forge a replacement transaction. The "break during broadcast" vector is closed at the signature level.

---

### Scenario 3: Attacker Pre-computes Attack on Address (Harvest Now, Decrypt Later)

- **Setup:** Attacker **records** all public keys ever exposed on the chain (e.g. from past transactions). They do not have a large quantum computer today but expect to in 10-15 years.
- **Attack (ECDSA chain):** When a large quantum computer is available, run Shor on all stored public keys and recover private keys. Then spend from those addresses.
- **Outcome:** Mass theft from any address that ever revealed its ECDSA public key. Immutable history means keys cannot be "rotated" after the fact.
- **MEMO mitigation:** Only **Falcon public keys** are stored on-chain. There is no known quantum algorithm to derive Falcon secret keys from those public keys. So "harvest now, decrypt later" does not apply to MEMO's transaction or validator signatures.

---

## 6. Conclusion

- **Critical risks** come from **Shor's algorithm** breaking **ECDSA** (and RSA): transaction and validator **signatures** are exposed once quantum computers reach roughly **~1,500+ logical qubits** (millions of physical qubits with error correction). MEMO removes this by using **Falcon-512** for all such signatures.
- **Moderate risks** come from **Grover's algorithm** weakening **256-bit hashes** and thus Merkle trees and other hash-based structures. MEMO addresses this with **BLAKE3-512** and **double-length (512-bit)** hashing.
- **Proof-of-Space** (hash-based) and **network protocol** (no long-term ECDSA/RSA in chain logic) are **safe** in this model; addresses are **low risk** until the public key is revealed, and in MEMO revealed keys are Falcon keys, not ECDSA.

MEMO's use of **Falcon-512** and **BLAKE3-512** is a direct response to these threats and places the chain in a **post-quantum secure** posture for signatures and hashing, independent of the exact timeline (5-15 years) for large-scale quantum computers.

---

## References

- NIST Post-Quantum Cryptography Standardization (Falcon, ML-DSA, etc.)
- Kudelski Security: "Quantum Attack Resource Estimate: Using Shor's Algorithm to Break RSA vs DH/DSA vs ECC"
- Gouzien et al.: "How to factor 2048 bit RSA integers in 8 hours using 20 million noisy qubits" (Quantum 2021)
- NIST / ePrint: "Estimating the cost of generic quantum pre-image attacks on SHA-2 and SHA-3"; "On practical cost of Grover"
