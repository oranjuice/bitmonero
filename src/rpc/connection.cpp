//
// connection.cpp
// ~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2013 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include "connection.hpp"
#include <vector>
#include <boost/bind.hpp>
#include "connection_manager.hpp"
#include "request_handler.hpp"

namespace HTTP {
namespace Server {

Connection::Connection(boost::asio::io_service& io_service,
    ConnectionManager& manager, RequestHandler& handler)
  : socket(io_service),
    connection_manager(manager),
    request_handler(handler)
{
}

boost::asio::ip::tcp::socket& Connection::get_socket()
{
  return socket;
}

void Connection::start()
{
  socket.async_read_some(boost::asio::buffer(buffer),
    boost::bind(&Connection::handle_read, shared_from_this(),
      boost::asio::placeholders::error,
      boost::asio::placeholders::bytes_transferred));
}

void Connection::stop()
{
  socket.close();
}

void Connection::handle_read(const boost::system::error_code& e,
    std::size_t bytes_transferred)
{
  if (!e)
  {
    boost::tribool result;
    boost::tie(result, boost::tuples::ignore) = request_parser.parse(
      request, buffer.data(), buffer.data() + bytes_transferred);

    if (result)
    {
      request_handler.handle_request(request, reply);
      boost::asio::async_write(socket, reply.to_buffers(),
        boost::bind(&Connection::handle_write, shared_from_this(),
          boost::asio::placeholders::error));
    }
    else if (!result)
    {
      reply = Reply::stock_reply(Reply::bad_request);
      boost::asio::async_write(socket, reply.to_buffers(),
        boost::bind(&Connection::handle_write, shared_from_this(),
          boost::asio::placeholders::error));
    }
    else
    {
      socket.async_read_some(boost::asio::buffer(buffer),
        boost::bind(&Connection::handle_read, shared_from_this(),
          boost::asio::placeholders::error,
          boost::asio::placeholders::bytes_transferred));
    }
  }
  else if (e != boost::asio::error::operation_aborted)
  {
    connection_manager.stop(shared_from_this());
  }
}

void Connection::handle_write(const boost::system::error_code& e)
{
  if (!e)
  {
    // Initiate graceful connection closure.
    boost::system::error_code ignored_ec;
    socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored_ec);
  }

  if (e != boost::asio::error::operation_aborted)
  {
    connection_manager.stop(shared_from_this());
  }
}

}
}

