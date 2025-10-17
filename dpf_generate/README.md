# Additive DPF with Correction Words (CS670)

This repository contains a single-file reference implementation of a **2‑party Distributed Point Function (DPF)** over (\mathbb{Z}_{2^{64}}) that uses **correction words** in the GGM tree and a **leaf seed correction** to program the non‑zero output at a chosen index.

> **File:** `gen_dpf.cpp` (C++20)

---

## TL;DR

* Build: `g++ -std=c++20 -O2 gen_dpf.cpp -o gen_dpf`
* Run: `./gen_dpf <DPF_size> <num_DPFs>`
* The program prints keys for two parties and self‑tests that the reconstructed vector is 0 everywhere except at index **α**, where it equals **β** $(mod (2^{64}))$.

---

## Features

* 2‑party **additive DPF** over $(\mathbb{Z}_{2^{64}})$.
* GGM expansion with per‑level **correction words**: `(dSL,dTL,dSR,dTR)`.
* **Leaf seed correction**: a single `cwOut` XORed into the seed of the unique party whose leaf control bit is 1 at **α**.
* Deterministic, compact test harness (`EvalFull`) that verifies the DPF reconstruction across the entire domain.
* Domain size **not** required to be a power of two (the tree depth is `nbits = ceil(log2(size))`; evaluation is only over `[0, size-1]`).

> ⚠️ **Security note**: This coursework version uses SplitMix64 as the PRG/mixer and a trivial extractor `v_from_seed(s)=s`; this is **not crypto‑secure** and is intended purely for the assignment. For a production system, use a PRF/PRG such as AES‑CTR or ChaCha20 (with domain separation), and a proper extractor.

---

## Build Instructions

```bash
# Using GCC
g++ -std=c++20 -O2 gen_dpf.cpp -o gen_dpf
```

## Usage

```bash
./gen_dpf <DPF_size> <num_DPFs>
```

**Arguments**

* `DPF_size` — size of the domain (positive integer).
* `num_DPFs` — how many random (α, β) instances to generate, print, and test.

**Example**

```bash
./gen_dpf 1024 3 > out.txt 2> err.txt
```

* `out.txt` includes a pass/fail line per instance and the serialized keys of both parties.
* `err.txt` is empty on success; mismatches (if any) are printed here.

**Tip (shell redirection):** `>` redirects **stdout** only. Use `> out.txt 2> err.txt` to capture both streams, or `> all.txt 2>&1` to combine.

---

## Output Format

For each instance the program prints:

```
DPF #i (size=..., alpha=..., beta=...) => Test Passed
Key0:
{ "s0": ..., "t0": 0, "cwOut": ..., "cws": [
    { "dSL": ..., "dTL": 0/1, "dSR": ..., "dTR": 0/1 },
    ... per level ...
]}
Key1:
{ "s0": ..., "t0": 1, "cwOut": ..., "cws": [ ... ]}
------------------------------------------------------------
```

* `s0` — root seed for the party.
* `t0` — root control bit (party 0 has 0, party 1 has 1).
* `cws[i]` — per‑level correction words; applied when the running `t` is 1.
* `cwOut` — **leaf seed correction** (XOR) applied by the party whose leaf control bit is 1 at **α**.

---

## How It Works (High‑Level)

### GGM Tree with Correction Words

At each level, a seed `s` expands via a PRG `G(s)` into two child seeds and control bits: `(sL,tL)` and `(sR,tR)`. The two parties hold independent roots `(s0^A,t0^A)` and `(s0^B,t0^B)` with `t0^A ⊕ t0^B = 1`.

Per level, we publish **correction words** `CW[i] = (dSL,dTL,dSR,dTR)` so that:

* Off the programmed path, both parties see **equal** child states after (conditional) application of CWs → their shares cancel.
* On the programmed path bit `a_i`, the combined control bit toggles so that **exactly one** party has `t=1` at the leaf.

### Programming β via a Leaf **Seed** Correction

Instead of adding a numeric correction to the output, we modify the **seed** at the leaf of the unique party where `t=1`:

* If party A has `t=1` at α, set A’s final seed to `s* = β + sB(α)` (mod (2^{64})), and publish `cwOut = sA(α) ⊕ s*`.
* Else modify B’s leaf seed analogously with `s* = sA(α) − β`.
* Evaluator applies `s ^= cwOut` **iff** its leaf `t==1`.

With the extractor `v_from_seed(s)=s` and party‑signs `(+, −)` (party with `t0==1` negates), we get:

* **Off‑path**: both parties have equal seeds → `(+s) + (−s) = 0`.
* **At α**: seeds differ by β and exactly one party applies `cwOut` → sum equals **β**.

---

## Correctness Sketch

Let `sA(x), sB(x)` be the post‑CW seeds at index `x` before the leaf seed correction, and let `tA(x), tB(x)` be the leaf control bits.

* For any **off‑path** `x≠α`, `sA(x)=sB(x)` and `tA(x)=tB(x)`; if both are 0, no correction is applied; if both are 1, the **same** correction is applied to both, so seeds remain equal. With share signs `(+, −)`, reconstruction is 0.
* At **α**, exactly one of `tA(α), tB(α)` is 1. The corresponding party’s seed is XOR‑corrected to `s*` so that `( +s*(α) ) + ( −s_other(α) ) = β (mod 2^{64})`.

The `EvalFull` routine checks this identity for all `x∈[0, size)`.

---

## Complexity

* **Key size:** `O(log |D|)` correction words + a single leaf seed correction.
* **Eval time:** `O(log |D|)` PRG expansions per index.
* **Memory:** `O(log |D|)` per key.

---

## Testing & Debugging

* The program prints `Test Passed` for each generated instance if reconstruction holds.
* Mismatches (if any) are reported to **stderr** with the first few failing indices.
* To capture both streams:

  * Bash/zsh: `./gen_dpf 1024 1 > out.txt 2>&1` or `> out.txt 2> err.txt`
  * PowerShell: `./gen_dpf.exe *> all.txt` or `> out.txt 2> err.txt`

Common pitfalls:

* **Combining streams:** `>` only captures stdout. Use `2>&1` to merge stderr.
* **Old binary:** Rebuild after edits (`make clean` equivalent). On Windows, ensure you run the freshly built `.exe` in the correct directory.

---