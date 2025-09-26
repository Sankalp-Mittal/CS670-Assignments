#include <iostream>
#include <fstream>
#include <vector>
#include <random>
#include <string>
#include <cstdint>
#include <stdexcept>
#include <sstream>
#include "common.hpp"
struct Args {
    int m, n, k, q;
    bool have_seed = false;
    uint64_t seed = 0;
    bool packets = false;
    bool debug = false;          // NEW
};

static bool starts_with(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

static Args parse_args(int argc, char* argv[]) {
    if (argc < 5) {
        throw std::runtime_error(
            "Usage: ./gen_queries <m> <n> <k> <q> [--seed=SEED] [--debug]");
    }
    Args a{};
    a.m = std::stoi(argv[1]);
    a.n = std::stoi(argv[2]);
    a.k = std::stoi(argv[3]);
    a.q = std::stoi(argv[4]);
    if (a.m <= 0 || a.n <= 0 || a.k <= 0 || a.q <= 0) {
        throw std::runtime_error("All of m, n, k, q must be positive.");
    }
    for (int i = 5; i < argc; ++i) {
        std::string arg = argv[i];
        if (starts_with(arg, "--seed=")) {
            a.have_seed = true;
            a.seed = std::stoull(arg.substr(8));
        } else if (arg == "--debug") {       // NEW
            a.debug = true;
        } else {
            std::ostringstream oss;
            oss << "Unknown option: " << arg;
            throw std::runtime_error(oss.str());
        }
    }
    return a;
}

class RNG {
public:
    explicit RNG(bool have_seed, uint64_t seed) {
        if (have_seed) gen_.seed(seed);
        else gen_.seed(std::random_device{}());
    }
    int64_t randint(int64_t lo, int64_t hi) {
        std::uniform_int_distribution<int64_t> dist(lo, hi);
        return dist(gen_);
    }
    int32_t index(int32_t hi) {
        std::uniform_int_distribution<int32_t> dist(0, hi);
        return dist(gen_);
    }
private:
    std::mt19937_64 gen_;
};

int main(int argc, char* argv[]) {
    try {
        Args args = parse_args(argc, argv);
        RNG rng(args.have_seed, args.seed);

        const int m = args.m, n = args.n, k = args.k, q = args.q;
        const int64_t VAL_MIN = -5, VAL_MAX = 5;                      // true latent values
        const int64_t SHARE_MIN = -1'000'000, SHARE_MAX = 1'000'000;  // random share range

        // Underlying integer latent factors
        std::vector<std::vector<int64_t>> U(m, std::vector<int64_t>(k));
        std::vector<std::vector<int64_t>> V(n, std::vector<int64_t>(k));
        for (int i = 0; i < m; ++i)
            for (int d = 0; d < k; ++d)
                U[i][d] = rng.randint(VAL_MIN, VAL_MAX);
        for (int j = 0; j < n; ++j)
            for (int d = 0; d < k; ++d)
                V[j][d] = rng.randint(VAL_MIN, VAL_MAX);

        // Additive shares: U = U0 + U1, V = V0 + V1
        std::vector<std::vector<int64_t>> U0(m, std::vector<int64_t>(k));
        std::vector<std::vector<int64_t>> U1(m, std::vector<int64_t>(k));
        std::vector<std::vector<int64_t>> V0(n, std::vector<int64_t>(k));
        std::vector<std::vector<int64_t>> V1(n, std::vector<int64_t>(k));

        for (int i = 0; i < m; ++i) {
            for (int d = 0; d < k; ++d) {
                int64_t r = rng.randint(SHARE_MIN, SHARE_MAX);
                U0[i][d] = r;
                U1[i][d] = U[i][d] - r;
            }
        }
        for (int j = 0; j < n; ++j) {
            for (int d = 0; d < k; ++d) {
                int64_t r = rng.randint(SHARE_MIN, SHARE_MAX);
                V0[j][d] = r;
                V1[j][d] = V[j][d] - r;
            }
        }

        // Write matrix shares
        const auto write_matrix = [](const std::string& path, int rows, int cols,
                                     const std::vector<std::vector<int64_t>>& M) {

            std::ofstream f(path);
            if (!f) throw std::runtime_error("Failed to open " + path);
            f << rows << " " << cols << "\n";
            for (int r = 0; r < rows; ++r) {
                for (int c = 0; c < cols; ++c) {
                    if (c) f << " ";
                    f << M[r][c];
                }
                f << "\n";
            }
        };

        write_matrix(P0_USER_SHARES_FILE, m, k, U0);
        write_matrix(P1_USER_SHARES_FILE, m, k, U1);
        // write_matrix("/data/p0_shares/p0_V.txt", n, k, V0);
        // write_matrix("/data/p1_shares/p1_V.txt", n, k, V1);

        // Generate queries (i, j)
        std::vector<std::pair<int,int>> queries(q);
        for (int t = 0; t < q; ++t) {
            int ui = rng.index(m - 1);
            int vj = rng.index(n - 1);
            queries[t] = std::make_pair(ui, vj);
        }

        // Write queries file: q then q lines "i j"
        // {
        //     std::ofstream fq("/data/queries.txt");
        //     if (!fq) throw std::runtime_error("Failed to open queries.txt");
        //     fq << q << " " << k << "\n";
        //     for (size_t idx = 0; idx < queries.size(); ++idx) {
        //         const int i = queries[idx].first;
        //         const int j = queries[idx].second;
        //         fq << i << " " << j << "\n";
        //     }
        // }

        // Optional: per-query packets for each party (pulled from pre-shared matrices)
        std::ofstream f0(P0_QUERIES_SHARES_FILE), f1(P1_QUERIES_SHARES_FILE);
        if (!f0 || !f1) throw std::runtime_error("Failed to open p0_queries.txt/p1_queries.txt");
        f0 << q << " " << k << "\n";
        f1 << q << " " << k << "\n";
        for (size_t idx = 0; idx < queries.size(); ++idx) {
            const int i = queries[idx].first;
            const int j = queries[idx].second;

            f0 << i << " ";
            for (int d = 0; d < k; ++d) f0 << " " << V0[j][d];
            f0 << "\n";

            f1 << i << " ";
            for (int d = 0; d < k; ++d) f1 << " " << V1[j][d];
            f1 << "\n";
        }

        // DEBUG OUTPUTS — only when --debug
        if (args.debug) {
            // dump true U, V, and queries for reconstruction checks
            std::ofstream fd("/data/plain_UV.txt");
            if (fd) {
                fd << "U (m=" << m << ", k=" << k << ")\n";
                for (int i = 0; i < m; ++i) {
                    for (int d = 0; d < k; ++d) {
                        if (d) fd << " ";
                        fd << U[i][d];
                    }
                    fd << "\n";
                }
                fd << "V (n=" << n << ", k=" << k << ")\n";
                for (int j = 0; j < n; ++j) {
                    for (int d = 0; d < k; ++d) {
                        if (d) fd << " ";
                        fd << V[j][d];
                    }
                    fd << "\n";
                }
                fd << "queries (q=" << q << ")\n";
                for (size_t idx = 0; idx < queries.size(); ++idx) {
                    fd << queries[idx].first << " " << queries[idx].second << "\n";
                }
                // Write queries file: q then q lines "i j"
                {
                    std::ofstream fq("/data/plain_queries.txt");
                    if (!fq) throw std::runtime_error("Failed to open plain_queries.txt");
                    fq << q << " " << k << "\n";
                    for (size_t idx = 0; idx < queries.size(); ++idx) {
                        const int i = queries[idx].first;
                        const int j = queries[idx].second;
                        fq << i << " " << j << "\n";
                    }
                }
            }

            // console debug summary
            std::cout << "Wrote:\n"
                      << P0_USER_SHARES_FILE << "," << P1_USER_SHARES_FILE << "(matrix shares of U)\n"
                      << P0_QUERIES_SHARES_FILE << "," << P1_QUERIES_SHARES_FILE << "(matrix shares of U)\n";
            std::cout << "(Debug) plain_UV.txt with true values (do NOT give to parties)\n";
        }

        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }
}
