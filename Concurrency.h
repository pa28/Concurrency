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

    /**
     * @brief A class to provide a future operated process_queue where the input queue is processed asynchronously.
     * @tparam ResultType The type of data returned from the process function.
     * @tparam ArgType The type of data passed to the process function as an argument.
     */
    template<typename ResultType, typename ArgType>
    class process_future {
    public:
        using this_type = process_future<ResultType, ArgType>;
        using future_type = std::shared_future<ResultType>;
        using process_function_type = ResultType (*)(ArgType const &);

    private:
        bool process_run{false};                ///< While true the processor continues to run.
        bool process_pending{false};            ///< Used to notify the processor it has work.
        std::future<bool> run_future{};         ///< The future that manages the queue processing.

        std::mutex process_mutex{};             ///< Mutex to manage processing.
        std::condition_variable process_cv{};   ///< Condition variable to manage processing.

        std::mutex empty_input_mutex{};             ///< Mutex for empty_input_cv
        std::condition_variable empty_input_cv{};   ///< Contition variable to notify when the input queue is empty.

        std::queue<ArgType> input_queue{};      ///< The input queue
        std::mutex input_mutex{};               ///< The input queue mutex

        std::queue<ResultType> output_queue{};  ///< The output queue
        std::mutex output_mutex{};              ///< The output queue mutex

        size_t pool_size;   ///< The number of futures in the pool.
        std::vector<future_type> future_list;   ///< The future pool.

        std::queue<size_t> available_futures{}; ///< Indexes to available futures
        std::queue<size_t> finishing_futures{}; ///< Indexes to futures that are finishing

        std::mutex pool_mutex{};                ///< Mutex to manage access to stl containers.
        std::condition_variable pool_cv{};      ///< Condition variable to manage access to stl containers.

        std::mutex empty_pool_mutex{};
        std::condition_variable empty_pool_cv{};

        /**
         * @brief The method that manages queue processing
         * @param process_function the user supplied processing function
         * @return true
         */
        bool run(process_function_type process_function) {
            /**
             * The function called in the future.
             */
            auto future_function = [this](process_function_type process_function, ArgType const &arg,
                                          size_t idx) -> ResultType {
                // Call the user process function.
                auto result = process_function(arg);

                // Lock the process queueu
                std::unique_lock<std::mutex> lock(process_mutex);

                // Register the future as finishing
                finishing_futures.push(idx);

                // Notify other threads of the finishing thread
                process_cv.notify_all();
                return result;
            };

            while (process_run) {
                while (not input_empty() || not finishing_futures.empty()) {
                    if (not input_empty()) {
                        std::unique_lock<std::mutex> lock(input_mutex);
                        while (not input_queue.empty() && not available_futures.empty()) {
                            std::unique_lock<std::mutex> pool_lock(pool_mutex);
                            std::cout << "input\n";
                            auto idx = available_futures.front();
                            available_futures.pop();

                            auto arg = input_queue.front();
                            input_queue.pop();
                            future_list[idx] = std::async(std::launch::async, future_function, process_function, arg,
                                                          idx);
                        }

                        std::unique_lock<std::mutex> empty_lock(empty_input_mutex);
                        empty_input_cv.notify_all();
                    }

                    if (not finishing_futures.empty()) {
                        std::unique_lock<std::mutex> lock(input_mutex);
                        while (not finishing_futures.empty()) {
                            std::unique_lock<std::mutex> pool_lock(pool_mutex);
                            std::cout << "output\n";
                            auto idx = finishing_futures.front();
                            finishing_futures.pop();
                            output_queue.push(future_list[idx].get());
                            available_futures.push(idx);
                        }

                        std::unique_lock<std::mutex> empty_lock(empty_pool_mutex);
                        empty_pool_cv.notify_all();
                    }
                }

                std::unique_lock<std::mutex> lock(process_mutex);
                process_cv.wait(lock, [this]() -> bool { return process_pending; });
                if (process_pending) {
                    process_pending = false;
                }
            }
            return true;
        }

    public:
        /**
         * This class can not be default constructed.
         */
        process_future() = delete;

        /**
         * @brief Constructor
         * @param future_pool_size The number of futures in the pool.
         */
        explicit process_future(size_t future_pool_size)
                : pool_size{future_pool_size}, future_list{future_pool_size} {
            for (size_t idx = 0; idx < pool_size; ++idx)
                available_futures.push(idx);
        }

        /**
         * @brief Start processing
         * @param process_function the user supplied processing function.
         */
        void start(process_function_type process_function) {
            process_run = true;
            run_future = std::async(std::launch::async, &this_type::run, this, process_function);
        }

        /**
         * @brief Stop processing.
         * @return true.
         */
        bool stop() {
            if (process_run) {
                std::unique_lock<std::mutex> lock(process_mutex);
                process_run = false;
                process_pending = true;
                process_cv.notify_all();
            }
            return run_future.get();
        }

        /**
         * @brief Wait until the input queue is empty.
         */
        process_future& wait_input_empty() {
            std::unique_lock<std::mutex> empty_lock(empty_input_mutex);
            empty_input_cv.wait(empty_lock, [this]() -> bool { return input_queue.empty(); });
            return *this;
        }

        process_future& wait_pool_free() {
            std::unique_lock<std::mutex> empty_lock(empty_pool_mutex);
            empty_pool_cv.wait(empty_lock, [this]() -> bool { return finishing_futures.empty(); });
            return *this;
        }

        /**
         * @brief Push an argument onto the input queue.
         * @param arg the argument.
         */
        void input_push(ArgType const &arg) {
            std::unique_lock<std::mutex> lock(input_mutex);
            input_queue.push(arg);
            std::unique_lock<std::mutex> pool_lock(process_mutex);
            process_pending = true;
            process_cv.notify_all();
        }

        /**
         * @brief Determine if the input queue is empty.
         * @return true when empty.
         */
        bool input_empty() {
            std::unique_lock<std::mutex> lock(input_mutex);
            return input_queue.empty();
        }

        /**
         * @brief Determine if the output queue is empty.
         * @return true when empty.
         */
        bool output_empty() {
            std::unique_lock<std::mutex> lock(output_mutex);
            return output_queue.empty();
        }

        /**
         * @brief Pop and return the front of the input queue.
         * @return The argument.
         */
        ArgType input_pop_front() {
            std::unique_lock<std::mutex> lock(input_mutex);
            if (not input_queue.empty()) {
                ArgType arg = input_queue.front();
                input_queue.pop();
                return arg;
            }
            throw std::logic_error("input_pop_front() on empty intput queue.");
        }

        /**
         * @brief Pop and return the front of the output queue.
         * @return The result.
         */
        ResultType output_pop_front() {
            std::unique_lock<std::mutex> lock(output_mutex);
            if (not output_queue.empty()) {
                ResultType result = output_queue.front();
                output_queue.pop();
                return result;
            }
            throw std::logic_error("output_pop_front() on empyt output queue.");
        }
    };
} // namespace Concurrency