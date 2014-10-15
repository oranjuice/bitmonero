//
// connection_manager.hpp
// ~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2013 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef HTTP_CONNECTION_MANAGER_HPP
#define HTTP_CONNECTION_MANAGER_HPP

#include <set>
#include <boost/noncopyable.hpp>
#include "connection.hpp"

namespace HTTP {
namespace Server {

/// Manages open connections so that they may be cleanly stopped when the server
/// needs to shut down.
class ConnectionManager
  : private boost::noncopyable
{
public:
  /// Add the specified connection to the manager and start it.
  void start(ConnectionPtr c);

  /// Stop the specified connection.
  void stop(ConnectionPtr c);

  /// Stop all connections.
  void stop_all();

private:
  /// The managed connections.
  std::set<ConnectionPtr> connections;
};

}
}

#endif // HTTP_CONNECTION_MANAGER_HPP

