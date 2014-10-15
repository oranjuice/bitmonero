//
// connection.hpp
// ~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2013 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef HTTP_CONNECTION_HPP
#define HTTP_CONNECTION_HPP

#include <boost/asio.hpp>
#include <boost/array.hpp>
#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include "reply.hpp"
#include "request.hpp"
#include "request_handler.hpp"
#include "request_parser.hpp"

namespace HTTP {
namespace Server {

class ConnectionManager;

/// Represents a single connection from a client.
class Connection
  : public boost::enable_shared_from_this<Connection>,
    private boost::noncopyable
{
public:
  /// Construct a connection with the given io_service.
  explicit Connection(boost::asio::io_service& io_service,
      ConnectionManager& manager, RequestHandler& handler);

  /// Get the socket associated with the connection.
  boost::asio::ip::tcp::socket& get_socket();

  /// Start the first asynchronous operation for the connection.
  void start();

  /// Stop all asynchronous operations associated with the connection.
  void stop();

private:
  /// Handle completion of a read operation.
  void handle_read(const boost::system::error_code& e,
      std::size_t bytes_transferred);

  /// Handle completion of a write operation.
  void handle_write(const boost::system::error_code& e);

  /// Socket for the connection.
  boost::asio::ip::tcp::socket socket;

  /// The manager for this connection.
  ConnectionManager& connection_manager;

  /// The handler used to process the incoming request.
  RequestHandler& request_handler;

  /// Buffer for incoming data.
  boost::array<char, 8192> buffer;

  /// The incoming request.
  Request request;

  /// The parser for the incoming request.
  RequestParser request_parser;

  /// The reply to be sent back to the client.
  Reply reply;
};

typedef boost::shared_ptr<Connection> ConnectionPtr;

}
}

#endif // HTTP_CONNECTION_HPP

