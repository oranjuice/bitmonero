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

/*!
 * \file system_stats.cpp
 * \brief Contains system stats fetching utilities' implementations.
 *
 * Under the namespace `system_stats` it offers functions to fetch:
 * - Total system memory
 * - Used system memory
 * - CPU usage
 * - Battery stats
 * It works on Linux and Windows.
 */

#ifdef __linux__

#include "system_stats.h"
#include "sys/types.h"
#include "sys/sysinfo.h"
#include <cstdlib>
#include <cstdio>
#include <unistd.h>
#include <stdexcept>
#include <cinttypes>
#include <boost/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <atomic>

#include <iostream>

/*! \brief History size (in seconds) of the CPU usage cache buffer */
const int cpu_usage_buffer_size = 60;

namespace
{
  /*! \brief Gets a snapshot of CPU times till this point (not from the cache) */
  void get_cpu_snapshot_from_file(uint64_t &total_cpu_user, uint64_t &total_cpu_user_low,
    uint64_t &total_cpu_sys, uint64_t &total_cpu_idle)
  {
    // Get a snapshot of the how much CPU time has been spent for each type.
    FILE *file = fopen("/proc/stat", "r");
    if (!fscanf(file, "cpu %" SCNu64 "%" SCNu64 "%" SCNu64 "%" SCNu64, &total_cpu_user, &total_cpu_user_low,
      &total_cpu_sys, &total_cpu_idle))
    {
      throw std::runtime_error("Couldn't read /proc/stat");
    }
    fclose(file);
  }

  /*! \brief Find CPU usage given two snapshots */
  double calculate_cpu_load(uint64_t total_cpu_user_before, uint64_t total_cpu_user_low_before,
    uint64_t total_cpu_sys_before, uint64_t total_cpu_idle_before,
    uint64_t total_cpu_user_after, uint64_t total_cpu_user_low_after,
    uint64_t total_cpu_sys_after, uint64_t total_cpu_idle_after)
  {
    uint64_t total;
    double percent;
    if (total_cpu_user_after < total_cpu_user_before || total_cpu_user_low_after < total_cpu_user_low_before ||
      total_cpu_sys_after < total_cpu_sys_before || total_cpu_idle_after < total_cpu_idle_before)
    {
      // Overflow detected.
      throw std::runtime_error("Rare CPU time integer overflow occured. Try again.");
    }
    else
    {
      total = (total_cpu_user_after - total_cpu_user_before) + (total_cpu_user_low_after - total_cpu_user_low_before) +
        (total_cpu_sys_after - total_cpu_sys_before);
      percent = total;
      total += (total_cpu_idle_after - total_cpu_idle_before);
      percent /= total;
      percent *= 100;
    }
    return percent;
  }

  /* Structure that represents a CPU snapshot */
  struct cpu_usage_snapshot {
    uint64_t total_cpu_user;
    uint64_t total_cpu_user_low;
    uint64_t total_cpu_sys;
    uint64_t total_cpu_idle;
  };

  std::atomic<bool> cpu_usage_recording_started(false); // Whether buffering has started
  std::atomic<bool> cpu_usage_buffered(false); // Whether the entire buffer is full
  boost::mutex cpu_usage_snapshots_mutex;
  cpu_usage_snapshot cpu_usage_snapshots[cpu_usage_buffer_size]; // A circular queue
  int cpu_usage_snapshots_head = 0; // index of head of queue
  int cpu_usage_snapshot_count = 0; // Number of snapshots read so far
  boost::thread *cpu_usage_thread;

  /*! \brief Records CPU usage regularly */
  void record_cpu_usage()
  {
    while (cpu_usage_recording_started.load())
    {
      cpu_usage_snapshots_mutex.lock();
      if (cpu_usage_snapshot_count < cpu_usage_buffer_size) 
      {
        cpu_usage_snapshot_count++;
      }
      else
      {
        cpu_usage_buffered.store(true);
      }
      cpu_usage_snapshot snapshot;
      get_cpu_snapshot_from_file(snapshot.total_cpu_user, snapshot.total_cpu_user_low,
        snapshot.total_cpu_sys, snapshot.total_cpu_idle);
      cpu_usage_snapshots[cpu_usage_snapshots_head] = snapshot;
      cpu_usage_snapshots_head = (cpu_usage_snapshots_head + 1) % cpu_usage_buffer_size;
      cpu_usage_snapshots_mutex.unlock();
      // Wait for a second
      // CANNOT change this duration at the moment.
      usleep(1000000);
    }
  }

  /*! \brief Gets a snapshot of CPU times seconds_before seconds ago */
  void get_cpu_snapshot(uint64_t &total_cpu_user, uint64_t &total_cpu_user_low,
    uint64_t &total_cpu_sys, uint64_t &total_cpu_idle, uint32_t seconds_before)
  {
    if (!cpu_usage_recording_started.load() || seconds_before > cpu_usage_buffer_size ||
      seconds_before > static_cast<uint64_t>(cpu_usage_snapshot_count))
    {
      usleep(seconds_before * 1000000);
      get_cpu_snapshot_from_file(total_cpu_user, total_cpu_user_low,
        total_cpu_sys, total_cpu_idle);
    }
    else
    {
      cpu_usage_snapshots_mutex.lock();
      cpu_usage_snapshot snapshot = cpu_usage_snapshots[(cpu_usage_snapshots_head - seconds_before) %
        cpu_usage_buffer_size];
      cpu_usage_snapshots_mutex.unlock();
      total_cpu_user = snapshot.total_cpu_user;
      total_cpu_user_low = snapshot.total_cpu_user_low;
      total_cpu_sys = snapshot.total_cpu_sys;
      total_cpu_idle = snapshot.total_cpu_idle;
    }
  }
};

/*!
 * \namespace system_stats
 * \brief Namespace to hold system stats fetching functions.
 */
namespace system_stats
{
  /*! \brief Returns total system memory (RAM) in bytes. */
  uint64_t get_total_system_memory()
  {
    struct sysinfo mem_info;
    sysinfo(&mem_info);
    uint64_t total_mem = mem_info.totalram;
    total_mem *= mem_info.mem_unit;
    return total_mem;
  }

  /*! \brief Returns currently used system memory (used RAM) in bytes. */
  uint64_t get_used_system_memory()
  {
    struct sysinfo mem_info;
    sysinfo(&mem_info);
    uint64_t used_mem = mem_info.totalram - mem_info.freeram;
    used_mem *= mem_info.mem_unit;
    return used_mem;
  }

  /*! \brief Starts recording 60 second CPU usage history. */
  bool start_recording_cpu_usage()
  {
    if (cpu_usage_recording_started.load())
    {
      return false;
    }
    cpu_usage_recording_started.store(true);
    boost::thread::attributes attrs;
    cpu_usage_thread = new boost::thread(attrs, &record_cpu_usage);
    return true;
  }

  /*! \brief Stops recording 60 second CPU usage history. */
  bool stop_recording_cpu_usage()
  {
    if (!cpu_usage_recording_started.load())
    {
      return false;
    }
    cpu_usage_recording_started.store(false);
    cpu_usage_buffered.store(false);
    cpu_usage_snapshot_count = 0;
    cpu_usage_snapshots_head = 0;
    boost::thread::attributes attrs;
    cpu_usage_thread->join();
    delete cpu_usage_thread;
    return true;
  }

  /*!
   * \brief Tells if CPU usage is being recorded
   * \return True if being recorded, false otherwise
   */
  bool is_cpu_usage_recording()
  {
    return cpu_usage_recording_started.load();
  }

  /*!
   * \brief Tells if CPU usage has been completely buffered
   * \return True if completely buffered, false otherwise
   */
  bool is_cpu_usage_buffered()
  {
    return cpu_usage_buffered.load();
  }
  
  /*!
   * \brief Returns current CPU usage as a percentage.
   * \param  wait_duration Time between capturing two CPU snapshots in seconds
   * \return               CPU usage percentage
   */
  double get_cpu_usage(uint64_t wait_duration)
  {
    double percent;
    uint64_t total_cpu_user_before, total_cpu_user_low_before, total_cpu_sys_before,
      total_cpu_idle_before;
    uint64_t total_cpu_user_after, total_cpu_user_low_after, total_cpu_sys_after,
      total_cpu_idle_after;

    try
    {
      // Get a CPU usage snapshot
      get_cpu_snapshot(total_cpu_user_before, total_cpu_user_low_before, total_cpu_sys_before,
        total_cpu_idle_before, 0);

      // Get a CPU usage snapshot wait_duration seconds before
      get_cpu_snapshot(total_cpu_user_after, total_cpu_user_low_after, total_cpu_sys_after,
        total_cpu_idle_after, wait_duration);
    }
    catch (std::runtime_error &e)
    {
      throw e;
    }
    try
    {
      percent = calculate_cpu_load(total_cpu_user_before, total_cpu_user_low_before, total_cpu_sys_before,
        total_cpu_idle_before, total_cpu_user_after, total_cpu_user_low_after, total_cpu_sys_after,
        total_cpu_idle_after);
    }
    catch (std::runtime_error &e)
    {
      throw e;
    }
    return percent;
  }

  /*!
   * \brief Tells if battery is charging
   * \return True if battery is charging
   */
  bool is_battery_charging()
  {
    FILE *file = fopen("/sys/class/power_supply/BAT0/status", "r");
    char status[50];
    if (!fgets(status, 49, file))
    {
      throw std::runtime_error("Couldn't read /proc/stat");
    }
    fclose(file);
    if (strlen(status) > 0 && status[strlen(status) - 1] == '\n')
    {
      status[strlen(status) - 1] = '\0';
    }
    if (strcmp(status, "Charging") == 0)
    {
      return true;
    }
    return false;
  }
};

#elif _WIN32

#include "windows.h"
#include <pdh.h>
#include <pdhmsg.h>
#include <stdexcept>
#include <string>
#include <cinttypes>

CONST PWSTR COUNTER_PATH    = L"\\Processor(0)\\% Processor Time";

/*!
 * \namespace system_stats
 * \brief Namespace to hold system stats fetching functions.
 */
namespace system_stats
{
  /*! \brief Returns total system memory (RAM) in bytes. */
  uint64_t get_total_system_memory()
  {
    DWORDLONG w_total_mem = memInfo.ullTotalPhys;
    uint64_t total_mem = static_cast<uint64_t>(w_total_mem);
    return total_mem;
  }

  /*! \brief Returns currently used system memory (used RAM) in bytes. */
  uint64_t get_used_system_memory()
  {
    DWORDLONG w_used_mem = memInfo.ullTotalPhys - memInfo.ullAvailPhys;
    uint64_t used_mem = static_cast<uint64_t>(w_used_mem);
    return used_mem;
  }

  /*!
   * \brief Returns current CPU usage as a percentage.
   * \param  wait_duration Time between capturing two CPU snapshots
   * \return               CPU usage percentage
   */
  double get_cpu_usage(uint64_t wait_duration)
  {
    HQUERY h_query = NULL;
    HCOUNTER h_counter = NULL;
    DWORD dw_format = PDH_FMT_DOUBLE;
    PDH_STATUS status = PdhOpenQuery(NULL, NULL, &h_query);
    PDH_FMT_COUNTERVALUE item_buffer;
    if (status != ERROR_SUCCESS)
    {
      std::string msg = "Failure while getting CPU usage. `PdhOpenQuery` \
        failed with error code: " + std::to_string(status);
      throw std::runtime_error(msg);
    }
    status = PdhAddCounter(h_query, COUNTER_PATH, 0, &h_counter);
    if (status != ERROR_SUCCESS)
    {
      std::string msg = "Failure while getting CPU usage. `PdhAddCounter` \
        failed with error code: " + std::to_string(status);
      throw std::runtime_error(msg);
    }
    status = PdhCollectQueryData(h_query);
    if (status != ERROR_SUCCESS)
    {
      std::string msg = "Failure while getting CPU usage. `PdhCollectQueryData` \
        failed with error code: " + std::to_string(status);
      throw std::runtime_error(msg);
    }
    status = PdhGetFormattedCounterValue(h_counter, dw_format, (LPDWORD)NULL, &item_buffer);
    if (status != ERROR_SUCCESS)
    {
      std::string msg = "Failure while getting CPU usage. `PdhGetFormattedCounterValue` \
        failed with error code: " + std::to_string(status);
      throw std::runtime_error(msg);
    }
    return item_buffer.doubleValue;
  }
};

#endif
