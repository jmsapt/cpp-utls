#pragma once
/* A thread safe, single reader/writer ring buffer
 *
 * Notes:
 * - Internal data is allocated to allow for cheap move operations
 * - It is templated with a statically sized buffer to eleminate the additional
 * indirection that would happen if the buffer were also allocated
 * - This is a single reader, single writer ring buffer design which allow for
 * no sync. primitives^
 * - To prevent accidental constructor of multi receivers/senders, the channels
 * must be created via the helper function.
 * - A channel is closed when the other end "hangs-up" (i.e. is dropped)
 * - The shared internal state is the responsibility of the last channel to
 * "hang-up"
 *
 *
 * Assumptions:
 * - ^Sync primitives are used in the case of a blocking receive call, which
 * occur when there are not elements to receive. When the buffer has non-zero
 * length, no-sync primitives are invoked
 */

#include <array>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <semaphore>
#include <unistd.h>
#include <utility>

static constexpr int DefaultSize = 256;

template <typename T, int Size> class SenderChannel;
template <typename T, int Size> class ReceiverChannel;

template <typename T, int Size = DefaultSize>
std::pair<ReceiverChannel<T, Size>, SenderChannel<T, Size>> create_channel() {
    ReceiverChannel<T, Size> rx{};
    SenderChannel<T, Size> tx{rx.internal};
    return std::make_pair(std::move(rx), std::move(tx));
}

namespace {
// TODO `inline` these

template <typename T, int Size> struct Data {
    bool open{true};
    /// SAFETY:
    /// - head and head-th buffer element is only ever mutated by the
    /// ReceiverChannel thread
    /// - tail and tail-th buffer element is only ever mutated by the
    /// SenderChannel thread
    size_t head{0};
    size_t tail{0};
    std::array<T, Size> buffer;

    std::counting_semaphore<std::numeric_limits<int>::max()> read{0};
    std::counting_semaphore<std::numeric_limits<int>::max()> write{Size};
};

} // namespace

template <typename T, int Size = DefaultSize> class ReceiverChannel {
  public:
    ReceiverChannel(ReceiverChannel &&other) noexcept
        : internal(other.internal) {
        other.internal = nullptr;
    }

    ReceiverChannel(const ReceiverChannel &) = delete;

    ~ReceiverChannel() { close(); }

    void close() noexcept {
        if (internal != nullptr)
            internal->open = false;
    }

    [[nodiscard]] bool is_open() const noexcept { return internal->open; }

    /// Non-Blocking
    [[nodiscard]] std::optional<T> try_receive() noexcept {
        if (!is_open() || !internal->read.try_acquire()) {
            return std::nullopt;
        }

        return std::optional<T>(std::move(getHead()));
    }

    /// Blocking
    [[nodiscard]] T receive() noexcept {
        internal->read.acquire();
        auto tmp = getHead();
        internal->write.release();
        return std::move(tmp);
    }

  private:
    explicit ReceiverChannel() : internal{std::make_shared<Data<T, Size>>()} {}

    T getHead() {
        auto new_head = (internal->head + 1) % internal->buffer.size();
        auto tmp{std::move(internal->buffer[internal->head])};
        internal->head = new_head;
        return std::move(tmp);
    }

    std::shared_ptr<Data<T, Size>> internal;

    friend SenderChannel<T, Size>;
    template <typename U, int V>
    friend std::pair<ReceiverChannel<U, V>, SenderChannel<U, V>>
    create_channel();
};

template <typename T, int Size = DefaultSize> class SenderChannel {
  public:
    explicit SenderChannel() = delete;

    ~SenderChannel() { close(); }

    SenderChannel(const SenderChannel<T, Size> &) = delete;
    SenderChannel(SenderChannel &&other) : internal(other.internal) {
        other.internal = nullptr;
    }

    bool is_open() const noexcept { return internal->open; }
    void close() noexcept {
        if (internal != nullptr)
            internal->open = false;
    }

    [[nodiscard]] bool try_send(T data) noexcept {
        if (!is_open() || !internal->write.try_acquire())
            return false;

        writeTail(std::move(data));
        internal->read.release();
        return true;
    }

    void send(T data) {
        internal->write.acquire();
        writeTail(std::move(data));
        internal->read.release();
    }

  private:
    SenderChannel(std::shared_ptr<Data<T, Size>> ptr) : internal(ptr) {}

    void writeTail(T data) {
        internal->buffer[internal->tail] = std::move(data);
        internal->tail = (internal->tail + 1) % internal->buffer.size();
    }

    std::shared_ptr<Data<T, Size>> internal;

    friend ReceiverChannel<T, Size>;
    template <typename U, int V>
    friend std::pair<ReceiverChannel<U, V>, SenderChannel<U, V>>
    create_channel();
};
