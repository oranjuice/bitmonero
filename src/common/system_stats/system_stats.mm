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

#include "system_stats.h"
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
extern "C" {
#include "sigar.h"
#include "sigar_format.h"
}

#include <iostream>

/*! \brief History size (in seconds) of the CPU usage cache buffer */
const int cpu_usage_buffer_size = 60;

namespace
{
  sigar_t *sigar; // Instance of sigar used for getting CPU snapshots
  std::atomic<bool> cpu_usage_recording_started(false); // Whether buffering has started
  std::atomic<bool> cpu_usage_buffered(false); // Whether the entire buffer is full
  boost::mutex cpu_usage_snapshots_mutex;
  sigar_cpu_t cpu_usage_snapshots[cpu_usage_buffer_size]; // A circular queue
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
      sigar_cpu_t snapshot;
      if (sigar_cpu_get(sigar, &snapshot))
      {
        throw std::runtime_error("sigar_cpu_get failed. Stopped recording CPU usage.");
      }
      cpu_usage_snapshots[cpu_usage_snapshots_head] = snapshot;
      cpu_usage_snapshots_head = (cpu_usage_snapshots_head + 1) % cpu_usage_buffer_size;
      cpu_usage_snapshots_mutex.unlock();
      // Wait for a second
      // CANNOT change this duration at the moment.
      boost::this_thread::sleep(boost::posix_time::millisec(1000));
    }
  }

  /*! \brief Gets a snapshot of CPU times seconds_apart seconds apart */
  void get_cpu_snapshots(sigar_cpu_t *old, sigar_cpu_t *current, uint64_t seconds_apart)
  {
    if (!cpu_usage_recording_started.load() || seconds_apart > cpu_usage_buffer_size ||
      seconds_apart > static_cast<uint64_t>(cpu_usage_snapshot_count))
    {
      // Buffered content isn't enough yet.
      if (sigar_cpu_get(sigar, old))
      {
        throw std::runtime_error("sigar_cpu_get failed.");
      }
      boost::this_thread::sleep(boost::posix_time::millisec(seconds_apart * 1000));
      if (sigar_cpu_get(sigar, current))
      {
        throw std::runtime_error("sigar_cpu_get failed.");
      }
    }
    else
    {
      cpu_usage_snapshots_mutex.lock();
      *old = cpu_usage_snapshots[(cpu_usage_snapshots_head - 1 - seconds_apart) %
        cpu_usage_buffer_size];
      *current = cpu_usage_snapshots[(cpu_usage_snapshots_head - 1) % 
        cpu_usage_buffer_size];

      std::cout << old->user << " " << old->sys << " " << old->nice << " " << old->wait << std::endl;
      std::cout << current->user << " " << current->sys << " " << current->nice << " " << current->wait << std::endl;
      std::cout << "---\n";
      cpu_usage_snapshots_mutex.unlock();
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
    sigar_t *sigar;
    sigar_mem_t sigar_mem;

    if (sigar_open(&sigar))
    {
      throw std::runtime_error("sigar_open failed");
    }
    if (sigar_mem_get(sigar, &sigar_mem))
    {
      throw std::runtime_error("sigar_mem_get failed");
    }
    sigar_close(sigar);
    return sigar_mem.total;
  }

  /*! \brief Returns currently used system memory (used RAM) in bytes. */
  uint64_t get_used_system_memory()
  {
    sigar_t *sigar;
    sigar_mem_t sigar_mem;

    if (sigar_open(&sigar))
    {
      throw std::runtime_error("sigar_open failed");
    }
    if (sigar_mem_get(sigar, &sigar_mem))
    {
      throw std::runtime_error("sigar_mem_get failed");
    }
    sigar_close(sigar);
    return sigar_mem.actual_used;
  }

  /*! \brief Returns currently free system memory (free RAM) in bytes. */
  uint64_t get_free_system_memory()
  {
    sigar_t *sigar;
    sigar_mem_t sigar_mem;

    if (sigar_open(&sigar))
    {
      throw std::runtime_error("sigar_open failed");
    }
    if (sigar_mem_get(sigar, &sigar_mem))
    {
      throw std::runtime_error("sigar_mem_get failed");
    }
    sigar_close(sigar);
    return sigar_mem.actual_free;
  }

  /*! \brief Returns currently used system memory (used RAM) as percentage. */
  double get_used_percent_system_memory()
  {
    sigar_t *sigar;
    sigar_mem_t sigar_mem;

    if (sigar_open(&sigar))
    {
      throw std::runtime_error("sigar_open failed");
    }
    if (sigar_mem_get(sigar, &sigar_mem))
    {
      throw std::runtime_error("sigar_mem_get failed");
    }
    sigar_close(sigar);
    return sigar_mem.used_percent;
  }

  /*! \brief Returns currently free system memory (free RAM) as percentage. */
  double get_free_percent_system_memory()
  {
    sigar_t *sigar;
    sigar_mem_t sigar_mem;

    if (sigar_open(&sigar))
    {
      throw std::runtime_error("sigar_open failed");
    }
    if (sigar_mem_get(sigar, &sigar_mem))
    {
      throw std::runtime_error("sigar_mem_get failed");
    }
    sigar_close(sigar);
    return sigar_mem.free_percent;
  }

  /*! \brief Starts recording 60 second CPU usage history. */
  bool start_recording_cpu_usage()
  {
    if (cpu_usage_recording_started.load())
    {
      return false;
    }
    cpu_usage_recording_started.store(true);
    sigar_open(&sigar);
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
    sigar_close(sigar);
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
    sigar_cpu_t old, current;
    try
    {
      // Get two CPU usage snapshots wait_duration seconds apart
      get_cpu_snapshots(&old, &current, wait_duration);
    }
    catch (std::runtime_error &e)
    {
      throw e;
    }
    sigar_cpu_perc_t percentage;
    if (sigar_cpu_perc_calculate(&old, &current, &percentage))
    {
      throw std::runtime_error("sigar_cpu_perc_calculate failed");
    }
    std::cout << percentage.user << " " << percentage.sys << " " << percentage.nice << " " << percentage.wait << std::endl;
    return percentage.combined * 100;
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
