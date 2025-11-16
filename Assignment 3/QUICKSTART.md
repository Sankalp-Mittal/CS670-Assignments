# Quick Start Guide - Assignment 3

## Prerequisites
- C++20 compiler (g++)
- Boost libraries (libboost-all-dev)
- Python 3 (for test data generation and checking)
- Docker & Docker Compose (optional)

## Method 1: Local Build

### Step 1: Generate Test Data
```bash
python3 gen_test_data.py 10 20 5 8
# Parameters: m=10 users, n=20 items, k=5 dimensions, q=8 queries
```

### Step 2: Build
```bash
make all
# Or manually:
# make p0
# make p1
# make p2
```

### Step 3: Run (in separate terminals)
```bash
# Terminal 1
./p2

# Terminal 2
./p0

# Terminal 3
./p1
```

### Step 4: Verify
```bash
python3 checker.py
```

## Method 2: Docker (Recommended)

### Step 1: Generate Test Data
```bash
python3 gen_test_data.py 10 20 5 8
```

### Step 2: Build and Run
```bash
docker-compose up --build
```

### Step 3: Verify
```bash
python3 checker.py
```

## Expected Output

### P2 (Dealer)
```
P2 server starting...
Listening on port 9002...
P0 connected.
P1 connected.
Generating 8 query shares...
Sent all query shares...
Generating DPF keys for 8 queries...
All DPF keys sent. P2 server done.
```

### P0 / P1 (Servers)
```
Connecting to P2...
Total shares received from P2: 8
Total multiplication triples received: 8 sets of 10 each
Setting up peer connection...
Preprocessing complete, ready to process queries
Read 8 queries

=== Processing query #0 ===
Updating user profile for user #3
  Computing update value share...
User profile #3 updated successfully
Assignment 3: Updating item profile #7 (query by user #3)
  Receiving DPF key from user...
  Computing update value share...
  Adjusting DPF correction word...
Item profile #7 updated successfully (Assignment 3)
Query #0 completed

...
All queries processed successfully!
```

### Checker
```
Assignment 1 & 3 Verification Checker
Reading initial shares...
Reading final shares...
Reading queries...

=== Checking Assignment 1: User Profile Updates ===
  Query 0 (user 3): ✓ PASS
  Query 1 (user 5): ✓ PASS
  ...
✓ Assignment 1 PASSED: All 8 updates correct

=== Checking Assignment 3: Item Profile Updates ===
  Query 0 (item 7): ✓ PASS
  Query 1 (item 12): ✓ PASS
  ...
✓ Assignment 3 PASSED: All 8 updates correct

SUMMARY
Assignment 1: ✓ PASS
Assignment 3: ✓ PASS

🎉 ALL TESTS PASSED!
```

## Troubleshooting

### "Connection refused" error
- Make sure P2 starts first
- Check that ports 9001-9002 are not in use
- For Docker: ensure network is created (`docker network ls`)

### "File not found" error
- Run `python3 gen_test_data.py` first
- Check that `data/` directory exists
- Verify file permissions

### Compilation errors
- Install Boost: `sudo apt-get install libboost-all-dev`
- Check C++20 support: `g++ --version` (need g++ 10+)

### Dimension mismatch
- Ensure all matrix files have consistent `k` value
- Regenerate test data with matching parameters

## File Outputs

After successful execution:
- User profiles updated in `data/p0_shares/p0_U.txt` and `p1_U.txt`
- Item profiles updated in `data/p0_shares/p0_V.txt` and `p1_V.txt`
- Logs in `data/client0.results` and `client1.results`

## Next Steps

- Vary parameters (m, n, k, q) to test scalability
- Implement Assignment 4 (performance evaluation)
- Add secure XOR-to-additive conversion for bonus points
