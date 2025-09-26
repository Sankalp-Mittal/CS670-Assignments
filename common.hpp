#pragma once
#include <utility>
#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <iostream>
#include<vector>
#include<utility>
#include <random>

using boost::asio::awaitable;
using boost::asio::co_spawn;
using boost::asio::detached;
using boost::asio::use_awaitable;
using boost::asio::ip::tcp;
namespace this_coro = boost::asio::this_coro;

#define UPPER_LIM 100

inline uint32_t random_uint32() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint32_t> dis;
    return dis(gen);
}

// Blind by XOR mask
inline uint32_t blind_value(uint32_t v) {
    return v ^ 0xDEADBEEF;
}

struct random_vector{
    std::vector<int> data;

    random_vector(size_t k) : data(k) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dist(-UPPER_LIM, UPPER_LIM);

        // Fill vector with random values
        for (auto &x : data) {
            x = dist(gen);
        }
    }

    int& operator[](size_t i) {
        return data[i];
    }

    const int& operator[](size_t i) const {
        return data[i];
    }

    // Get length
    size_t size() const {
        return data.size();
    }

    int dot_product(random_vector& x){
        int val = 0;
        int k = this->data.size();
        for(int i=0;i<k;i++){
            val += data[i]*(x.data[i]);
        }
        return val;
    }
};

struct DuAtAllahClient{
    std::vector<int> X, Y;
    int z;
};

struct DuAtAllahServer{
    random_vector X0, X1, Y0, Y1;
    int alpha;

    DuAtAllahServer(int k) : X0(k), X1(k), Y0(k), Y1(k) {
        alpha = random_uint32();
    }

    std::pair<DuAtAllahClient,DuAtAllahClient> generate_client_shares(){
        DuAtAllahClient client0, client1;
        int k = X0.size();
        client0.X.resize(k);
        client0.Y.resize(k);
        client0.X = X0.data;
        client0.Y = Y0.data;
        client0.z = X0.dot_product(Y1) + alpha;
        client1.X.resize(k);
        client1.Y.resize(k);
        client1.X = X1.data;
        client1.Y = Y1.data;
        client1.z = Y0.dot_product(X1) - alpha;
        return {client0, client1};
    }
};
