#ifndef GEO_REALTIME_COMPUTING_SCHEDULOR_H_
#define GEO_REALTIME_COMPUTING_SCHEDULOR_H_


#include <memory>
#include <queue>
#include <condition_variable>
class CudaSession;
class CollectorSession;

class Schedulor {
public:
    Schedulor(const Schedulor&) = delete;
    Schedulor& operator=(const Schedulor&) = delete;

    static Schedulor& Get() {
        static Schedulor schedulor;
        return schedulor;
    }

    void SetCuda(std::shared_ptr<CudaSession> cuda) {
        this->cuda = cuda;
    }

    bool CudaIsOnline() const {
        return cuda != nullptr;
    }

    void SetCollector(std::shared_ptr<CollectorSession> collector) {
        this->collector = collector;
    }

    bool CollectorIsOnline() const {
        return collector != nullptr;
    }

    std::shared_ptr<CudaSession> cuda {nullptr};
    std::shared_ptr<CollectorSession> collector {nullptr};

    // cuda 发送队列、接受队列
    std::queue<std::string> cuda_send{};
    std::queue<std::string> cuda_recieve{};
    std::mutex cuda_send_m_{};
    std::mutex cuda_recieve_m_{};
    std::condition_variable cuda_send_con_{};
    std::condition_variable cuda_recieve_con_{};

    // cuda 发送队列
    void cuda_send_push(const std::string& msg) {
        {
            std::lock_guard<std::mutex> lock(cuda_send_m_);
            cuda_send.push(msg);
        }
        cuda_send_con_.notify_one();
    }

    std::string cuda_send_pop() {
        std::unique_lock<std::mutex> lock(cuda_send_m_);
        cuda_send_con_.wait(lock,[this]{return !cuda_send.empty();});
        std::string msg = cuda_send.front();
        cuda_send.pop();
        return msg;
    }

    // cuda 接受队列
    void cuda_recieve_push(const std::string& msg) {
        {
            std::lock_guard<std::mutex> lock(cuda_recieve_m_);
            cuda_recieve.push(msg);
        }
        cuda_recieve_con_.notify_one();
    }

    std::string cuda_recieve_pop() {
        std::unique_lock<std::mutex> lock(cuda_recieve_m_);
        cuda_recieve_con_.wait(lock,[this]{return !cuda_recieve.empty();});
        std::string msg = cuda_recieve.front();
        cuda_recieve.pop();
        return msg;
    }

    

    // collector 发送队列、接受队列
    std::queue<std::string> collector_send{};
    std::queue<std::string> collector_recieve{};
    std::mutex collector_send_m_{};
    std::mutex collector_recieve_m_{};
    std::condition_variable collector_send_con_{};
    std::condition_variable collector_recieve_con_{};


    // collector 发送队列
    void collector_send_push(const std::string& msg) {
        {
            std::lock_guard<std::mutex> lock(collector_send_m_);
            collector_send.push(msg);
        }
        collector_send_con_.notify_one();
    }

    std::string collector_send_pop() {
        std::unique_lock<std::mutex> lock(collector_send_m_);
        collector_send_con_.wait(lock,[this]{return !collector_send.empty();});
        std::string msg = collector_send.front();
        collector_send.pop();
        return msg;
    }

    // collector 接受队列
    void collector_recieve_push(const std::string& msg) {
        {
            std::lock_guard<std::mutex> lock(collector_recieve_m_);
            collector_recieve.push(msg);
        }
        collector_recieve_con_.notify_one();
    }

    std::string collector_recieve_pop() {
        std::unique_lock<std::mutex> lock(collector_recieve_m_);
        collector_recieve_con_.wait(lock,[this]{return !collector_recieve.empty();});
        std::string msg = collector_recieve.front();
        collector_recieve.pop();
        return msg;
    }

    // cuda -> server -> collector
    

    // collector -> server -> cuda


private:
    Schedulor() = default;
};

#endif // GEO_REALTIME_COMPUTING_SCHEDULOR_H_