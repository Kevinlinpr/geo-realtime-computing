#include "cuda_side_next.h"

int main(int argc, char** argv)
{
    std::cout<<"START"<<std::endl;
    net::io_context ioc;
    std::shared_ptr<Session> session_p;
    session_p = std::make_shared<Session>(ioc, Session::SessionType::cuda);
    session_p->Connect("127.0.0.1", "8833");

    std::thread computing([](){
        while(true){
            std::string data = Delivery::Get().RecievePop();
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            std::string result =  data + " from cuda";
            Delivery::Get().CommitPush(result);
        }
    });
    ioc.run();
    computing.join();
}
