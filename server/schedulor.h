#ifndef GEO_REALTIME_COMPUTING_SCHEDULOR_H_
#define GEO_REALTIME_COMPUTING_SCHEDULOR_H_


#include <memory>

class session;

class Schedulor {
public:
    Schedulor(const Schedulor&) = delete;
    Schedulor& operator=(const Schedulor&) = delete;

    static Schedulor& Get() {
        static Schedulor schedulor;
        return schedulor;
    }

    void SetCuda(session* cuda) {
        this->cuda = cuda;
    }

    void SetCollector(session* collector) {
        this->collector = collector;
    }

    session* cuda;
    session* collector;

private:
    Schedulor() = default;
};

#endif // GEO_REALTIME_COMPUTING_SCHEDULOR_H_