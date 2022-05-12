#ifndef GEO_REALTIME_COMPUTING_COLLECTOR_SIDE_NEXT_H_
#define GEO_REALTIME_COMPUTING_COLLECTOR_SIDE_NEXT_H_

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

    void SetTaskId(int id){
        task_id = id;
    }

    void Connect(char const* host, char const* port){
        run(host,port);
    }

    websocket::stream<beast::tcp_stream> ws;
    SessionType type;
    int task_id;
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

        // ws.async_write(net::buffer("CUDA_STATUS"),
        //         beast::bind_front_handler(
        //             &Session::on_handshake_reply,
        //             shared_from_this()));
        ws.async_write(net::buffer("TASK_START"),
                beast::bind_front_handler(
                    &Session::on_task_start_reply,
                    shared_from_this()));
    }

    void on_handshake_reply(beast::error_code ec, std::size_t bytes_transferred){
        boost::ignore_unused(bytes_transferred);
        if(ec)
            return fail(ec, "on_handshake_reply");
        ws.async_read(
            buffer_,
            beast::bind_front_handler(
                &Session::on_cuda_status,
                shared_from_this()));
    }

    void on_cuda_status(beast::error_code ec, std::size_t bytes_transferred){
        std::string result(boost::asio::buffer_cast<const char*>(buffer_.data()),bytes_transferred);
        buffer_.clear();
        if(result == "CUDA_IS_ONLINE"){
            // cuda 在线
            std::cout<<"CUDA is Online"<<std::endl;
            ws.async_write(net::buffer("TASK_START"),
                beast::bind_front_handler(
                    &Session::on_task_start_reply,
                    shared_from_this()));
        }else if(result == "CUDA_IS_OFFLINE"){
            // cuda 不在线
            std::cout<<"CUDA is Offline"<<std::endl;
        }

    }

    void on_task_start_reply(beast::error_code ec, std::size_t bytes_transferred){
        boost::ignore_unused(bytes_transferred);
        if(ec)
            return fail(ec, "on_handshake_reply");
        ws.async_read(
            buffer_,
            beast::bind_front_handler(
                &Session::on_if_task_start,
                shared_from_this()));
    }

    void on_if_task_start(beast::error_code ec, std::size_t bytes_transferred){
        std::string result(boost::asio::buffer_cast<const char*>(buffer_.data()),bytes_transferred > 0?(bytes_transferred - 1):bytes_transferred);
        buffer_.clear();
        if(result == "TASK_START_READY"){
            // 服务器已经做好接受task的准备
            std::cout<<"TASK_START_READY"<<std::endl;
            ws.async_write(net::buffer(std::to_string(task_id)),
                beast::bind_front_handler(
                    &Session::on_send_task_id_reply,
                    shared_from_this()));
        }else if(result == "TASK_START_SOMETHING_WRONG"){
            // 没有做好task准备
        }
    }

    void on_send_task_id_reply(beast::error_code ec, std::size_t bytes_transferred){
        boost::ignore_unused(bytes_transferred);
        if(ec)
            return fail(ec, "on_send_task_id_reply");
        ws.async_read(
            buffer_,
            beast::bind_front_handler(
                &Session::on_task_info_ready,
                shared_from_this()));
    }

    void on_task_info_ready(beast::error_code ec, std::size_t bytes_transferred) {
        std::string result(boost::asio::buffer_cast<const char*>(buffer_.data()),bytes_transferred - 1);
        buffer_.clear();
        if(result == "TASK_INFO_READY"){
            // cuda 已经下载好初始模型可以开始计算
            std::cout<<"TASK_INFO_READY"<<std::endl;
            // 阻塞操作
            on_send_data();
        }else if(result == "TASK_INFO_SOMETHING_WRONG"){
            // cuda 在下载模型的时候出现了问题
        }
    }

    void on_send_data(){
        std::string msg = Delivery::Get().CommitPop();
        ws.async_write(net::buffer(msg),
                       beast::bind_front_handler(
                               &Session::on_cuda_computing_result,
                               shared_from_this()));
    }

    void on_cuda_computing_result(beast::error_code ec, std::size_t bytes_transferred){
        boost::ignore_unused(bytes_transferred);
        if(ec)
            return fail(ec, "on_cuda_computing_result");
        ws.async_read(
                buffer_,
                beast::bind_front_handler(
                        &Session::on_get_cuda_computing_result,
                        shared_from_this()));
    }

    void on_get_cuda_computing_result(beast::error_code ec, std::size_t bytes_transferred){
        std::string computing_result(boost::asio::buffer_cast<const char*>(buffer_.data()),bytes_transferred);
        buffer_.clear();
        if(computing_result == "[FINISHED]"){
           Delivery::Get().RecievePush(computing_result);
            ws.async_close(websocket::close_code::normal,
                std::bind(
                &Session::on_close,
                shared_from_this(),
                std::placeholders::_1));
        }else{
            Delivery::Get().RecievePush(computing_result);
            on_send_data();
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
        std::cout << msg << std::endl;
    }

    tcp::resolver resolver_;
    beast::flat_buffer buffer_;
    std::string host_;
};





#endif // GEO_REALTIME_COMPUTING_COLLECTOR_SIDE_NEXT_H_