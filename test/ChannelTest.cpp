#include "../lib/Channel.hpp"
// #include "lib/Channel.hpp"
#include <gtest/gtest.h>
#include <optional>
#include <thread>

using namespace ::testing;

class ChannelTest : public ::testing::Test {};

TEST_F(ChannelTest, SyncSingleWriter) {
    auto [rx, tx] = create_channel<int>();
    tx.send(10);
    tx.send(20);
    tx.send(30);

    EXPECT_EQ(rx.receive(), 10);
    EXPECT_EQ(rx.receive(), 20);
    EXPECT_EQ(rx.receive(), 30);

    EXPECT_EQ(rx.try_receive(), std::nullopt);
    tx.send(40);
    EXPECT_EQ(rx.receive(), 40);

    // delete rx
    EXPECT_EQ(tx.is_open(), true);
    EXPECT_EQ(rx.is_open(), true);
    { auto x = std::move(rx); }
    EXPECT_EQ(tx.is_open(), false);
}

TEST_F(ChannelTest, ThreadSingleWriter) {
    constexpr int SampleSize = 1000;
    auto [rx, tx] = create_channel<int>();

    auto reader = [](auto rx) {
        for (auto i = 0; i < SampleSize; ++i) {
            EXPECT_EQ(rx.receive(), i);
        }
    };

    auto writer = [](auto tx) {
        for (auto i = 0; i < SampleSize; ++i) {
            tx.send(i);
        }
    };

    auto rx_thread = std::thread(reader, std::move(rx));
    auto tx_thread = std::thread(writer, std::move(tx));

    rx_thread.join();
    tx_thread.join();
}

TEST_F(ChannelTest, TrySendAndReceive) {
    auto [rx, tx] = create_channel<int>();

    EXPECT_EQ(rx.try_receive(), std::nullopt);
    EXPECT_EQ(tx.try_send(10), true);
    EXPECT_EQ(rx.try_receive(), std::optional<int>(10));
    EXPECT_EQ(rx.is_open(), true);
    EXPECT_EQ(tx.is_open(), true);

    rx.close();

    // multiple closes should not break anything, but also not do anything
    tx.close();
    rx.close();

    EXPECT_EQ(tx.is_open(), false);
    EXPECT_EQ(rx.is_open(), false);
    EXPECT_EQ(tx.try_send(10), false);
    EXPECT_EQ(rx.try_receive(), std::nullopt);
}
