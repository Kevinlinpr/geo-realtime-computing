#ifndef GEO_REALTIME_COMPUTING_SERVER_SIDE_NEXT_H_
#define GEO_REALTIME_COMPUTING_SERVER_SIDE_NEXT_H_

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
#include "schedulor.h"

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
class CudaSession : public std::enable_shared_from_this<CudaSession> {
public:

    explicit CudaSession(tcp::socket&& socket, 
                     boost::beast::http::request<boost::beast::http::string_body> req)
                : ws(std::move(socket)){
        ws.set_option(
                websocket::stream_base::timeout::suggested(
                    beast::role_type::server));

        // Set a decorator to change the Server of the handshake
        ws.set_option(websocket::stream_base::decorator(
                [](websocket::response_type& res)
                {
                    res.set(http::field::server,
                        std::string(BOOST_BEAST_VERSION_STRING) +
                            " websocket-server-async");
                }));

        ws.accept(req);

    }

    void Run(){
        net::dispatch(ws.get_executor(),
            beast::bind_front_handler(
                &CudaSession::WaitAndDoNothing,
                shared_from_this()));
    }

    void WaitAndDoNothing(){

    }

    void DoNothing(beast::error_code ec, std::size_t bytes_transferred){}

    websocket::stream<beast::tcp_stream> ws;

    void msg_from_collector(){
        std::string msg_from_collector = Schedulor::Get().cuda_send_pop();

        if(msg_from_collector == "[COLLECTOR_CLOSE]"){
            ws.async_write(
                    net::buffer(msg_from_collector),
                    beast::bind_front_handler(
                            &CudaSession::DoNothing,
                            shared_from_this()));
        }else{
            ws.async_write(
                    net::buffer(msg_from_collector),
                    beast::bind_front_handler(
                            &CudaSession::reply_from_cuda,
                            shared_from_this()));
        }

    }


private:

    void reply_from_cuda(beast::error_code ec, std::size_t bytes_transferred){
        ws.async_read(
                buffer_,
                beast::bind_front_handler(
                    &CudaSession::reply_to_collector,
                    shared_from_this()));
    }

    void reply_to_collector(beast::error_code ec, std::size_t bytes_transferred){
        std::string msg(boost::asio::buffer_cast<const char*>(buffer_.data()),buffer_.size());
        buffer_.clear();
        Schedulor::Get().collector_recieve_push(msg);
    }


    beast::flat_buffer buffer_;
};


class CollectorSession : public std::enable_shared_from_this<CollectorSession> {
public:
    
    explicit CollectorSession(tcp::socket&& socket,
                     boost::beast::http::request<boost::beast::http::string_body> req)
                : ws(std::move(socket)) {
        ws.set_option(
                websocket::stream_base::timeout::suggested(
                    beast::role_type::server));

        // Set a decorator to change the Server of the handshake
        ws.set_option(websocket::stream_base::decorator(
                [](websocket::response_type& res)
                {
                    res.set(http::field::server,
                        std::string(BOOST_BEAST_VERSION_STRING) +
                            " websocket-server-async");
                }));

        ws.accept(req);

    }

    void Run(){
        net::dispatch(ws.get_executor(),
            beast::bind_front_handler(
                &CollectorSession::route_msg_from_collector,
                shared_from_this()));
    }

    websocket::stream<beast::tcp_stream> ws;
private:

    void route_msg_from_collector(){
        ws.async_read(
                buffer_,
                beast::bind_front_handler(
                    &CollectorSession::msg_from_collector,
                    shared_from_this()));
    }

    void msg_from_collector(beast::error_code ec, std::size_t bytes_transferred){
        std::string msg(boost::asio::buffer_cast<const char*>(buffer_.data()),bytes_transferred);
        buffer_.clear();
        std::cout<<msg<<std::endl;

        if(msg == ""){
            Schedulor::Get().cuda_send_push("[COLLECTOR_CLOSE]");
            if(Schedulor::Get().CudaIsOnline()){
                Schedulor::Get().cuda->msg_from_collector();
            }
        }else{
            Schedulor::Get().cuda_send_push(msg);
            if(Schedulor::Get().CudaIsOnline()){
                Schedulor::Get().cuda->msg_from_collector();
            }
            std::string reply_from_cuda = Schedulor::Get().collector_recieve_pop();
            ws.async_write(
                    net::buffer(reply_from_cuda),
                    beast::bind_front_handler(
                            &CollectorSession::route_from_collector,
                            shared_from_this()));
        }



    }

    void route_from_collector(beast::error_code ec, std::size_t bytes_transferred){
        ws.async_read(
                        buffer_,
                        beast::bind_front_handler(
                            &CollectorSession::msg_from_collector,
                            shared_from_this()));
    }


    beast::flat_buffer buffer_;
};

class listener : public std::enable_shared_from_this<listener>
{
    net::io_context& ioc_;
    tcp::acceptor acceptor_;

public:
    listener(
        net::io_context& ioc,
        tcp::endpoint endpoint)
        : ioc_(ioc)
        , acceptor_(ioc)
    {
        beast::error_code ec;

        // Open the acceptor
        acceptor_.open(endpoint.protocol(), ec);
        if(ec)
        {
            fail(ec, "open");
            return;
        }

        // Allow address reuse
        acceptor_.set_option(net::socket_base::reuse_address(true), ec);
        if(ec)
        {
            fail(ec, "set_option");
            return;
        }

        // Bind to the server address
        acceptor_.bind(endpoint, ec);
        if(ec)
        {
            fail(ec, "bind");
            return;
        }

        // Start listening for connections
        acceptor_.listen(
            net::socket_base::max_listen_connections, ec);
        if(ec)
        {
            fail(ec, "listen");
            return;
        }

    }

    // Start accepting incoming connections
    void
    run()
    {
        do_accept();
    }

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


private:
    void
    do_accept()
    {
        // The new connection gets its own strand
        acceptor_.async_accept(
            net::make_strand(ioc_),
            beast::bind_front_handler(
                &listener::on_accept,
                shared_from_this()));
    }

    void
    on_accept(beast::error_code ec, tcp::socket socket)
    {
        if(ec)
        {
            fail(ec, "accept");
        }
        else
        {   
            // Buffer required for reading HTTP messages
            beast::flat_buffer buffer;

            // Read the HTTP request ourselves
            http::request<http::string_body> req;
            http::read(socket, buffer, req);
            if(websocket::is_upgrade(req)){
                // Create the session and run it
                if(sessionType(req.find("session-type")->value().to_string()) == SessionType::collector){
                    Schedulor::Get().SetCollector(std::make_shared<CollectorSession>(std::move(socket),req));
                    Schedulor::Get().collector->Run();
                }else if(sessionType(req.find("session-type")->value().to_string()) == SessionType::cuda){
                    Schedulor::Get().SetCuda(std::make_shared<CudaSession>(std::move(socket),req));
                    Schedulor::Get().cuda->Run();
                }
                
            }

        }

        // Accept another connection
        do_accept();
    }
};


#endif // GEO_REALTIME_COMPUTING_SERVER_SIDE_NEXT_H_