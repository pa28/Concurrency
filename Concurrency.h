//
// Created by richard on 2019-05-09.
//

#pragma once

#include <queue>
#include <mutex>
#include <future>

/**
 * @namespace Concurrency
 * @brief Support for concurrency.
 */
namespace Concurrency {
    /**
     * @brief Use std::async to run a process function against an input queue producing an
     * output queue using multiple futures.
     * @tparam ResultType The type of data returned from the process function.
     * @tparam ArgType The type of data passed to the process function as an argument.
     */
    template<typename ResultType, typename ArgType>
    class process_queue {
    public:
        using future_type = std::shared_future<ResultType>;
        using process_function_type = ResultType (*)(ArgType const &);

    private:
        size_t pool_size;   ///< The number of futures in the pool.
        std::vector<future_type> future_list;   ///< The future pool.

        std::queue<size_t> available_futures{}; ///< Indexes to available futures
        std::queue<size_t> finishing_futures{}; ///< Indexes to futures that are finishing

        std::mutex pool_mutex{};                ///< Mutex to manage access to stl containers.
        std::condition_variable pool_cv{};      ///< Condition variable to manage access to stl containers.

    public:
        std::queue<ArgType> input_queue{};      ///< The input queue.
        std::queue<ResultType> output_queue{};  ///< The output queue.

        /**
         * @brief This class can not be default constructed.
         */
        process_queue() = delete;

        /**
         * @brief Constructor
         * @param future_pool_size The number of futures in the pool.
         */
        explicit process_queue(size_t future_pool_size)
                : pool_size{future_pool_size}, future_list{future_pool_size} {
            for (size_t idx = 0; idx < pool_size; ++idx)
                available_futures.push(idx);
        }

        /**
         * @brief Run the input queue through the process function.
         * @param process_function the user provided process function.
         */
        void run(process_function_type process_function) {
            /**
             * The function called in the future.
             */
            auto future_function = [this](process_function_type process_function, ArgType const &arg,
                                          size_t idx) -> ResultType {
                // Call the user process function.
                auto result = process_function(arg);

                // Lock the process queueu
                std::unique_lock<std::mutex> lock(pool_mutex);

                // Register the future as finishing
                finishing_futures.push(idx);

                // Notify other threads of the finishing thread
                pool_cv.notify_all();
                return result;
            };

            /**
             * If the finishing futures queue is not empty, at least one future is finsishing.
             */
            auto future_finishing = [this]() -> bool {
                return not finishing_futures.empty();
            };

            // Don't process empty input queues.
            if (input_queue.empty())
                return;

            // Start as many futures as we can.
            while (not input_queue.empty() && not available_futures.empty()) {
                auto idx = available_futures.front();
                auto arg = input_queue.front();
                future_list[idx] = std::async(std::launch::async, future_function, process_function, arg, idx);
                available_futures.pop();
                input_queue.pop();
            }

            // Monitor for finishing futures, harvest the result and start a new future
            // if there is data in the queue.
            while (not input_queue.empty()) {
                std::unique_lock<std::mutex> lock(pool_mutex);
                pool_cv.wait(lock, future_finishing);
                if (future_finishing()) {
                    auto idx = finishing_futures.front();
                    finishing_futures.pop();
                    output_queue.push(future_list[idx].get());

                    auto arg = input_queue.front();
                    input_queue.pop();
                    future_list[idx] = std::async(std::launch::async, future_function, process_function, arg, idx);
                }
            }

            // No more data in the queue, monitor for finishing futures and harvest the results.
            while (available_futures.size() < pool_size) {
                std::unique_lock<std::mutex> lock(pool_mutex);
                pool_cv.wait(lock, future_finishing);
                if (future_finishing()) {
                    auto idx = finishing_futures.front();
                    finishing_futures.pop();
                    output_queue.push(future_list[idx].get());
                    available_futures.push(idx);
                }
            }
        }

        void clear() {
            while (not available_futures.empty())
                available_futures.pop();

            while (not output_queue.empty())
                output_queue.pop();
        }
    };
} // namespace Concurrency