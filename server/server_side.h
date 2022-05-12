#ifndef GEO_REALTIME_COMPUTING_SERVER_SIDE_H_
#define GEO_REALTIME_COMPUTING_SERVER_SIDE_H_

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "schedulor.h"

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

class session : public std::enable_shared_from_this<session>{
private:
    beast::flat_buffer buffer_;

public:
    websocket::stream<beast::tcp_stream> ws_;
    enum class sessiontype {
        collector,
        cuda,
        unspecific,
    };
    
    sessiontype type;

    static sessiontype sessionType(const std::string& type_string) {
        if(type_string == "Geo_Collector") {
            return sessiontype::collector;
        }else if(type_string == "Geo_Cuda"){
            return sessiontype::cuda;
        }else {
            return sessiontype::unspecific;
        }
    }

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

    // Take ownership of the socket
    explicit session(tcp::socket&& socket, sessiontype stype , boost::beast::http::request<boost::beast::http::string_body> req) 
        : ws_(std::move(socket)),
          type(stype) {
            
        ws_.set_option(
                websocket::stream_base::timeout::suggested(
                    beast::role_type::server));

        // Set a decorator to change the Server of the handshake
        ws_.set_option(websocket::stream_base::decorator(
                [](websocket::response_type& res)
                {
                    res.set(http::field::server,
                        std::string(BOOST_BEAST_VERSION_STRING) +
                            " websocket-server-async");
                }));

        ws_.accept(req);

        if(type == session::sessiontype::cuda) {
            Schedulor::Get().SetCuda(this);
        }else if(type == session::sessiontype::collector) {
            if(Schedulor::Get().cuda == nullptr) {
                std::cout<<"cuda is null"<<std::endl;
            }
            
            Schedulor::Get().SetCollector(this);
        }

        std::cout<<getSessionType()<<std::endl;
    }

    // Get on the correct executor
    void run(){
        // We need to be executing within a strand to perform async operations
        // on the I/O objects in this session. Although not strictly necessary
        // for single-threaded contexts, this example code is written to be
        // thread-safe by default.
        net::dispatch(ws_.get_executor(),
            beast::bind_front_handler(
                &session::do_read,
                shared_from_this()));
    }

    void do_read(){
        // Read a message into our buffer
        ws_.async_read(
                buffer_,
                beast::bind_front_handler(
                    &session::on_read,
                    shared_from_this()));
    }

    void do_nothing(beast::error_code ec,
        std::size_t bytes_transferred){
        buffer_.clear();
        run();
    }

    void on_collector_disconnected(beast::error_code ec,
        std::size_t bytes_transferred){
        buffer_.clear();
    }

    void on_read( beast::error_code ec, std::size_t bytes_transferred) {

        boost::ignore_unused(bytes_transferred);

        // This indicates that the session was closed
        if(ec == websocket::error::closed){
            std::cout<<"websocket closed"<<std::endl;
            return;
        }
        
        if(ec){
            // if(Schedulor::Get().cuda){
            //     Schedulor::Get().cuda->ws_.text(Schedulor::Get().cuda->ws_.got_text());
            //     Schedulor::Get().cuda->ws_.async_write(
            //             net::buffer("COLLECTOR_CLOSED"),
            //             beast::bind_front_handler(
            //                     &session::do_nothing,
            //                     shared_from_this()));
            // }
            // if(Schedulor::Get().collector){
            //     Schedulor::Get().collector->ws_.text(Schedulor::Get().collector->ws_.got_text());
            //     Schedulor::Get().collector->ws_.async_write(
            //             net::buffer("CUDA_CLOSED"),
            //             beast::bind_front_handler(
            //                     &session::do_nothing,
            //                     shared_from_this()));
            // }
            Schedulor::Get().collector = nullptr;
            // Schedulor::Get().cuda = nullptr;
            fail(ec, "read");
            return;
        }else {
            if(type == sessiontype::collector){
                if(Schedulor::Get().cuda){
                    Schedulor::Get().cuda->ws_.text(Schedulor::Get().cuda->ws_.got_text());
                    Schedulor::Get().cuda->ws_.async_write(
                            buffer_.data(),
                            beast::bind_front_handler(
                                    &session::do_nothing,
                                    shared_from_this()));
                }
            }else if(type == sessiontype::cuda) {
                if(Schedulor::Get().collector){
                    Schedulor::Get().collector->ws_.text(Schedulor::Get().collector->ws_.got_text());
                    Schedulor::Get().collector->ws_.async_write(
                            buffer_.data(),
                            beast::bind_front_handler(
                                    &session::do_nothing,
                                    shared_from_this()));
                }
            }
        } 
    }

};
//------------------------------------------------------------------------------

// Accepts incoming connections and launches the sessions
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
                std::make_shared<session>(std::move(socket),
                        session::sessionType(req.find("session-type")->value().to_string()),
                        req)->run();
            }

        }

        // Accept another connection
        do_accept();
    }
};

#endif // GEO_REALTIME_COMPUTING_SERVER_SIDE_H_