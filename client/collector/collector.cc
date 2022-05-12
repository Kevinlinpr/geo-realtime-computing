#include "collector_side_next.h"

int main(){
    while(true){
        net::io_context ioc;
        std::shared_ptr<Session> session_p;
        session_p = std::make_shared<Session>(ioc, Session::SessionType::collector);
        session_p->Connect("127.0.0.1", "8833");
        std::thread commit([&](){
            for(int i = 0; i < 5; ++i){
                std::string msg = std::to_string((i + 1)) + "/5: " + "msg";
                Delivery::Get().CommitPush(msg);

                std::string result = Delivery::Get().RecievePop();
                std::cout<<result<<std::endl;
            }
            Delivery::Get().Finish();
            std::string result = Delivery::Get().RecievePop();
            std::cout<<result<<std::endl;
        });

        ioc.run();
        commit.join();
    }
    
}
