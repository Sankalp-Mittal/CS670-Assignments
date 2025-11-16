# Assignment 3: Secure Item Profile Updates with DPF

## Overview
This implementation completes Assignment 3 for CS670, adding secure item profile updates using Distributed Point Functions (DPF) on top of the existing user profile update protocol from Assignment 1.

## Fixed Components

### Assignment 1 Fixes (User Profile Updates)
- Fixed secure dot product computation using proper MPC multiplication
- Corrected update formula: `u_i ← u_i + v_j * (1 - ⟨u_i, v_j⟩)`
- Fixed share re-randomization and persistence

### Assignment 3 Implementation (Item Profile Updates)
**Protocol (as per assignment PDF):**

1. **User-side DPF Generation**: User generates DPF keys with value 0 at target item index
   - `(k0, k1) ← Gen(item_idx, 0)`
   - Sends k0 to P0, k1 to P1

2. **Server-side Update Computation**: Each server computes its share of `M = ui * (1 - ⟨ui, vj⟩)`
   - Uses MPC to compute dot product shares
   - Computes update shares locally

3. **DPF Correction Word Adjustment**: Servers exchange masked differences
   - P0 sends: `M0 - FCW0`
   - P1 sends: `M1 - FCW1`
   - Both compute: `FCWm = (M0 - FCW0) + (M1 - FCW1)`

4. **DPF Evaluation and Update**: Servers evaluate modified DPF and add to item profiles
   - `Vb ← Vb + EvalFull(kb_modified)`
   - **Note**: Your DPF implementation is already additive, so no XOR→additive conversion is needed

## File Structure

```
.
├── common.hpp              # Common structures, DPF types, file paths
├── pB.cpp                  # Server code (P0/P1) with both assignments
├── p2.cpp                  # Trusted dealer - generates shares and DPF keys
├── gen_dpf.cpp             # DPF key generation utility (if needed standalone)
├── gen_queries.cpp         # Query generation
├── checker.py              # Verification script
├── docker-compose.yml      # Docker setup
└── README.md               # This file
```

## Key Changes from Original Code

### common.hpp
- Added item matrix file paths (`P0_ITEM_SHARES_FILE`, `P1_ITEM_SHARES_FILE`)
- Added DPF structures (`DPFCorrectionWord`, `DPFKey`)

### pB.cpp
- **Assignment 1 fixes**:
  - `update_user_profile_secure()`: Proper MPC dot product and update
  - Correct handling of share arithmetic for `(1 - ⟨u,v⟩)`

- **Assignment 3 implementation**:
  - `send_dpf_key()`, `recv_dpf_key()`: DPF key exchange protocol
  - `evalDPF()`, `evalFullDPF()`: DPF evaluation (additive output)
  - `update_item_profile_with_dpf()`: Complete Assignment 3 protocol
  - Processes each dimension independently with adjusted correction words

### p2.cpp
- Generates DPF keys for each query with `alpha=item_idx`, `beta=0`
- Distributes keys to P0 and P1
- Sends multiplication triples for both user and item updates (2k per query)

## Build and Run

### Using Docker (Recommended)
```bash
docker-compose up --build
```

### Manual Compilation
```bash
# Compile server code
g++ -DROLE_p0 -std=c++20 -I. pB.cpp -lboost_system -lpthread -o p0
g++ -DROLE_p1 -std=c++20 -I. pB.cpp -lboost_system -lpthread -o p1

# Compile dealer
g++ -std=c++20 -I. p2.cpp -lboost_system -lpthread -o p2

# Run (in separate terminals)
./p2
./p0
./p1
```

## Data Files

### Input Files
- `/data/p0_shares/p0_U.txt`: P0's user profile shares (m × k matrix)
- `/data/p1_shares/p1_U.txt`: P1's user profile shares
- `/data/p0_shares/p0_V.txt`: P0's item profile shares (n × k matrix)
- `/data/p1_shares/p1_V.txt`: P1's item profile shares
- `/data/p0_shares/p0_queries.txt`: Query file with format:
  ```
  q k
  user_idx item_idx v[0] v[1] ... v[k-1]
  ...
  ```
- `/data/params.txt`: Parameters `m n k q`

### Output Files
- `/data/client0.results`: P0's updated user profile shares
- `/data/client1.results`: P1's updated user profile shares
- Item profiles are updated in-place in `p0_V.txt` and `p1_V.txt`

## Protocol Execution Flow

1. **Initialization**:
   - P2 generates and distributes query shares
   - P2 generates multiplication triples (2k per query)
   - P2 generates DPF key pairs (one per query)

2. **For each query**:
   - **Assignment 1**: Update user profile
     - Compute `⟨ui, vj⟩` using MPC
     - Compute update `vj * (1 - ⟨ui, vj⟩)` using MPC
     - Apply additive update to user share

   - **Assignment 3**: Update item profile
     - Receive DPF keys from P2
     - Compute local share of `M = ui * (1 - ⟨ui, vj⟩)`
     - Exchange correction word adjustments
     - Evaluate DPF with adjusted keys
     - Apply additive updates to all item profile shares

## Important Notes

### DPF Implementation
- Your `gen_dpf.cpp` already produces **additive** shares (not XOR shares)
- The `evalDPF()` function returns values such that `y0 + y1 = beta` at alpha, `0` elsewhere
- No conversion is needed in Assignment 3 implementation

### MPC Multiplication
- Uses Beaver triples from `DuAtAllahMultClient`
- Formula: `[c] = [a][b]` where `c = a*(b + peer_y) - my_y*peer_x + z`

### Dimensions
- Each query updates **all k dimensions** of both user and item profiles
- Item update processes each dimension independently to adjust correction words

## Verification

Run the checker to verify correctness:
```bash
python3 checker.py
```

## Troubleshooting

### Common Issues
1. **Dimension mismatch**: Ensure all matrices have consistent k
2. **Connection timeout**: Check Docker network or port bindings
3. **File not found**: Verify data directory mounts in docker-compose.yml

### Debug Output
Enable verbose logging by checking console output from P0, P1, and P2.
