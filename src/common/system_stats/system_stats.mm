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
 * \file system_stats.mm
 * \brief Contains system stats fetching utilities' implementations for Mac.
 *
 * Under the namespace `system_stats` it offers functions to fetch:
 * - Total system memory
 * - Used system memory
 * - CPU usage
 * - Battery stats
 */

#include <mach/mach_init.h>
#include <mach/mach_error.h>
#include <mach/mach_host.h>
#include <mach/vm_map.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <mach/vm_statistics.h>
#include <mach/mach_types.h>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <cinttypes>
#include <boost/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <atomic>
#import <Cocoa/Cocoa.h>
#import <IOKit/ps/IOPowerSources.h>
#import <IOKit/ps/IOPSKeys.h>

#include <iostream>

/*! \brief History size (in seconds) of the CPU usage cache buffer */
const int cpu_usage_buffer_size = 60;

namespace
{
  /* Gets a snapshot of the number of CPU ticks (idle and total) at this point (not from cache) */
  void get_cpu_snapshot_from_file(uint64_t &idle_ticks, uint64_t &total_ticks)
  {
    mach_port_t mach_port = mach_host_self();
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    host_cpu_load_info_data_t cpu_stats;

    int status = host_statistics(mach_port, HOST_CPU_LOAD_INFO, (host_info_t)&cpu_stats, &count);
    if (status != KERN_SUCCESS)
    {
      std::string msg = "Failure while getting CPU usage. `host_statistics` \
        failed with error code: " + std::to_string(status);
      throw std::runtime_error(msg);
    }
    for (int i = 0; i < CPU_STATE_MAX; i++)
    {
      total_ticks += cpu_stats.cpu_ticks[i];
    }
    idle_ticks = cpu_stats.cpu_ticks[CPU_STATE_IDLE];
  }

  /* Find CPU usage given two snapshots */
  double calculate_cpu_load(uint64_t idle_ticks_1, uint64_t total_ticks_1,
    uint64_t idle_ticks_2, uint64_t total_ticks_2)
  {
    long long total_ticks_diff = total_ticks_2 - total_ticks_1;
    long long idle_ticks_diff  = idle_ticks_2 -idle_ticks_1;
    if (total_ticks_diff < 0 ||idle_ticks_diff < 0)
    {
      // Overflow detected.
      throw std::runtime_error("Rare CPU time integer overflow occured. Try again.");
    }
    return 100 * (1.0 - (static_cast<double>(idle_ticks_diff) / total_ticks_diff));
  }

  /* Structure that represents a CPU snapshot */
  struct cpu_usage_snapshot {
    uint64_t idle_ticks;
    uint64_t total_ticks;
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
      get_cpu_snapshot_from_file(snapshot.idle_ticks, snapshot.total_ticks);
      cpu_usage_snapshots[cpu_usage_snapshots_head] = snapshot;
      cpu_usage_snapshots_head = (cpu_usage_snapshots_head + 1) % cpu_usage_buffer_size;
      cpu_usage_snapshots_mutex.unlock();
      // Wait for a second
      // CANNOT change this duration at the moment.
      usleep(1000000);
    }
  }

  /*! \brief Gets a snapshot of CPU times seconds_before seconds ago */
  void get_cpu_snapshot(uint64_t &idle_ticks, uint64_t &total_ticks, uint32_t seconds_before)
  {
    if (!cpu_usage_recording_started.load() || seconds_before > cpu_usage_buffer_size ||
      seconds_before > static_cast<uint64_t>(cpu_usage_snapshot_count))
    {
      usleep(seconds_before * 1000000);
      get_cpu_snapshot_from_file(idle_ticks, total_ticks);
    }
    else
    {
      cpu_usage_snapshots_mutex.lock();
      cpu_usage_snapshot snapshot = cpu_usage_snapshots[(cpu_usage_snapshots_head - seconds_before) %
        cpu_usage_buffer_size];
      cpu_usage_snapshots_mutex.unlock();
      idle_ticks = snapshot.idle_ticks;
      total_ticks = snapshot.total_ticks;
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
    int mib[] = {CTL_HW, HW_MEMSIZE};
    int64_t physical_memory;
    size_t length = sizeof(int64_t);
    sysctl(mib, 2, &physical_memory, &length, NULL, 0);
    return static_cast<uint64_t>(physical_memory);
  }

  /*! \brief Returns currently used system memory (used RAM) in bytes. */
  uint64_t get_used_system_memory()
  {
    vm_size_t page_size;
    mach_port_t mach_port;
    mach_msg_type_number_t count;
    vm_statistics_data_t vm_stats;

    mach_port = mach_host_self();
    count = sizeof(vm_stats) / sizeof(natural_t);
    int status = host_page_size(mach_port, &page_size);
    if (status != KERN_SUCCESS)
    {
      std::string msg = "Failure while getting used system memory. `host_page_size` \
        failed with error code: " + std::to_string(status);
      throw std::runtime_error(msg);
    }
    status = host_statistics(mach_port, HOST_VM_INFO, (host_info_t)&vm_stats, &count);
    if (status != KERN_SUCCESS)
    {
      std::string msg = "Failure while getting used system memory. `host_statistics` \
        failed with error code: " + std::to_string(status);
      throw std::runtime_error(msg);
    }
    uint64_t used_mem = (static_cast<uint64_t>(vm_stats.active_count) + 
      static_cast<uint64_t>(vm_stats.inactive_count) + 
      static_cast<uint64_t>(vm_stats.wire_count)) * page_size;
    return used_mem;
  }

  /*!
   * \brief Returns current CPU usage as a percentage.
   * \param  wait_duration Time between capturing two CPU snapshots
   * \return               CPU usage percentage
   */
  double get_cpu_usage(uint64_t wait_duration)
  {
    uint64_t total_ticks_1 = 0;
    uint64_t total_ticks_2 = 0;
    uint64_t idle_ticks_1 = 0;
    uint64_t idle_ticks_2 = 0;
    double percent;

    try
    {
      // Get two CPU snapshots separated by wait_duration.
      get_cpu_snapshot(idle_ticks_1, total_ticks_1, 0);
      get_cpu_snapshot(idle_ticks_2, total_ticks_2, wait_duration);
      std::cout << "1: " << idle_ticks_1 << " " << total_ticks_1 << std::endl;
      std::cout << "2: " << idle_ticks_2 << " " << total_ticks_2 << std::endl;
    }
    catch (std::runtime_error &e)
    {
      throw e;
    }
    try
    {
      // Calculate CPU usage based on the two snapshots.
      percent = calculate_cpu_load(idle_ticks_1, total_ticks_1, idle_ticks_2, total_ticks_2);
    }
    catch (std::runtime_error &e)
    {
      throw e;
    }
    return percent;
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
   * \brief Tells if battery is charging
   * \return True if battery is charging
   */
  bool is_battery_charging()
  {
    if (IOPSCopyExternalPowerAdapterDetails())
    {
      return true;
    }
    return false;
  }
};
