//
// server.cpp
// ~~~~~~~~~~
//
// Copyright (c) 2003-2013 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include "server.hpp"
#include <boost/bind.hpp>
#include <signal.h>

namespace HTTP {
namespace Server {

Server::Server(const std::string& address, const std::string& port,
  RequestHandler &request_handler)
  : io_service(),
    signals(io_service),
    acceptor(io_service),
    connection_manager(),
    new_connection(),
    request_handler(request_handler)
{
  // Register to handle the signals that indicate when the server should exit.
  // It is safe to register for the same signal multiple times in a program,
  // provided all registration for the specified signal is made through Asio.
  signals.add(SIGINT);
  signals.add(SIGTERM);
#if defined(SIGQUIT)
  signals.add(SIGQUIT);
#endif // defined(SIGQUIT)
  signals.async_wait(boost::bind(&Server::handle_stop, this));

  // Open the acceptor with the option to reuse the address (i.e. SO_REUSEADDR).
  boost::asio::ip::tcp::resolver resolver(io_service);
  boost::asio::ip::tcp::resolver::query query(address, port);
  boost::asio::ip::tcp::endpoint endpoint = *resolver.resolve(query);
  acceptor.open(endpoint.protocol());
  acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
  acceptor.bind(endpoint);
  acceptor.listen();

  start_accept();
}

void Server::run()
{
  // The io_service::run() call will block until all asynchronous operations
  // have finished. While the server is running, there is always at least one
  // asynchronous operation outstanding: the asynchronous accept call waiting
  // for new incoming connections.
  io_service.run();
}

void Server::start_accept()
{
  new_connection.reset(new Connection(io_service,
        connection_manager, request_handler));
  acceptor.async_accept(new_connection->get_socket(),
      boost::bind(&Server::handle_accept, this,
        boost::asio::placeholders::error));
}

void Server::handle_accept(const boost::system::error_code& e)
{
  // Check whether the server was stopped by a signal before this completion
  // handler had a chance to run.
  if (!acceptor.is_open())
  {
    return;
  }

  if (!e)
  {
    connection_manager.start(new_connection);
  }

  start_accept();
}

void Server::handle_stop()
{
  // The server is stopped by cancelling all outstanding asynchronous
  // operations. Once all operations have finished the io_service::run() call
  // will exit.
  acceptor.close();
  connection_manager.stop_all();
}

}
}

