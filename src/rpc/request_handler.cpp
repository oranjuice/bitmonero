//
// request_handler.cpp
// ~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2013 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include "request_handler.hpp"
#include <fstream>
#include <sstream>
#include <string>
#include <boost/lexical_cast.hpp>
#include "reply.hpp"
#include "request.hpp"

namespace HTTP {
namespace Server {

RequestHandler::RequestHandler()
{
}

void RequestHandler::handle_request(const Request& req, Reply& rep)
{
  if (req.uri != "/json_rpc")
  {
    rep = Reply::stock_reply(Reply::not_found);
    return;
  }

  // Fill out the reply to be sent to the client.
  rep.status = Reply::ok;
  rep.headers.resize(2);
  rep.headers[0].name = "Content-Length";
  rep.headers[0].value = boost::lexical_cast<std::string>(rep.content.size());
  rep.headers[1].name = "Content-Type";
  rep.headers[1].value = "application/json";
}

}
}

