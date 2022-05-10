#include "collector_side.h"

int main(int argc, char** argv)
{
    net::io_context ioc;
    std::shared_ptr<session> session_p;
    session_p = std::make_shared<session>(ioc, session::sessiontype::collector);
    session_p->run("47.108.115.41", "8833", "");

    Collector::Get().session_p = session_p;

    std::thread commit_mission([](){
        for(int i = 0;1<100;++i){
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            Collector::Get().Commit("data from other computer " + std::to_string(i));
        }
    });

    std::thread recieve_mission([](){
        while (true)
        {
            std::string result = Collector::Get().Recieve();
            std::cout<<"Get New Result from remote : "<<result<<std::endl;
        }
    });

    
    
    ioc.run();

    commit_mission.join();
    recieve_mission.join();
    return EXIT_SUCCESS;
}
