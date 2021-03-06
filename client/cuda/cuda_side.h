#ifndef GEO_REALTIME_COMPUTING_CUDA_SIDE_H_
#define GEO_REALTIME_COMPUTING_CUDA_SIDE_H_

// collector send to cuda
// protocol format :
// index | content
// example :
// 0 | 2022-5-10 10:10:00 | {...JSON inial Model ...} | task_id
// 1 | [12,32,12,4343,532,523,5,33,523,55...]
// 2 | [...]
// 3 | [...]

// cuda send to collector
// protocol format :
// index | context
// example :
// 0 | [...]

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/strand.hpp>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <thread>
#include <queue>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <boost/asio.hpp>

namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace websocket = beast::websocket; // from <boost/beast/websocket.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>


// Report a failure
void
fail(beast::error_code ec, char const* what)
{
    std::cerr << what << ": " << ec.message() << "\n";
}

// Sends a WebSocket message and prints the response
class session : public std::enable_shared_from_this<session>
{
    tcp::resolver resolver_;
    websocket::stream<beast::tcp_stream> ws_;
    beast::flat_buffer buffer_;
    std::string host_;

    std::mutex m_;
    std::condition_variable con_;
    std::queue<std::string> msgs_;

    std::mutex recieve_m_;
    std::condition_variable recieve_con_;
    std::queue<std::string> recieve_msg_;

public:
    void Push(const std::string& msg) {
        {
            std::lock_guard<std::mutex> lock(m_);
            msgs_.push(msg);
        }
        con_.notify_one();
    }

    std::string Pop() {
        std::unique_lock<std::mutex> lock(m_);
        con_.wait(lock,[this]{return !(msgs_.size() == 0);});
        std::string msg = msgs_.front();
        msgs_.pop();
        return msg;
    }

    void RecieveResultMsg(const std::string& msg){
        {
            std::lock_guard<std::mutex> lock(recieve_m_);
            recieve_msg_.push(msg);
        }
        recieve_con_.notify_one();
    }

    std::string ReadResultMsg(){
        std::unique_lock<std::mutex> lock(recieve_m_);
        recieve_con_.wait(lock,[this]{return !(recieve_msg_.size() == 0);});
        std::string msg = recieve_msg_.front();
        std::cout<<"ReadResultMsg : "<<msg<<std::endl;
        recieve_msg_.pop();
        return msg;
    }
    enum class sessiontype {
        collector,
        cuda,
        unspecific,
    };
    
    sessiontype type;

    std::string getSessionType() const {
        switch (type)
        {
        case sessiontype::collector:
            return "Geo_Collector";
        case sessiontype::cuda:
            return "Geo_Cuda";
        default:
            return "Geo_Unspecific";
        }
    }

    std::string getUserAgent() const {
        return std::string(BOOST_BEAST_VERSION_STRING) + " " + getSessionType();
    }
    
    // Resolver and socket require an io_context
    explicit
    session(net::io_context& ioc, sessiontype stype)
        : resolver_(net::make_strand(ioc))
        , ws_(net::make_strand(ioc))
        , type(stype)
        , msgs_()
        , recieve_msg_()
    {
    }

    // Start the asynchronous operation
    void
    run(
        char const* host,
        char const* port)
    {
        // Save these for later
        host_ = host;

        // Look up the domain name
        resolver_.async_resolve(
            host,
            port,
            beast::bind_front_handler(
                &session::on_resolve,
                shared_from_this()));
    }

    void
    on_resolve(
        beast::error_code ec,
        tcp::resolver::results_type results)
    {
        if(ec)
            return fail(ec, "resolve");

        // Set the timeout for the operation
        beast::get_lowest_layer(ws_).expires_after(std::chrono::seconds(30));

        // Make the connection on the IP address we get from a lookup
        beast::get_lowest_layer(ws_).async_connect(
            results,
            beast::bind_front_handler(
                &session::on_connect,
                shared_from_this()));
    }

    void
    on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type ep)
    {
        if(ec)
            return fail(ec, "connect");

        // Turn off the timeout on the tcp_stream, because
        // the websocket stream has its own timeout system.
        beast::get_lowest_layer(ws_).expires_never();

        // Set suggested timeout settings for the websocket
        ws_.set_option(
            websocket::stream_base::timeout::suggested(
                beast::role_type::client));

        // Set a decorator to change the User-Agent of the handshake
        ws_.set_option(websocket::stream_base::decorator(
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
        ws_.async_handshake(host_, "/",
            beast::bind_front_handler(
                &session::on_handshake,
                shared_from_this()));
    }

    void
    on_handshake(beast::error_code ec)
    {
        if(ec)
            return fail(ec, "handshake");

        ws_.async_read(
            buffer_,
            beast::bind_front_handler(
                &session::on_read,
                shared_from_this()));
        
    }

    void
    on_write(
        beast::error_code ec,
        std::size_t bytes_transferred)
    {
        boost::ignore_unused(bytes_transferred);

        if(ec)
            return fail(ec, "write");
        
        // Read a message into our buffer
        ws_.async_read(
            buffer_,
            beast::bind_front_handler(
                &session::on_read,
                shared_from_this()));
    }

    void
    on_finish(
        beast::error_code ec,
        std::size_t bytes_transferred){

            if(!ec){
                ws_.async_read(
                buffer_,
                beast::bind_front_handler(
                    &session::on_read,
                    shared_from_this()));
            }else{
                std::cout<<"on finished : "<<ec.what()<<std::endl;
            }
            
    }

    void
    on_read(
        beast::error_code ec,
        std::size_t bytes_transferred)
    {
        boost::ignore_unused(bytes_transferred);

        if(ec){
            fail(ec, "read");
            return;
        }else{
            std::cout<<"on read"<<std::endl;
            std::cout << beast::make_printable(buffer_.data()) << std::endl;
            std::string recieved_msg(boost::asio::buffer_cast<const char*>(buffer_.data()),buffer_.size());
            
            // std::this_thread::sleep_for(std::chrono::milliseconds(10));
            
                    
            // std::string s(boost::asio::buffer_cast<const char*>(buffer_.data()),buffer_.size());
            
            buffer_.clear();

            if(recieved_msg[0] == 'C'){
                std::cout<<"FUCK"<<std::endl;
                // ws_.async_read(
                //         buffer_,
                //         beast::bind_front_handler(
                //                 &session::on_read,
                //                 shared_from_this()));
            }else{
                RecieveResultMsg(recieved_msg);

                std::string computed_result = Pop();
                ws_.async_write(
                        net::buffer(computed_result),
                        beast::bind_front_handler(
                            &session::on_finish,
                            shared_from_this()));
            }
        }
    }

    void
    on_close(beast::error_code ec)
    {
        if(ec)
            return fail(ec, "close");

        // If we get here then the connection is closed gracefully

        // The make_printable() function helps print a ConstBufferSequence
        std::cout << beast::make_printable(buffer_.data()) << std::endl;
    }
};

class Cuda {
public:
    Cuda(const Cuda&) = delete;
    Cuda& operator=(const Cuda&) = delete;


    static Cuda& Get(){
        static Cuda cuda;
        return cuda;
    }

    void Commit(const std::string& msg) {
        session_p->Push(msg);
    }

    std::string Recieve() {
        return session_p->ReadResultMsg();
    }

    void FreeBlock() {
        session_p->RecieveResultMsg("COLLECTOR_CLOSED");
    }

    std::shared_ptr<session> session_p;
private:
    Cuda() = default;
};

#endif // GEO_REALTIME_COMPUTING_CUDA_SIDE_H_