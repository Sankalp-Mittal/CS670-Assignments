#!/usr/bin/env python3
"""
Generate test data files for Assignment 3
Creates user matrix, item matrix, and queries with proper formatting
"""

import random
import sys
import os

def generate_shares(m, n, k, q):
    """Generate secret shares for users, items, and queries"""

    print(f"Generating test data: m={m}, n={n}, k={k}, q={q}")

    # Create data directories
    os.makedirs("data/p0_shares", exist_ok=True)
    os.makedirs("data/p1_shares", exist_ok=True)

    # Generate user matrix shares (m users × k dimensions)
    print("Generating user profile shares...")
    U = [[random.randint(-100, 100) for _ in range(k)] for _ in range(m)]
    U0 = [[random.randint(-100, 100) for _ in range(k)] for _ in range(m)]
    U1 = [[U[i][j] - U0[i][j] for j in range(k)] for i in range(m)]

    with open("data/p0_shares/p0_U.txt", "w") as f:
        f.write(f"{m} {k}\n")
        for row in U0:
            f.write(" ".join(map(str, row)) + "\n")

    with open("data/p1_shares/p1_U.txt", "w") as f:
        f.write(f"{m} {k}\n")
        for row in U1:
            f.write(" ".join(map(str, row)) + "\n")

    print(f"  User profiles: {m} users, {k} dimensions")

    # Generate item matrix shares (n items × k dimensions)
    print("Generating item profile shares...")
    V = [[random.randint(-100, 100) for _ in range(k)] for _ in range(n)]
    V0 = [[random.randint(-100, 100) for _ in range(k)] for _ in range(n)]
    V1 = [[V[i][j] - V0[i][j] for j in range(k)] for i in range(m)]

    with open("data/p0_shares/p0_V.txt", "w") as f:
        f.write(f"{n} {k}\n")
        for row in V0:
            f.write(" ".join(map(str, row)) + "\n")

    with open("data/p1_shares/p1_V.txt", "w") as f:
        f.write(f"{n} {k}\n")
        for row in V1:
            f.write(" ".join(map(str, row)) + "\n")

    print(f"  Item profiles: {n} items, {k} dimensions")

    # Generate queries (format: user_idx item_idx v[0] ... v[k-1])
    print("Generating queries...")
    queries = []
    for _ in range(q):
        user_idx = random.randint(0, m-1)
        item_idx = random.randint(0, n-1)
        # Item values are from the actual item (for this query)
        item_vals = V[item_idx]
        query = [user_idx, item_idx] + item_vals
        queries.append(query)

    with open("data/queries.txt", "w") as f:
        f.write(f"{q} {k}\n")
        for query in queries:
            f.write(" ".join(map(str, query)) + "\n")

    # Share queries for P0 and P1
    with open("data/p0_shares/p0_queries.txt", "w") as f:
        f.write(f"{q} {k}\n")
        for query in queries:
            f.write(" ".join(map(str, query)) + "\n")

    with open("data/p1_shares/p1_queries.txt", "w") as f:
        f.write(f"{q} {k}\n")
        for query in queries:
            f.write(" ".join(map(str, query)) + "\n")

    print(f"  Queries: {q} total")

    # Write parameters
    with open("data/params.txt", "w") as f:
        f.write(f"{m} {n} {k} {q}\n")

    print("✓ Test data generation complete!")
    print(f"  Files created in data/ directory")

if __name__ == "__main__":
    if len(sys.argv) == 5:
        m, n, k, q = map(int, sys.argv[1:5])
    else:
        # Default parameters
        m, n, k, q = 10, 20, 5, 8
        print(f"Using default parameters: m={m}, n={n}, k={k}, q={q}")
        print("Usage: python3 gen_test_data.py <m> <n> <k> <q>")

    generate_shares(m, n, k, q)
