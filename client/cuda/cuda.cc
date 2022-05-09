#include "cuda_side.h"

int main(int argc, char** argv)
{
    // Check command line arguments.
    if(argc != 4)
    {
        std::cerr <<
            "Usage: websocket-client-async <host> <port> <text>\n" <<
            "Example:\n" <<
            "    websocket-client-async echo.websocket.org 80 \"Hello, world!\"\n";
        return EXIT_FAILURE;
    }
    auto const host = argv[1];
    auto const port = argv[2];
    auto const text = argv[3];

    while(true){
        std::cout<<"Restart"<<std::endl;
        // The io_context is required for all I/O
        net::io_context ioc;

        // Launch the asynchronous operation
        std::make_shared<session>(ioc, session::sessiontype::cuda)->run(host, port, text);

        // Run the I/O service. The call will return when
        // the socket is closed.
        ioc.run();
    }

    return EXIT_SUCCESS;
}
