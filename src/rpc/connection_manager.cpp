//
// connection_manager.cpp
// ~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2013 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include "connection_manager.hpp"
#include <algorithm>
#include <boost/bind.hpp>

namespace HTTP {
namespace Server {

void ConnectionManager::start(ConnectionPtr c)
{
  connections.insert(c);
  c->start();
}

void ConnectionManager::stop(ConnectionPtr c)
{
  connections.erase(c);
  c->stop();
}

void ConnectionManager::stop_all()
{
  std::for_each(connections.begin(), connections.end(),
      boost::bind(&Connection::stop, _1));
  connections.clear();
}

}
}

