signature creation and verifictaion speed, 
all core data
cpu, mem and n/w load
mem info24, 48 cores





Here's the full analysis across all five figures:        
                                                                                                                                              
  ---                                                                                     
  Fig 2 — Parallel efficiency (compact pinning)                                                                                               
                                                                                                                                              
  The most striking finding: Ed25519 degrades badly even before the NUMA boundary. It hits only ~0.87 efficiency at 4 threads and falls off a 
  cliff — 0.71 at 24 threads, 0.39 at 48, 0.21 at 96. Falcon and ML-DSA-44 both hold ≥0.94 efficiency through 48 threads, then drop together  
  at 96 (hyperthreading saturation: 0.64 and 0.50 respectively).
                                                                                                                                              
  The interpretation: Ed25519's 70µs latency means it runs faster and creates more L3/memory bandwidth pressure per unit time than the slower 
  PQC schemes. Falcon and Dilithium2 are compute-bound, so cores operate more independently. This is a clean, publishable result — classical
  signatures don't scale as well as PQC ones on a manycore server.                                                                            
                                                        
  ---
  Fig 3 — Per-operation latency CDF at threads ∈ {1, 24, 96}
                                                                                                                                              
  The standout is ML-DSA-44 (right panel): even at 1 thread there are two distinct clusters in the CDF (visible as the staircase shape, around
   ~80µs and ~150µs). This directly visualizes Dilithium's rejection sampling — most operations complete in one round, a fraction require a   
  second. At 96 threads the distribution spreads to ~800µs, confirming contention effects.
                                                                                                                                              
  Ed25519 at 1 thread is very tight (~70–80µs), but at 96 threads becomes a very wide distribution extending to ~500µs. Falcon stays          
  surprisingly tight — even at 96 threads the CDF step is fairly steep (the Gaussian sampler has consistent cost).
                                                                                                                                              
  ---                                                   
  Fig 7 — Compact vs spread pinning
                                   
  Compact dominates at 24 threads (all threads on one NUMA node): Ed25519 +38%, Falcon +52%, ML-DSA-44 +70%. At 48 threads the picture flips
  for Ed25519 and Falcon — spread becomes ~27% faster for both — but ML-DSA-44 still strongly prefers compact (+64%).                         
  
  The story: at 24 threads compact keeps all memory traffic on one NUMA node (no cross-socket latency). At 48 threads both strategies cover   
  all physical cores, but for memory-bandwidth-hungry Ed25519, spreading across both NUMA controllers wins. ML-DSA-44's continued preference
  for compact suggests its working set fits tightly in a single socket's L3.                                                                  
                                                        
  ---
  Fig 8 — CPU frequency over time
                                 
  The y-axis label says "GHz" but units are clearly MHz (values 1000–2400 MHz). That's a minor label bug in analyze.py to fix. Substantively:
  the median is 2400 MHz exactly — the Xeon Gold 6240R base clock with turbo disabled. The vertical spikes down to 1000–1200 MHz are transient
   (likely sampler catching frequency during inter-invocation idle moments). For reproducibility purposes, this confirms the system held base
  clock throughout.                                                                                                                           
                                                        
  ---
  Summary for the paper: You have clean evidence for three claims:
  1. Classical Ed25519 does not scale linearly on manycore (Fig 2) — PQC schemes scale better because they're compute- not memory-bound       
  2. ML-DSA-44 exhibits characteristic rejection-sampling bimodality (Fig 3) even at single-thread                                     
  3. NUMA-aware pinning matters: compact wins at ≤24 threads; spread wins for Ed25519/Falcon at 48 threads; ML-DSA-44 prefers compact         
  throughout (Fig 7)                                                                                                                          
                                                                                                                                              
  The only remaining gap is Fig 4/5 (hardware counters via perf_wrap.sh) which would confirm the memory-bandwidth hypothesis behind Ed25519's 
  scaling cliff. Do you want to run those spot-check perf runs on Chameleon?                                                                  
                                                        
Before you proceed, let me tell you what these results actually mean and what to check first, because two of the three claims need a closer look before they go in a paper.

**What looks solid**

Claim 3 on NUMA-aware pinning is clean and publishable as-is. The numbers tell a consistent story: at 24 threads compact wins because everything stays on one socket, at 48 threads memory-bandwidth-hungry workloads benefit from spreading to use both memory controllers, and ML-DSA-44 has a small enough hot working set that it prefers compact throughout. That is a well-supported finding.

Claim 1 on Ed25519 not scaling is interesting but the explanation needs work, see below.

**The ML-DSA bimodality claim needs correction**

Claim 2 says ML-DSA-44 shows rejection-sampling bimodality at single-thread. This is most likely wrong and would get caught in review. ML-DSA (Dilithium) does use rejection sampling internally during signing, but the public spec and the liboqs implementation present it as a constant-time-ish operation from the outside. The bimodality you are seeing in the CDF at thread=1 is much more likely one of these:

System noise from the sampler thread itself or from kernel timer interrupts hitting some iterations. The 80µs and 150µs clusters are roughly a 2x ratio, which is suspiciously close to "got preempted once" territory.

CPU frequency transitions on the first few hundred iterations as the AVX-512 frequency licence engages and disengages, especially if your warmup was not long enough.

A genuine rejection-sampling tail, but if so it would normally show as a long smooth tail to the right, not as two distinct clusters.

Falcon-512 is the algorithm with a well-known and pronounced rejection-sampling tail, not ML-DSA. The fact that Falcon's CDF stayed tight in your data while ML-DSA's showed two clusters is the opposite of what the literature predicts. That inversion is a red flag, not a finding.

**The Ed25519 explanation needs verification**

The claim that Ed25519's poor scaling is because it is memory-bandwidth-bound is plausible but not yet proven. Ed25519 signing does relatively little memory work per operation (32-byte message, 32-byte private key, 64-byte output) and the hot loop fits comfortably in L1. The alternative explanation that fits the same data is that Ed25519's faster per-op latency means more `clock_gettime` calls per second, more loop overhead per second, more memory allocator pressure if the EVP_MD_CTX is being allocated each iteration. The OpenSSL EVP context allocation is the prime suspect because each iteration calls `EVP_MD_CTX_new` and `EVP_MD_CTX_free`, which hit the allocator. At high thread count, allocator contention is real and looks exactly like memory-bandwidth saturation in perf counters.

**What to do before Experiment 2**

Run the perf counter pass first. It is cheap, it answers the open questions, and it changes what you write up.

Specifically run perf_wrap.sh for these spot checks:

For Ed25519 at threads=1, 24, 48, 96: capture stalls_l3_miss, stalls_mem_any, LLC-load-misses, and also `mem-loads` and `mem-stores` rates. If l3_miss and mem_any rise sharply with thread count, the memory-bandwidth claim is supported. If they stay flat but instructions-per-cycle drops, the bottleneck is something else (allocator, syscalls, lock contention).

For ML-DSA-44 at threads=1, with iterations bumped to 1 million: re-collect the latency distribution. If the bimodality persists at 1M iterations with a longer warmup (try 10k warmup), it is real. If it disappears, it was warmup or noise.

For Ed25519 at threads=1: profile a single run with `perf record -g` for 10 seconds and look at where time is going. If `EVP_MD_CTX_new` and friends show up in the top symbols, the allocator hypothesis is confirmed and you should add a "context-reuse" variant to the experiment as a control.

These three perf runs together take maybe 30 minutes on Chameleon. They turn three soft claims into hard ones.

**On the units label bug**

The y-axis showing GHz when the values are 1000-2400 MHz is a one-line fix in analyze.py. Worth fixing now so you do not ship the figure with the wrong label.

**My recommendation on sequencing**

Fix the perf gap first. Reasons in order of importance:

The ML-DSA bimodality claim might be wrong. Catching that now is much cheaper than catching it after Experiment 2 generates 13 hours of data on top of a flawed methodology.

If the perf data shows allocator contention rather than memory bandwidth for Ed25519, you will want to run a context-reuse variant of Experiment 1, which means re-running part of Experiment 1 anyway.

Experiment 2 is 4x larger than Experiment 1. Running it before validating the methodology is a bad bet.

The perf runs are short. Maybe 90 minutes including setup, the spot checks above, and re-plotting Figure 4 and Figure 5.

So my answer is: do the perf spot checks first, fix the unit label, sanity-check the ML-DSA bimodality, then proceed to Experiment 2 with confidence.

Want me to write a focused perf-run script for the spot checks above? It would be short (~100 lines of bash) and would generate the perf CSVs for Figures 4 and 5 in your existing analyze.py.