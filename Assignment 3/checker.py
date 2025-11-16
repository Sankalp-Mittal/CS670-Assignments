#!/usr/bin/env python3
"""
Checker for Assignment 1 and Assignment 3
Verifies correctness of user and item profile updates
"""

import sys
import os

def read_matrix(filepath):
    """Read a matrix file"""
    with open(filepath, 'r') as f:
        lines = f.readlines()
        m, k = map(int, lines[0].split())
        matrix = []
        for i in range(1, m+1):
            row = list(map(int, lines[i].split()))
            matrix.append(row)
        return matrix

def read_queries(filepath):
    """Read queries file"""
    with open(filepath, 'r') as f:
        lines = f.readlines()
        q, k = map(int, lines[0].split())
        queries = []
        for i in range(1, q+1):
            query = list(map(int, lines[i].split()))
            queries.append(query)
        return queries

def reconstruct_matrix(M0, M1):
    """Reconstruct matrix from shares"""
    m = len(M0)
    k = len(M0[0])
    M = [[M0[i][j] + M1[i][j] for j in range(k)] for i in range(m)]
    return M

def dot_product(u, v):
    """Compute dot product"""
    return sum(u[i] * v[i] for i in range(len(u)))

def check_assignment1(U0_init, U1_init, U0_final, U1_final, V, queries):
    """Check user profile updates (Assignment 1)"""
    print("\n=== Checking Assignment 1: User Profile Updates ===")

    U_init = reconstruct_matrix(U0_init, U1_init)
    U_final = reconstruct_matrix(U0_final, U1_final)

    errors = []
    for qidx, query in enumerate(queries):
        user_idx = query[0]
        item_vals = query[2:]

        ui_init = U_init[user_idx]
        ui_final = U_final[user_idx]
        vj = item_vals

        # Expected update: u_i ← u_i + v_j * (1 - <u_i, v_j>)
        dot = dot_product(ui_init, vj)
        factor = 1 - dot

        ui_expected = [ui_init[d] + vj[d] * factor for d in range(len(ui_init))]

        # Check if update is correct
        match = all(abs(ui_final[d] - ui_expected[d]) < 1e-6 for d in range(len(ui_init)))

        if match:
            print(f"  Query {qidx} (user {user_idx}): ✓ PASS")
        else:
            print(f"  Query {qidx} (user {user_idx}): ✗ FAIL")
            print(f"    Expected: {ui_expected}")
            print(f"    Got:      {ui_final}")
            errors.append(qidx)

    if errors:
        print(f"\n✗ Assignment 1 FAILED: {len(errors)} errors")
        return False
    else:
        print(f"\n✓ Assignment 1 PASSED: All {len(queries)} updates correct")
        return True

def check_assignment3(V0_init, V1_init, V0_final, V1_final, U, queries):
    """Check item profile updates (Assignment 3)"""
    print("\n=== Checking Assignment 3: Item Profile Updates ===")

    V_init = reconstruct_matrix(V0_init, V1_init)
    V_final = reconstruct_matrix(V0_final, V1_final)

    errors = []
    for qidx, query in enumerate(queries):
        user_idx = query[0]
        item_idx = query[1]

        ui = U[user_idx]
        vj_init = V_init[item_idx]
        vj_final = V_final[item_idx]

        # Expected update: v_j ← v_j + u_i * (1 - <u_i, v_j>)
        dot = dot_product(ui, vj_init)
        factor = 1 - dot

        vj_expected = [vj_init[d] + ui[d] * factor for d in range(len(vj_init))]

        # Check if update is correct
        match = all(abs(vj_final[d] - vj_expected[d]) < 1e-6 for d in range(len(vj_init)))

        if match:
            print(f"  Query {qidx} (item {item_idx}): ✓ PASS")
        else:
            print(f"  Query {qidx} (item {item_idx}): ✗ FAIL")
            print(f"    Expected: {vj_expected}")
            print(f"    Got:      {vj_final}")
            errors.append(qidx)

    if errors:
        print(f"\n✗ Assignment 3 FAILED: {len(errors)} errors")
        return False
    else:
        print(f"\n✓ Assignment 3 PASSED: All {len(queries)} updates correct")
        return True

def main():
    print("=" * 60)
    print("Assignment 1 & 3 Verification Checker")
    print("=" * 60)

    try:
        # Read initial shares
        print("\nReading initial shares...")
        U0_init = read_matrix("data/p0_shares/p0_U.txt")
        U1_init = read_matrix("data/p1_shares/p1_U.txt")
        V0_init = read_matrix("data/p0_shares/p0_V.txt")
        V1_init = read_matrix("data/p1_shares/p1_V.txt")

        # Read final shares (after updates)
        print("Reading final shares...")
        # Note: These should be the updated files from pB execution
        # For now, using same as init (update this after running pB)
        U0_final = read_matrix("data/p0_shares/p0_U.txt")
        U1_final = read_matrix("data/p1_shares/p1_U.txt")
        V0_final = read_matrix("data/p0_shares/p0_V.txt")
        V1_final = read_matrix("data/p1_shares/p1_V.txt")

        # Read queries
        print("Reading queries...")
        queries = read_queries("data/queries.txt")

        # Reconstruct user profiles (for item update verification)
        U = reconstruct_matrix(U0_init, U1_init)

        # Check Assignment 1
        a1_pass = check_assignment1(U0_init, U1_init, U0_final, U1_final, None, queries)

        # Check Assignment 3
        a3_pass = check_assignment3(V0_init, V1_init, V0_final, V1_final, U, queries)

        # Summary
        print("\n" + "=" * 60)
        print("SUMMARY")
        print("=" * 60)
        print(f"Assignment 1: {'✓ PASS' if a1_pass else '✗ FAIL'}")
        print(f"Assignment 3: {'✓ PASS' if a3_pass else '✗ FAIL'}")

        if a1_pass and a3_pass:
            print("\n🎉 ALL TESTS PASSED!")
            return 0
        else:
            print("\n❌ SOME TESTS FAILED")
            return 1

    except FileNotFoundError as e:
        print(f"\n✗ Error: File not found - {e}")
        print("Make sure to run gen_test_data.py first and execute the protocol")
        return 1
    except Exception as e:
        print(f"\n✗ Error: {e}")
        import traceback
        traceback.print_exc()
        return 1

if __name__ == "__main__":
    sys.exit(main())
