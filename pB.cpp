#include "common.hpp"
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/read.hpp>
// NEW:
#include <boost/asio/streambuf.hpp>
#include <boost/asio/connect.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>

using boost::asio::awaitable;
using boost::asio::use_awaitable;
using boost::asio::ip::tcp;


#if !defined(ROLE_p0) && !defined(ROLE_p1)
#error "ROLE must be defined as ROLE_p0 or ROLE_p1"
#endif

// ----------------------- Helper coroutines -----------------------
awaitable<void> send_coroutine(tcp::socket& sock, uint32_t value) {
    co_await boost::asio::async_write(sock, boost::asio::buffer(&value, sizeof(value)), use_awaitable);
}

awaitable<void> recv_coroutine(tcp::socket& sock, uint32_t& out) {
    co_await boost::asio::async_read(sock, boost::asio::buffer(&out, sizeof(out)), use_awaitable);
}

// Blinded exchange between peers
awaitable<void> exchange_blinded(tcp::socket socket, uint32_t value_to_send) {
    uint32_t blinded = blind_value(value_to_send);
    uint32_t received;

    co_await send_coroutine(socket, blinded);
    co_await recv_coroutine(socket, received);

    std::cout << "Received blinded value from other party: " << received << std::endl;
    co_return;
}

// ----------------------- Setup connections -----------------------

// Setup connection to P2 (P0/P1 act as clients, P2 acts as server)
awaitable<tcp::socket> setup_server_connection(boost::asio::io_context& io_context, tcp::resolver& resolver) {
    tcp::socket sock(io_context);

    // Connect to P2
    auto endpoints_p2 = resolver.resolve("p2", "9002");
    co_await boost::asio::async_connect(sock, endpoints_p2, use_awaitable);

    co_return sock;
}

// Receive random value from P2
awaitable<uint32_t> recv_from_P2(tcp::socket& sock) {
    uint32_t received;
    co_await recv_coroutine(sock, received);
    co_return received;
}

// Setup peer connection between P0 and P1
awaitable<tcp::socket> setup_peer_connection(boost::asio::io_context& io_context, tcp::resolver& resolver) {
    tcp::socket sock(io_context);
#ifdef ROLE_p0
    auto endpoints_p1 = resolver.resolve("p1", "9001");
    co_await boost::asio::async_connect(sock, endpoints_p1, use_awaitable);
#else
    tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), 9001));
    sock = co_await acceptor.async_accept(use_awaitable);
#endif
    co_return sock;
}

// ----------------------- NEW: Share reception + storage -----------------------
struct ReceivedShare {
    std::vector<int> X;
    std::vector<int> Y;
    int z = 0;
};

static inline void rstrip_cr(std::string& s) {
    if (!s.empty() && s.back() == '\r') s.pop_back();
}

awaitable<void> read_line(tcp::socket& sock, boost::asio::streambuf& buf, std::string& line) {
    co_await boost::asio::async_read_until(sock, buf, '\n', use_awaitable);
    std::istream is(&buf);
    std::getline(is, line);
    rstrip_cr(line);
    co_return;
}

static inline std::vector<int> parse_int_line(const std::string& line) {
    std::vector<int> out;
    std::istringstream iss(line);
    int v;
    while (iss >> v) out.push_back(v);
    return out;
}

static inline void print_share_debug(const ReceivedShare& s, std::size_t idx) {
    std::cout << "Share #" << idx << ":\n";
    std::cout << "  X: ";
    for (std::size_t i = 0; i < s.X.size(); ++i) { if (i) std::cout << ' '; std::cout << s.X[i]; }
    std::cout << "\n  Y: ";
    for (std::size_t i = 0; i < s.Y.size(); ++i) { if (i) std::cout << ' '; std::cout << s.Y[i]; }
    std::cout << "\n  z: " << s.z << "\n";
}

awaitable<void> recv_all_shares_from_P2(tcp::socket& sock, std::vector<ReceivedShare>& store) {
    boost::asio::streambuf buf;
    for (;;) {
        std::string line1;
        // skip empty lines
        do { co_await read_line(sock, buf, line1); } while (line1.empty());

        if (line1 == "OK") break;

        ReceivedShare s;
        s.X = parse_int_line(line1);

        std::string line2; co_await read_line(sock, buf, line2);
        s.Y = parse_int_line(line2);

        std::string line3; co_await read_line(sock, buf, line3);
        { std::istringstream iss(line3); iss >> s.z; }

        // separator (can be blank, read and drop)
        std::string sep; co_await read_line(sock, buf, sep);

        store.emplace_back(std::move(s));
        print_share_debug(store.back(), store.size());
    }
    std::cout << "Total shares received from P2: " << store.size() << std::endl;
    co_return;
}

// ----------------------- NEW: File persistence -----------------------
#ifdef ROLE_p0
static constexpr const char* SHARE_LOG_PATH  = "/data/client0.shares";
static constexpr const char* RESULT_LOG_PATH = "/data/client0.results";
#else
static constexpr const char* SHARE_LOG_PATH  = "/data/client1.shares";
static constexpr const char* RESULT_LOG_PATH = "/data/client1.results";
#endif

static inline void append_my_share_to_file(const ReceivedShare& s, std::size_t idx) {
    std::ofstream f(SHARE_LOG_PATH, std::ios::app);
    if (!f) { std::cerr << "Failed to open " << SHARE_LOG_PATH << " for append\n"; return; }
    f << "# query " << idx << "\n";
    for (size_t i = 0; i < s.X.size(); ++i) { if (i) f << ' '; f << s.X[i]; } f << '\n';
    for (size_t i = 0; i < s.Y.size(); ++i) { if (i) f << ' '; f << s.Y[i]; } f << '\n';
    f << s.z << "\n\n";
}

static inline void append_result_share_to_file(std::size_t idx, long long my_share, const std::vector<long long>& qvec) {
    std::ofstream f(RESULT_LOG_PATH, std::ios::app);
    if (!f) { std::cerr << "Failed to open " << RESULT_LOG_PATH << " for append\n"; return; }
    f << "query " << idx << " result_share " << my_share << " | q=";
    for (size_t i = 0; i < qvec.size(); ++i) { if (i) f << ' '; f << qvec[i]; }
    f << "\n";
}

// ----------------------- NEW: Barriers to keep both clients in lockstep -----------------------
awaitable<void> barrier_prep(tcp::socket& peer) {
#ifdef ROLE_p0
    uint32_t code = 1; // PREP
    co_await send_coroutine(peer, code);
    uint32_t ack; co_await recv_coroutine(peer, ack);
#else
    uint32_t code; co_await recv_coroutine(peer, code);
    co_await send_coroutine(peer, code);
#endif
    co_return;
}

awaitable<void> barrier_query(tcp::socket& peer, uint32_t idx) {
#ifdef ROLE_p0
    uint32_t code = 2; // QUERY
    co_await send_coroutine(peer, code);
    co_await send_coroutine(peer, idx);
    uint32_t code2, idx2;
    co_await recv_coroutine(peer, code2);
    co_await recv_coroutine(peer, idx2);
    if (code2 != 2 || idx2 != idx) {
        std::cerr << "Barrier mismatch (sent idx=" << idx << ", got idx=" << idx2 << ")\n";
    }
#else
    uint32_t code_in, idx_in;
    co_await recv_coroutine(peer, code_in);
    co_await recv_coroutine(peer, idx_in);
    co_await send_coroutine(peer, code_in);
    co_await send_coroutine(peer, idx_in);
#endif
    co_return;
}

// ----------------------- NEW: Query file loader -----------------------
static std::vector<std::vector<long long>> read_queries_file(const std::string& path, int expected_k) {
    std::ifstream fin(path);
    std::vector<std::vector<long long>> queries;
    if (!fin) {
        std::cerr << "Failed to open " << path << "\n";
        return queries;
    }
    long long q = 0;
    if (!(fin >> q)) {
        std::cerr << "queries.txt: first token must be q\n";
        return queries;
    }
    queries.reserve(static_cast<size_t>(q));
    for (long long i = 0; i < q; ++i) {
        std::vector<long long> v(expected_k);
        for (int j = 0; j < expected_k; ++j) {
            if (!(fin >> v[j])) {
                std::cerr << "queries.txt: not enough numbers on query " << i << " (expected " << expected_k << ")\n";
                v.resize(j);
                break;
            }
        }
        if ((int)v.size() == expected_k) queries.emplace_back(std::move(v));
        else break;
    }
    return queries;
}

// ----------------------- NEW: MPC dot product stub (fill this) -----------------------
static long long mpc_dot_product(const std::vector<long long>& q,
                                 const ReceivedShare& s,
                                 tcp::socket& peer_sock /* use for your protocol */) {
    // TODO: replace with your real MPC dot product using s.(X,Y,z) and peer communication.
    // For now, return 0 as a placeholder.
    (void)q; (void)s; (void)peer_sock;
    return 0;
}

// ----------------------- Main protocol -----------------------
awaitable<void> run(boost::asio::io_context& io_context) {
    tcp::resolver resolver(io_context);

    // Step 1: connect to P2 and receive ALL shares (preprocessing)
    tcp::socket server_sock = co_await setup_server_connection(io_context, resolver);
    std::vector<ReceivedShare> received_shares;
    co_await recv_all_shares_from_P2(server_sock, received_shares);

    std::cout << (
#ifdef ROLE_p0
        "P0"
#else
        "P1"
#endif
    ) << " finished receiving shares from P2\n";

    // Step 2: connect to peer (P0 <-> P1)
    tcp::socket peer_sock = co_await setup_peer_connection(io_context, resolver);

    // Step 3: PREPROCESSING BARRIER — both must reach here before starting queries
    co_await barrier_prep(peer_sock);

    // Step 4: read queries (same file path inside both containers)
    const int k = received_shares.empty() ? 0 : static_cast<int>(received_shares.front().X.size());
    auto queries = read_queries_file("queries.txt", k);
    if (k == 0 || queries.empty()) {
        std::cerr << "No shares or no queries to process (k=" << k << ", queries=" << queries.size() << ")\n";
        co_return;
    }
    if (queries.size() > received_shares.size()) {
        std::cerr << "Warning: queries (" << queries.size() << ") > shares (" << received_shares.size()
                  << "); truncating to available shares.\n";
        queries.resize(received_shares.size());
    }

    // Step 5: process each query in strict lockstep
    for (std::size_t i = 0; i < queries.size(); ++i) {
        // per-query barrier to ensure both parties use the same query index
        co_await barrier_query(peer_sock, static_cast<uint32_t>(i));

        // (optional) dump the share we are about to use
        append_my_share_to_file(received_shares[i], i);

        // compute the MPC dot product share (you will implement the real protocol)
        long long my_result_share = mpc_dot_product(queries[i], received_shares[i], peer_sock);

        // persist result share
        append_result_share_to_file(i, my_result_share, queries[i]);

        // debug print
        std::cout << "Processed query #" << i << ", my_result_share=" << my_result_share << "\n";
    }

    // (Optional) you can still do your blinded exchange demo if you want, using last z:
    if (!received_shares.empty()) {
        uint32_t val = static_cast<uint32_t>(received_shares.back().z);
        co_await exchange_blinded(std::move(peer_sock), val);
    }

    co_return;
}

int main() {
    std::cout.setf(std::ios::unitbuf); // auto-flush cout for Docker logs
    boost::asio::io_context io_context(1);
    co_spawn(io_context, run(io_context), boost::asio::detached);
    io_context.run();
    return 0;
}
