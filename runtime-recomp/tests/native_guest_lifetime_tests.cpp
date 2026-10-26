// Exercise the exact staged scheduler/threads, with no renderer/game window.
#include DKR_THREADS_SOURCE
#include DKR_QUEUE_SOURCE
#include DKR_SCHEDULING_SOURCE
#include <atomic>
#include <iostream>
#include <vector>

std::atomic_bool exited{false};
static std::atomic<unsigned> leases{0};
void run_thread_function(uint8_t* rdram, uint64_t, uint64_t, uint64_t) {
    struct Lease {
        Lease() {
            ++leases;
        }
        ~Lease() {
            --leases;
        }
    } lease;
    auto* context = TO_PTR(OSThread, ultramodern::this_thread())->context;
    wait_for_resumed(rdram, context);
    std::abort(); // A cancelled guest must unwind, not resume gameplay.
}
void ultramodern::wait_for_external_message(uint8_t*) {
    std::abort();
}
void ultramodern::wait_for_external_message_timed(uint8_t*, u32) {
    std::abort();
}
static void require(bool value) {
    if (!value)
        throw std::runtime_error("Native guest lifetime regression");
}
int main() {
    std::vector<uint8_t> memory(2 * 1024 * 1024);
    auto* rdram = memory.data();
    for (unsigned session = 0; session < 100; ++session) {
        exited = false;
        ultramodern::init_thread_cleanup();
        const auto a = static_cast<int32_t>(0x80001000U), b = static_cast<int32_t>(0x80002000U),
                   c = static_cast<int32_t>(0x80003000U);
        osCreateThread(rdram, a, 1, 0, 0, static_cast<int32_t>(0x80010000U), 30);
        osCreateThread(rdram, b, 2, 0, 0, static_cast<int32_t>(0x80020000U), 20);
        osCreateThread(rdram, c, 3, 0, 0, static_cast<int32_t>(0x80030000U), 10);
        ultramodern::thread_queue_insert(rdram, ultramodern::running_queue, b);
        ultramodern::thread_queue_insert(rdram, ultramodern::running_queue, c);
        require(ultramodern::thread_queue_remove(rdram, ultramodern::running_queue, c));
        require(!ultramodern::thread_queue_remove(rdram, ultramodern::running_queue, c));
        // osSetThreadPri reinserts using the removed thread's queue identity.
        // Removal must unlink its next node without erasing that identity.
        ultramodern::thread_queue_insert(rdram, ultramodern::running_queue, c);
        TO_PTR(OSThread, b)->state = OSThreadState::QUEUED;
        thread_self = a;
        osSetThreadPri(rdram, b, 5);
        thread_self = NULLPTR;
        require(ultramodern::thread_queue_peek(rdram, ultramodern::running_queue) == c);
        require(TO_PTR(OSThread, b)->queue == ultramodern::running_queue);
        require(ultramodern::thread_queue_remove(rdram, ultramodern::running_queue, c));
        require(ultramodern::thread_queue_peek(rdram, ultramodern::running_queue) == b);
        osStartThread(rdram, a);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!leases && std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        require(leases == 1);
        exited = true;
        ultramodern::cancel_native_guest_threads();
        ultramodern::join_thread_cleaner_thread();
        require(leases == 0 && native_guests.empty());
        require(TO_PTR(OSThread, a)->context == nullptr && TO_PTR(OSThread, b)->context == nullptr &&
                TO_PTR(OSThread, c)->context == nullptr);
        require(ultramodern::thread_queue_empty(rdram, ultramodern::running_queue));
        bool refused = false;
        try {
            resume_thread(TO_PTR(OSThread, b));
        } catch (const ultramodern::thread_terminated&) {
            refused = true;
        }
        require(refused); // Must refuse before reading a retired/null context.
    }
    std::cout << "100 retries retired running, paused and never-started guests and asset leases\n";
}
