#include "common.hpp"
#include <boost/asio.hpp>
#include <iostream>
#include <random>
#include <fstream> 
#include <vector>          // NEW: used by serializer
#include <string>          // NEW: used by serializer

using boost::asio::ip::tcp;

// Random vector generation for the Du-At-Allah protocol
// Original make_random just returns shares now
inline auto make_random_once(int k) {
    DuAtAllahServer security(k);
    auto [client0, client1] = security.generate_client_shares();
    return std::make_pair(std::move(client0), std::move(client1));
}

inline auto make_random_mul_once(){
    DuAtAllahMultServer dmul_s;
    DuAtAllahMultClient dmul_c0, dmul_c1;
    dmul_c0.x = dmul_s.x0;
    dmul_c0.y = dmul_s.y0;
    dmul_c0.z = dmul_s.alpha;
    dmul_c1.x = dmul_s.x1;
    dmul_c1.y = dmul_s.y1;
    dmul_c1.z = -dmul_s.alpha;
    return std::make_pair(std::move(dmul_c0), std::move(dmul_c1));
}

// Append helper
inline void append_shares(std::ofstream& f0, std::ofstream& f1, const DuAtAllahClient& s0, const DuAtAllahClient& s1) {
    
    int k = s0.X.size();
    for(int i = 0; i < k; ++i) {
        f0 << s0.X[i] << (i == k - 1 ? "" : " ");
    }
    f0 << '\n';
    for(int i = 0; i < k; ++i) {
        f0 << s0.Y[i] << (i == k - 1 ? "" : " ");
    }
    f0 << s0.z << '\n';
    f0<<'\n';

    for(int i = 0; i < k; ++i) {
        f1 << s1.X[i] << (i == k - 1 ? "" : " ");
    }
    f1 << '\n';
    for(int i = 0; i < k; ++i) {
        f1 << s1.Y[i] << (i == k - 1 ? "" : " ");
    }
    f1 << s1.z << '\n';
    f1 << '\n';
}

// Modified handle_client: q is passed as argument
/*boost::asio::awaitable<void> handle_client(tcp::socket socket,
                                           const std::string& name,
                                           int q,
                                           int k,
                                           const std::string& file0 = "client0.shares",
                                           const std::string& file1 = "client1.shares") {
    try {
        std::cout << "P2 handling client " << name << ", generating " << q << " shares\n";

        // Open files in append mode
        std::ofstream f0(file0, std::ios::out | std::ios::app);
        std::ofstream f1(file1, std::ios::out | std::ios::app);
        if (!f0 || !f1) {
            std::cerr << "Error opening output files\n";
            co_return;
        }

        // Generate q share pairs and dump
        for (int i = 0; i < q; ++i) {
            auto [c0, c1] = make_random_once(k);
            append_shares(f0, f1, c0, c1);
        }

        std::cout << "Done writing " << q << " shares for " << name << "\n";

        // (Optional) acknowledge success to the client
        static const char ok[] = "OK\n";
        co_await boost::asio::async_write(socket,
                                          boost::asio::buffer(ok, sizeof(ok) - 1),
                                          boost::asio::use_awaitable);
    } catch (const std::exception& e) {
        std::cerr << "Error in handle_client: " << e.what() << "\n";
    }
    co_return;
}*/

// Serialize a client's share exactly like the file format (X line, Y line, z line, blank)
inline std::string serialize_share_text(const DuAtAllahClient& s) {
    std::string out;

    // X
    for (int i = 0; i < (int)s.X.size(); ++i) {
        out += std::to_string(s.X[i]);
        if (i + 1 != (int)s.X.size()) out += ' ';
    }
    out += '\n';

    // Y
    for (int i = 0; i < (int)s.Y.size(); ++i) {
        out += std::to_string(s.Y[i]);
        if (i + 1 != (int)s.Y.size()) out += ' ';
    }
    out += '\n';

    // z
    out += std::to_string(s.z);
    out += '\n';

    // blank separator
    out += '\n';
    return out;
}

static inline std::string serialize_triples_header(int q, int k) {
    // One header line: "TRIPLES q k\n"
    return "TRIPLES " + std::to_string(q) + " " + std::to_string(k) + "\n";
}

static inline std::string serialize_triple_line(const DuAtAllahMultClient& m) {
    // One triple per line: "x y z\n"
    return std::to_string(m.x) + " " + std::to_string(m.y) + " " + std::to_string(m.z) + "\n";
}

// send a serialized text blob to a socket
static inline boost::asio::awaitable<void>
send_text(tcp::socket& socket, const std::string& payload) {
    co_await boost::asio::async_write(socket, boost::asio::buffer(payload), boost::asio::use_awaitable);
    co_return;
}

// Single coroutine that generates pairs and streams s0->P0, s1->P1 each iteration.
// This guarantees both clients receive matching shares per query.
boost::asio::awaitable<void> serve_pairs(tcp::socket socket_p0,
                                         tcp::socket socket_p1,
                                         int q,
                                         int k)
{
    try {
        // ---------- SHARES BLOCK (UNCHANGED) ----------
        std::cout << "P2: streaming " << q << " share pairs (k=" << k << ") to P0 and P1\n";

        for (int i = 0; i < q; ++i) {
            auto [c0, c1] = make_random_once(k);

            std::string msg0 = serialize_share_text(c0); // X\nY\nz\n\n
            std::string msg1 = serialize_share_text(c1);

            co_await send_text(socket_p0, msg0);
            co_await send_text(socket_p1, msg1);

            if ((i + 1) % 100 == 0 || i + 1 == q) {
                std::cout << "  sent " << (i + 1) << "/" << q << " shares\n";
            }
        }

        // End-of-shares sentinel (what your clients already expect)
        static constexpr char ok[] = "OK\n";
        co_await boost::asio::async_write(socket_p0, boost::asio::buffer(ok, sizeof(ok) - 1), boost::asio::use_awaitable);
        co_await boost::asio::async_write(socket_p1, boost::asio::buffer(ok, sizeof(ok) - 1), boost::asio::use_awaitable);

        std::cout << "P2: done streaming shares.\n";

        // ---------- TRIPLES BLOCK (NEW) ----------
        std::cout << "P2: streaming " << q << " multiplication triples (k=" << k << ") to P0 and P1\n";

        // Header line so clients know counts for RAM allocation
        std::string header = "TRPL " + std::to_string(q) + " " + std::to_string(k) + "\n";
        co_await boost::asio::async_write(socket_p0, boost::asio::buffer(header), boost::asio::use_awaitable);
        co_await boost::asio::async_write(socket_p1, boost::asio::buffer(header), boost::asio::use_awaitable);

        // Send q*k triples as lines: "x y z\n" (per client)
        for (int i = 0; i < q; ++i) {
            for (int d = 0; d < k; ++d) {
                auto [m0, m1] = make_random_mul_once();   // <-- your correlated randomness per dim

                std::string ln0 = serialize_triple_line(m0); // "x y z\n"
                std::string ln1 = serialize_triple_line(m1);
                co_await boost::asio::async_write(socket_p0, boost::asio::buffer(ln0), boost::asio::use_awaitable);
                co_await boost::asio::async_write(socket_p1, boost::asio::buffer(ln1), boost::asio::use_awaitable);
            }
        }

        // End-of-triples sentinel
        static constexpr char tok[] = "TOK\n";
        co_await boost::asio::async_write(socket_p0, boost::asio::buffer(tok, sizeof(tok) - 1), boost::asio::use_awaitable);
        co_await boost::asio::async_write(socket_p1, boost::asio::buffer(tok, sizeof(tok) - 1), boost::asio::use_awaitable);

        std::cout << "P2: done streaming multiplication triples.\n";
    } catch (const std::exception& e) {
        std::cerr << "serve_pairs error: " << e.what() << "\n";
    }
    co_return;
}


// Run multiple coroutines in parallel
template <typename... Fs>
void run_in_parallel(boost::asio::io_context& io, Fs&&... funcs) {
    (boost::asio::co_spawn(io, funcs, boost::asio::detached), ...);
}

int main() {
    try {
        boost::asio::io_context io_context;

        tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), 9002));

        // Accept clients
        tcp::socket socket_p0(io_context);
        acceptor.accept(socket_p0);

        tcp::socket socket_p1(io_context);
        acceptor.accept(socket_p1);

        std::ifstream fin(P0_QUERIES_SHARES_FILE);
        int q,k;
        fin >> q >> k;
        fin.close();

        // Launch all coroutines in parallel
        // (Keep your structure but use a single NEW coroutine to ensure matched shares)
        run_in_parallel(io_context,
            [&]() -> boost::asio::awaitable<void> {
                // NEW: stream matched pairs to each client instead of dumping to files
                co_await serve_pairs(std::move(socket_p0), std::move(socket_p1), q, k);
            }
            // NOTE: we no longer co-spawn two handle_client() here,
            // because serve_pairs() guarantees the i-th pair is split
            // consistently between P0 and P1.
        );

        io_context.run();

    } catch (std::exception& e) {
        std::cerr << "Exception in P2: " << e.what() << "\n";
    }
}
