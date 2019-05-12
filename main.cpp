#include <iostream>
#include <zconf.h>
#include "Concurrency.h"

u_int64_t f(u_int64_t const &x) {
    if (x <= 1)
        return 1;

    return x * f(x - 1);
}

int main() {
    std::cout << "Hello, World!" << std::endl;

#ifdef PROCESS_QUEUE
    Concurrency::process_queue<u_int64_t, u_int64_t> processQueue{8};
    
    for (u_int64_t x = 0; x < 60; ++x) {
        processQueue.input_queue.push(x);
    }
    
    processQueue.run(f);

    while (not processQueue.output_queue.empty()) {
        std::cout << processQueue.output_queue.front() << '\n';
        processQueue.output_queue.pop();
    }
#else
    Concurrency::process_future<u_int64_t, u_int64_t> processFuture{8};

    processFuture.start(f);

    for (u_int64_t x = 0; x < 60; ++x) {
        processFuture.input_push(x);
    }

    processFuture.wait_input_empty();
    processFuture.wait_pool_free();

    while (not processFuture.output_empty()) {
        std::cout << processFuture.output_pop_front() << '\n';
    }

    std::cout << processFuture.stop() << '\n';

#endif
    return 0;
}