#ifndef GEO_REALTIME_COMPUTING_CUDA_SIDE_NEXT_H_
#define GEO_REALTIME_COMPUTING_CUDA_SIDE_NEXT_H_

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/strand.hpp>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <queue>
#include <condition_variable>
#include <mutex>
#include <string>
#include <boost/asio.hpp>
#include <cstring>
#include <vector>

namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace websocket = beast::websocket; // from <boost/beast/websocket.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>

void
fail(beast::error_code ec, char const* what)
{
    std::cerr << what << ": " << ec.message() << "\n";
}

std::string FINISHED_FLAG {"[FINISHED]"};
class Delivery {
public:
    Delivery(const Delivery&) = delete;
    Delivery& operator=(const Delivery&) = delete;

    
    static Delivery& Get(){
        static Delivery d;
        return d;
    }

    void Finish(){
        CommitPush(FINISHED_FLAG);
    }

    void CommitPush(const std::string& msg) {
        {
            std::lock_guard<std::mutex> lock(commit_m_);
            commit_msg_.push(msg);
        }
        commit_con_.notify_one();
    }

    std::string CommitPop() {
        std::unique_lock<std::mutex> lock(commit_m_);
        commit_con_.wait(lock,[this]{return !commit_msg_.empty();});
        std::string msg = commit_msg_.front();
        commit_msg_.pop();
        return msg;
    }

    void RecievePush(const std::string& msg) {
        {
            std::lock_guard<std::mutex> lock(recieve_m_);
            recieve_msg_.push(msg);
        }
        recieve_con_.notify_one();
    }

    std::string RecievePop() {
        std::unique_lock<std::mutex> lock(recieve_m_);
        recieve_con_.wait(lock,[this]{return !recieve_msg_.empty();});
        std::string msg = recieve_msg_.front();
        recieve_msg_.pop();
        return msg;
    }

private:
    Delivery() = default;

    std::queue<std::string> commit_msg_;
    std::mutex commit_m_;
    std::condition_variable commit_con_;

    std::queue<std::string> recieve_msg_;
    std::mutex recieve_m_;
    std::condition_variable recieve_con_;
};

const std::vector<std::string> split(const std::string& str, const std::string& pattern) {
    std::vector<std::string> result;
    std::string::size_type begin, end;

    end = str.find(pattern);
    begin = 0;

    while (end != std::string::npos) {
        if (end - begin != 0) {
            result.push_back(str.substr(begin, end-begin)); 
        }    
        begin = end + pattern.size();
        end = str.find(pattern, begin);
    }

    if (begin != str.length()) {
        result.push_back(str.substr(begin));
    }
    return result;        
}

class Session : public std::enable_shared_from_this<Session> {
public:

    enum class SessionType {
        collector,
        cuda,
        unspecific,
    };

    static SessionType sessionType(const std::string& type_string) {
        if(type_string == "Geo_Collector") {
            return SessionType::collector;
        }else if(type_string == "Geo_Cuda"){
            return SessionType::cuda;
        }else {
            return SessionType::unspecific;
        }
    }

    std::string getSessionType() const {
        switch (type)
        {
        case SessionType::collector:
            return "Geo_Collector";
        case SessionType::cuda:
            return "Geo_Cuda";
        default:
            return "Geo_Unspecific";
        }
    }
    
    std::string getUserAgent() const {
        return std::string(BOOST_BEAST_VERSION_STRING) + " " + getSessionType();
    }
    
    explicit Session(net::io_context& ioc, SessionType stype)
                : resolver_(net::make_strand(ioc)),
                  ws(net::make_strand(ioc)),
                  type(stype) {}

    void Connect(char const* host, char const* port){
        run(host,port);
    }

    

    websocket::stream<beast::tcp_stream> ws;
    SessionType type;
private:

    void run(char const* host, char const* port){
        host_ = host;
        resolver_.async_resolve(
            host,
            port,
            beast::bind_front_handler(
                &Session::on_resolve,
                shared_from_this()));
    }

    void on_resolve(beast::error_code ec, tcp::resolver::results_type results){
        if(ec)
            return fail(ec, "on_resolve");
        // Set the timeout for the operation
        beast::get_lowest_layer(ws).expires_after(std::chrono::seconds(30));

        // Make the connection on the IP address we get from a lookup
        beast::get_lowest_layer(ws).async_connect(
            results,
            beast::bind_front_handler(
                &Session::on_connect,
                shared_from_this()));
    }

    void on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type ep){
        if(ec)
            return fail(ec, "on_connect");

        // Turn off the timeout on the tcp_stream, because
        // the websocket stream has its own timeout system.
        beast::get_lowest_layer(ws).expires_never();

        // Set suggested timeout settings for the websocket
        ws.set_option(
            websocket::stream_base::timeout::suggested(
                beast::role_type::client));

        // Set a decorator to change the User-Agent of the handshake
        ws.set_option(websocket::stream_base::decorator(
            [this](websocket::request_type& req)
            {
                req.set(http::field::user_agent, this->getUserAgent());
                req.insert("session-type", this->getSessionType());
            }));

        // Update the host_ string. This will provide the value of the
        // Host HTTP header during the WebSocket handshake.
        // See https://tools.ietf.org/html/rfc7230#section-5.4
        host_ += ':' + std::to_string(ep.port());

        // Perform the websocket handshake
        ws.async_handshake(host_, "/",
            beast::bind_front_handler(
                &Session::on_handshake,
                shared_from_this()));
    }

    void on_handshake(beast::error_code ec)
    {
        if(ec)
            return fail(ec, "on_handshake");

        wait_task();
    }

    void wait_task(){
        ws.async_read(
            buffer_,
            beast::bind_front_handler(
                &Session::wait_task_start,
                shared_from_this()));
    }

    void wait_task_start(beast::error_code ec, std::size_t bytes_transferred){
        std::string result(boost::asio::buffer_cast<const char*>(buffer_.data()),bytes_transferred - 1);
        buffer_.clear();
        if(result == "TASK_START"){
            std::cout<<"TASK_START"<<std::endl;
            ws.async_write(net::buffer("TASK_START_READY"),
                beast::bind_front_handler(
                    &Session::wait_task_id,
                    shared_from_this()));
        }
    }

    void wait_task_id(beast::error_code ec, std::size_t bytes_transferred){
        boost::ignore_unused(bytes_transferred);
        if(ec)
            return fail(ec, "wait_task_id");
        ws.async_read(
            buffer_,
            beast::bind_front_handler(
                &Session::download_task_info,
                shared_from_this()));
    }

    void download_task_info(beast::error_code ec, std::size_t bytes_transferred) {
        std::string task_id(boost::asio::buffer_cast<const char*>(buffer_.data()),bytes_transferred);
        buffer_.clear();
        // 在这里下载任务
        // 下载完成后返回
        bool successfully_get_task_info = true;
        std::cout<<"Already download Task info"<<std::endl;
        if(successfully_get_task_info){
            ws.async_write(net::buffer("TASK_INFO_READY"),
                beast::bind_front_handler(
                    &Session::on_wait_realtime_data,
                    shared_from_this()));
        }else{
            ws.async_write(net::buffer("TASK_INFO_SOMETHING_WRONG"),
                beast::bind_front_handler(
                    &Session::task_info_something_wrong,
                    shared_from_this()));
        }
    }

    void task_info_something_wrong(beast::error_code ec, std::size_t bytes_transferred){
        boost::ignore_unused(bytes_transferred);
        buffer_.clear();
        wait_task();
    }

    void on_wait_realtime_data(beast::error_code ec, std::size_t bytes_transferred){
        boost::ignore_unused(bytes_transferred);
        if(ec)
            return fail(ec, "on_wait_realtime_data");
        ws.async_read(
            buffer_,
            beast::bind_front_handler(
                &Session::realtime_data_recieve,
                shared_from_this()));
    }

    void realtime_data_recieve(beast::error_code ec, std::size_t bytes_transferred){
        std::string payload(boost::asio::buffer_cast<const char*>(buffer_.data()),bytes_transferred);
        buffer_.clear();
        // 0/100: 123
        if(payload == "[FINISHED]") {
            ws.async_write(net::buffer(payload),
                           beast::bind_front_handler(
                                   &Session::realtime_data_recieve_done,
                                   shared_from_this()));
        }else{
            std::cout<<"realtime data recieve :" << payload<<std::endl;
            std::vector<std::string> ret = split(payload, ": ");
            std::string process = ret[0];
            std::string data = ret[1];
            std::vector<std::string> prs = split(process, "/");
            int current_prs = std::atoi(prs[0].c_str());
            int total_prs = std::atoi(prs[1].c_str());
            Delivery::Get().RecievePush(payload);
            
            std::string computing_result = Delivery::Get().CommitPop();
            ws.async_write(net::buffer(computing_result),
                        beast::bind_front_handler(
                            &Session::on_wait_realtime_data,
                            shared_from_this()));
        }
        
    }

    void realtime_data_recieve_something_wrong(beast::error_code ec, std::size_t bytes_transferred){
        boost::ignore_unused(bytes_transferred);
        buffer_.clear();
        ws.async_close(websocket::close_code::normal,
                       std::bind(
                               &Session::on_close,
                               shared_from_this(),
                               std::placeholders::_1));
    }

    void realtime_data_recieve_done(beast::error_code ec, std::size_t bytes_transferred){
        boost::ignore_unused(bytes_transferred);
        buffer_.clear();
        ws.async_read(
                buffer_,
                beast::bind_front_handler(
                        &Session::on_collector_close,
                        shared_from_this()));
    }

    void on_collector_close(beast::error_code ec, std::size_t bytes_transferred){
        std::string payload(boost::asio::buffer_cast<const char*>(buffer_.data()),bytes_transferred);
        buffer_.clear();
        if(payload == "[COLLECTOR_CLOSE]") {
            std::cout<<"[COLLECTOR_CLOSE]"<<std::endl;
            wait_task();
        }
    }

    void on_close(boost::system::error_code ec)
    {
        if(ec)
            return fail(ec, "close");
        // If we get here then the connection is closed gracefully

        // The buffers() function helps print a ConstBufferSequence
        std::string msg(boost::asio::buffer_cast<const char*>(buffer_.data()),buffer_.size());
        buffer_.clear();
        std::cout << "COMPUTING_CLOSE" << std::endl;
    }

    // 没有使用
    void on_collector_status(beast::error_code ec, std::size_t bytes_transferred){
        std::string result(boost::asio::buffer_cast<const char*>(buffer_.data()),bytes_transferred);
        buffer_.clear();
        if(result == "COLLECTOR_IS_ONLINE"){
            // collector 在线
            std::cout<<"COLLECTOR is Online"<<std::endl;
        }else if(result == "COLLECTOR_IS_OFFLINE"){
            // collector 不在线
            std::cout<<"COLLECTOR is Offline"<<std::endl;
        }

    }

    tcp::resolver resolver_;
    beast::flat_buffer buffer_;
    std::string host_;
};



#endif // GEO_REALTIME_COMPUTING_CUDA_SIDE_NEXT_H_