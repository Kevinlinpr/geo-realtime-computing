#include "collector_side.h"

class Collector {
public:
    Collector(const Collector&) = delete;
    Collector& operator=(const Collector&) = delete;


    static Collector& Get(){
        static Collector collector;
        return collector;
    }

    void Commit(const std::string& msg) {
        session_p->Push(msg);
    }
    std::shared_ptr<session> session_p;
private:
    Collector() = default;
};


int main(int argc, char** argv)
{
    net::io_context ioc;
    std::shared_ptr<session> session_p;
    session_p = std::make_shared<session>(ioc, session::sessiontype::collector);
    session_p->run("127.0.0.1", "8080", "");

    Collector::Get().session_p = session_p;

    std::thread commit_mission([](){
        for(int i = 0;1<100;++i){
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            Collector::Get().Commit("Fuck you mother " + std::to_string(i));
        }
    });
    
    ioc.run();
    return EXIT_SUCCESS;
}
