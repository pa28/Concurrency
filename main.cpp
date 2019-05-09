#include <iostream>
#include "Concurrency.h"

u_int64_t f( u_int64_t const &x) {
    if (x <= 1)
        return 1;
    
    return x * f(x - 1);
}

int main() {
    std::cout << "Hello, World!" << std::endl;
    
    Concurrency::process_queue<u_int64_t, u_int64_t> processQueue{8};
    
    for (u_int64_t x = 0; x < 60; ++x) {
        processQueue.input_queue.push(x);
    }
    
    processQueue.run(f);

    while (not processQueue.output_queue.empty()) {
        std::cout << processQueue.output_queue.front() << '\n';
        processQueue.output_queue.pop();
    }

    return 0;
}