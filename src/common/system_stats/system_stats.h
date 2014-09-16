// Copyright (c) 2014, The Monero Project
// 
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without modification, are
// permitted provided that the following conditions are met:
// 
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
// 
// 2. Redistributions in binary form must reproduce the above copyright notice, this list
//    of conditions and the following disclaimer in the documentation and/or other
//    materials provided with the distribution.
// 
// 3. Neither the name of the copyright holder nor the names of its contributors may be
//    used to endorse or promote products derived from this software without specific
//    prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
// THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
// THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
// 
// Parts of this file are originally copyright (c) 2012-2013 The Cryptonote developers

#ifndef SYSTEM_STATS
#define SYSTEM_STATS

#include <exception>
#include <string>

namespace system_stats
{
  long long get_total_system_memory();
  long long get_used_system_memory();
  double get_cpu_usage();

  /* An exception class for integer overflows in CPU time holding variables */
  class cpu_time_integer_overflow_exception: public std::exception
  {
  public:
    virtual const char* what()
    {
      return "Unlikely CPU time integer overflow occured. Try again.";
    }
  };

  /* An exception class for file reading errors */
  class proc_file_error: public std::exception
  {
  public:
    virtual const char* what()
    {
      return "Couldn't read /proc/stat";
    }
  };

  /* An exception class for file reading errors */
  class win_cpu_usage_error: public std::exception
  {
  public:
    long error_code;
    std::string method_name;
    win_cpu_usage_error(std::string p_method_name, long p_error_code)
    {
      error_code = p_error_code;
      method_name = p_method_name;
    }
    virtual const char* what()
    {
      return "Error while reading CPU usage.";
    }
  };
};

#endif
