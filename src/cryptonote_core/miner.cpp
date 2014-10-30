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

#include <sstream>
#include <numeric>
#include <boost/utility/value_init.hpp>
#include <boost/interprocess/detail/atomic.hpp>
#include <boost/limits.hpp>
#include <boost/foreach.hpp>
#include "misc_language.h"
#include "include_base_utils.h"
#include "cryptonote_basic_impl.h"
#include "cryptonote_format_utils.h"
#include "file_io_utils.h"
#include "common/command_line.h"
#include "string_coding.h"
#include "storages/portable_storage_template_helper.h"
#include <boost/date_time/posix_time/posix_time.hpp>
#include "common/system_stats/system_stats.h"

using namespace epee;

#include "miner.h"


extern "C" void slow_hash_allocate_state();
extern "C" void slow_hash_free_state();

// All in seconds
uint32_t cryptonote::miner::system_check_period = 5;
double cryptonote::miner::cpu_usage_threshold = 25;
uint32_t cryptonote::miner::cpu_usage_check_period = 60;
uint32_t cryptonote::miner::double_check_period = 10;

namespace cryptonote
{

  namespace
  {
    const command_line::arg_descriptor<std::string> arg_extra_messages =  {"extra-messages-file", "Specify file for extra messages to include into coinbase transactions", "", true};
    const command_line::arg_descriptor<std::string> arg_start_mining =    {"start-mining", "Specify wallet address to mining for", "", true};
    const command_line::arg_descriptor<uint32_t>      arg_mining_threads =  {"mining-threads", "Specify mining threads count", 0, true};
  }


  miner::miner(i_miner_handler* phandler):m_stop(1),
    m_template(boost::value_initialized<block>()),
    m_template_no(0),
    m_diffic(0),
    m_thread_index(0),
    m_phandler(phandler),
    m_height(0),
    m_pausers_count(0),
    m_threads_total(0),
    m_starter_nonce(0), 
    m_last_hr_merge_time(0),
    m_hashes(0),
    m_do_print_hashrate(false),
    m_do_mining(false),
    m_current_hash_rate(0)
  {

  }
  //-----------------------------------------------------------------------------------------------------
  miner::~miner()
  {
    stop();
  }
  //-----------------------------------------------------------------------------------------------------
  bool miner::set_block_template(const block& bl, const difficulty_type& di, uint64_t height)
  {
    CRITICAL_REGION_LOCAL(m_template_lock);
    m_template = bl;
    m_diffic = di;
    m_height = height;
    ++m_template_no;
    m_starter_nonce = crypto::rand<uint32_t>();
    return true;
  }
  //-----------------------------------------------------------------------------------------------------
  bool miner::on_block_chain_update()
  {
    if(!is_mining())
      return true;

    return request_block_template();
  }
  //-----------------------------------------------------------------------------------------------------
  bool miner::request_block_template()
  {
    block bl = AUTO_VAL_INIT(bl);
    difficulty_type di = AUTO_VAL_INIT(di);
    uint64_t height = AUTO_VAL_INIT(height);
    cryptonote::blobdata extra_nonce; 
    if(m_extra_messages.size() && m_config.current_extra_message_index < m_extra_messages.size())
    {
      extra_nonce = m_extra_messages[m_config.current_extra_message_index];
    }

    if(!m_phandler->get_block_template(bl, m_mine_address, di, height, extra_nonce))
    {
      LOG_ERROR("Failed to get_block_template(), stopping mining");
      return false;
    }
    set_block_template(bl, di, height);
    return true;
  }
  //-----------------------------------------------------------------------------------------------------
  bool miner::on_idle()
  {
    m_update_block_template_interval.do_call([&](){
      if(is_mining())request_block_template();
      return true;
    });

    m_update_merge_hr_interval.do_call([&](){
      merge_hr();
      return true;
    });
    
    return true;
  }
  //-----------------------------------------------------------------------------------------------------
  void miner::do_print_hashrate(bool do_hr)
  {
    m_do_print_hashrate = do_hr;
  }
  //-----------------------------------------------------------------------------------------------------
  void miner::merge_hr()
  {
    if(m_last_hr_merge_time && is_mining())
    {
      m_current_hash_rate = m_hashes * 1000 / ((misc_utils::get_tick_count() - m_last_hr_merge_time + 1));
      CRITICAL_REGION_LOCAL(m_last_hash_rates_lock);
      m_last_hash_rates.push_back(m_current_hash_rate);
      if(m_last_hash_rates.size() > 19)
        m_last_hash_rates.pop_front();
      if(m_do_print_hashrate)
      {
        uint64_t total_hr = std::accumulate(m_last_hash_rates.begin(), m_last_hash_rates.end(), 0);
        float hr = static_cast<float>(total_hr)/static_cast<float>(m_last_hash_rates.size());
        std::cout << "hashrate: " << std::setprecision(4) << std::fixed << hr << ENDL;
      }
    }
    m_last_hr_merge_time = misc_utils::get_tick_count();
    m_hashes = 0;
  }
  //-----------------------------------------------------------------------------------------------------
  void miner::init_options(boost::program_options::options_description& desc)
  {
    command_line::add_arg(desc, arg_extra_messages);
    command_line::add_arg(desc, arg_start_mining);
    command_line::add_arg(desc, arg_mining_threads);
  }
  //-----------------------------------------------------------------------------------------------------
  bool miner::init(const boost::program_options::variables_map& vm, bool testnet)
  {
    if(command_line::has_arg(vm, arg_extra_messages))
    {
      std::string buff;
      bool r = file_io_utils::load_file_to_string(command_line::get_arg(vm, arg_extra_messages), buff);
      CHECK_AND_ASSERT_MES(r, false, "Failed to load file with extra messages: " << command_line::get_arg(vm, arg_extra_messages));
      std::vector<std::string> extra_vec;
      boost::split(extra_vec, buff, boost::is_any_of("\n"), boost::token_compress_on );
      m_extra_messages.resize(extra_vec.size());
      for(size_t i = 0; i != extra_vec.size(); i++)
      {
        string_tools::trim(extra_vec[i]);
        if(!extra_vec[i].size())
          continue;
        std::string buff = string_encoding::base64_decode(extra_vec[i]);
        if(buff != "0")
          m_extra_messages[i] = buff;
      }
      m_config_folder_path = boost::filesystem::path(command_line::get_arg(vm, arg_extra_messages)).parent_path().string();
      m_config = AUTO_VAL_INIT(m_config);
      epee::serialization::load_t_from_json_file(m_config, m_config_folder_path + "/" + MINER_CONFIG_FILE_NAME);
      LOG_PRINT_L0("Loaded " << m_extra_messages.size() << " extra messages, current index " << m_config.current_extra_message_index);
    }

    if(command_line::has_arg(vm, arg_start_mining))
    {
      if(!cryptonote::get_account_address_from_str(m_mine_address, testnet, command_line::get_arg(vm, arg_start_mining)))
      {
        LOG_ERROR("Target account address " << command_line::get_arg(vm, arg_start_mining) << " has wrong format, starting daemon canceled");
        return false;
      }
      m_threads_total = 1;
      m_do_mining = true;
      if(command_line::has_arg(vm, arg_mining_threads))
      {
        m_threads_total = command_line::get_arg(vm, arg_mining_threads);
      }
    }

    return true;
  }
  //-----------------------------------------------------------------------------------------------------
  bool miner::is_mining() const
  {
    return !m_stop;
  }
  //-----------------------------------------------------------------------------------------------------
  const account_public_address& miner::get_mining_address() const
  {
    return m_mine_address;
  }
  //-----------------------------------------------------------------------------------------------------
  uint32_t miner::get_threads_count() const {
    return m_threads_total;
  }
  
  /*!
   * \brief Starts mining
   * \param  adr            Address to mine for
   * \param  threads_count  Number of threads
   * \param  cpu_saving     True if CPU usage aware
   * \param  battery_saving True if power supple aware
   * \return                True if successful
   */
  bool miner::start(const account_public_address& adr, size_t threads_count, bool cpu_saving,
    bool battery_saving)
  {
    if (is_mining() && !cpu_saving && !battery_saving)
    {
      LOG_ERROR("Mining already in progress");
      return false;
    }
    m_mine_address = adr;
    m_threads_total = static_cast<uint32_t>(threads_count);
    m_starter_nonce = crypto::rand<uint32_t>();
    CRITICAL_REGION_LOCAL(m_threads_lock);

    if (!m_threads.empty())
    {
      LOG_ERROR("Unable to start miner because there are active mining threads");
      return false;
    }

    if (!m_template_no)
      request_block_template(); // let's update block template

    boost::thread::attributes attrs;
    attrs.set_stack_size(THREAD_STACK_SIZE);
    if (cpu_saving || battery_saving)
    {
      // Smart mining is required, so start the controller thread which will in turn spawn
      // the worker threads
      m_is_cpu_saving = cpu_saving;
      m_is_battery_saving = battery_saving;
      m_smart_controller_thread = new boost::thread(attrs, boost::bind(&miner::smart_miner_thread, this));
      LOG_PRINT_L0("Smart mining has started with " << threads_count << " threads, good luck!" )
      return true;
    }
    boost::interprocess::ipcdetail::atomic_write32(&m_stop, 0);
    boost::interprocess::ipcdetail::atomic_write32(&m_thread_index, 0);

    for (size_t i = 0; i != threads_count; i++)
    {
      m_threads.push_back(boost::thread(attrs, boost::bind(&miner::worker_thread, this)));
    }

    LOG_PRINT_L0("Mining has started with " << threads_count << " threads, good luck!" )
    return true;
  }
  //-----------------------------------------------------------------------------------------------------
  uint64_t miner::get_speed() const
  {
    if(is_mining()) {
      return m_current_hash_rate;
    }
    else {
      return 0;
    }
  }
  //-----------------------------------------------------------------------------------------------------
  void miner::send_stop_signal()
  {
    boost::interprocess::ipcdetail::atomic_write32(&m_stop, 1);
  }
  //-----------------------------------------------------------------------------------------------------
  bool miner::stop()
  {
    send_stop_signal();
    CRITICAL_REGION_LOCAL(m_threads_lock);

    if (m_is_cpu_saving || m_is_battery_saving)
    {
      m_smart_controller_thread->join();
      delete m_smart_controller_thread;
    }
    BOOST_FOREACH(boost::thread& th, m_threads)
      th.join();

    m_threads.clear();
    m_is_cpu_saving = false;
    m_is_battery_saving = false;
    LOG_PRINT_L0("Mining has been stopped, " << m_threads.size() << " finished" );
    return true;
  }
  //-----------------------------------------------------------------------------------------------------
  bool miner::find_nonce_for_given_block(block& bl, const difficulty_type& diffic, uint64_t height)
  {
    for(; bl.nonce != std::numeric_limits<uint32_t>::max(); bl.nonce++)
    {
      crypto::hash h;
      get_block_longhash(bl, h, height);

      if(check_hash(h, diffic))
      {
        return true;
      }
    }
    return false;
  }
  //-----------------------------------------------------------------------------------------------------
  void miner::on_synchronized()
  {
    if(m_do_mining)
    {
      start(m_mine_address, m_threads_total);
    }
  }
  //-----------------------------------------------------------------------------------------------------
  void miner::pause()
  {
    CRITICAL_REGION_LOCAL(m_miners_count_lock);
    ++m_pausers_count;
    if(m_pausers_count == 1 && is_mining())
      LOG_PRINT_L2("MINING PAUSED");
  }
  //-----------------------------------------------------------------------------------------------------
  void miner::resume()
  {
    CRITICAL_REGION_LOCAL(m_miners_count_lock);
    --m_pausers_count;
    if(m_pausers_count < 0)
    {
      m_pausers_count = 0;
      LOG_PRINT_RED_L0("Unexpected miner::resume() called");
    }
    if(!m_pausers_count && is_mining())
      LOG_PRINT_L2("Mining resumed");
  }
  //-----------------------------------------------------------------------------------------------------
  bool miner::worker_thread()
  {
    uint32_t th_local_index = boost::interprocess::ipcdetail::atomic_inc32(&m_thread_index);
    LOG_PRINT_L0("Miner thread was started ["<< th_local_index << "]");
    log_space::log_singletone::set_thread_log_prefix(std::string("[miner ") + std::to_string(th_local_index) + "]");
    uint32_t nonce = m_starter_nonce + th_local_index;
    uint64_t height = 0;
    difficulty_type local_diff = 0;
    uint32_t local_template_ver = 0;
    block b;
	  slow_hash_allocate_state();
    while(!m_stop)
    {
      if(m_pausers_count)//anti split workaround
      {
        misc_utils::sleep_no_w(100);
        continue;
      }

      if(local_template_ver != m_template_no)
      {
        
        CRITICAL_REGION_BEGIN(m_template_lock);
        b = m_template;
        local_diff = m_diffic;
        height = m_height;
        CRITICAL_REGION_END();
        local_template_ver = m_template_no;
        nonce = m_starter_nonce + th_local_index;
      }

      if(!local_template_ver)//no any set_block_template call
      {
        LOG_PRINT_L2("Block template not set yet");
        epee::misc_utils::sleep_no_w(1000);
        continue;
      }

      b.nonce = nonce;
      crypto::hash h;
      get_block_longhash(b, h, height);

      if(check_hash(h, local_diff))
      {
        //we lucky!
        ++m_config.current_extra_message_index;
        LOG_PRINT_GREEN("Found block for difficulty: " << local_diff, LOG_LEVEL_0);
        if(!m_phandler->handle_block_found(b))
        {
          --m_config.current_extra_message_index;
        }else
        {
          //success update, lets update config
          epee::serialization::store_t_to_json_file(m_config, m_config_folder_path + "/" + MINER_CONFIG_FILE_NAME);
        }
      }
      nonce+=m_threads_total;
      ++m_hashes;
    }
	  slow_hash_free_state();
    LOG_PRINT_L0("Miner thread stopped ["<< th_local_index << "]");
    return true;
  }

  /*!
   * \brief Runs in the smart mining controller thread
   * \return True if everything went fine
   */
  bool miner::smart_miner_thread()
  {
    // Start the actual mining threads
    start(m_mine_address, m_threads_total);
    bool is_mining_paused = false;
    bool battery_trigger = false; // When a battery state change triggers a mining state change
    bool cpu_trigger = false; // When a CPU state change triggers a mining state change
    long cpu_trigger_timestamp = 0; // When the CPU trigger happened
    long battery_trigger_timestamp = 0; // When the battery trigger happened

    while (!m_stop)
    {
      if (!is_mining_paused)
      {
        // Trying to look for situations that will need to pause the mining
        if (cpu_trigger)
        {
          // CPU had shown signs of high usage.
          // Confirm before we decide to pause mining.
          long timestamp_now = boost::posix_time::time_duration(boost::posix_time::microsec_clock::
            local_time().time_of_day()).total_milliseconds();

          // Double check only after double_check_period seconds after the trigger
          if (timestamp_now - cpu_trigger_timestamp > miner::double_check_period * 1000)
          {
            double cpu_usage = system_stats::get_cpu_usage(miner::double_check_period);
            if (cpu_usage > miner::cpu_usage_threshold)
            {
              // CPU usage is still bad. Time to pause mining.
              cpu_trigger = false;
              battery_trigger = false;
              is_mining_paused = true;
              LOG_PRINT_L0("Pausing miner due to high CPU usage");
              pause();
            }
          }
        }
        else if (m_is_cpu_saving && system_stats::is_cpu_usage_buffered())
        {
          double cpu_usage = system_stats::get_cpu_usage(miner::cpu_usage_check_period);
          if (cpu_usage > miner::cpu_usage_threshold)
          {
            // High CPU usage over the past `cpu_usage_check_period` seconds. Must double-check after
            // double_check_period seconds before deciding to pause mining.
            cpu_trigger = true;
            cpu_trigger_timestamp = boost::posix_time::time_duration(boost::posix_time::microsec_clock::
              local_time().time_of_day()).total_milliseconds();
          }
        }
        if (battery_trigger)
        {
          // Battery wasn't charging a while ago.
          // Confirm before we decide to pause mining.
          long timestamp_now = boost::posix_time::time_duration(boost::posix_time::microsec_clock::
            local_time().time_of_day()).total_milliseconds();

          // Double check only after double_check_period seconds after the trigger
          if (timestamp_now - battery_trigger_timestamp > miner::double_check_period * 1000)
          {
            if (!system_stats::is_battery_charging())
            {
              // Battery is still not charging. Time to pause mining.
              battery_trigger = false;
              cpu_trigger = false;
              is_mining_paused = true;
              LOG_PRINT_L0("Pausing miner because battery is discharging");
              pause();
            }
          }
        }
        else if (m_is_battery_saving)
        {
          if (!system_stats::is_battery_charging())
          {
            // Battery isn't charging. Recheck after sometime before deciding to
            // pause mining
            battery_trigger = true;
            battery_trigger_timestamp = boost::posix_time::time_duration(boost::posix_time::microsec_clock::
              local_time().time_of_day()).total_milliseconds();
          }
        }
      }
      else
      {
        // Trying to look for situations that will need to resume the mining
        if (cpu_trigger && battery_trigger)
        {
          // Both CPU and battery had shown positive signs.
          // Double check before we decide to resume mining.
          long timestamp_now = boost::posix_time::time_duration(boost::posix_time::microsec_clock::
            local_time().time_of_day()).total_milliseconds();

          // Double check only after double_check_period seconds after the triggers
          if (timestamp_now - cpu_trigger_timestamp > miner::double_check_period * 1000)
          {
            double cpu_usage = system_stats::get_cpu_usage(miner::double_check_period);
            if (cpu_usage <= miner::cpu_usage_threshold && system_stats::is_battery_charging())
            {
              cpu_trigger = false;
              battery_trigger = false;
              is_mining_paused = false;
              LOG_PRINT_L0("Resuming miner");
              resume();
            }
          }
        }
        else
        {
          double cpu_usage = system_stats::get_cpu_usage(miner::cpu_usage_check_period);
          if (cpu_usage <= miner::cpu_usage_threshold  && system_stats::is_battery_charging())
          {
            // CPU usage is low and batter is charging.
            // Set a trigger and check again later before we decide to resume mining.
            cpu_trigger = true;
            battery_trigger = true;
            cpu_trigger_timestamp = battery_trigger_timestamp =
              boost::posix_time::time_duration(boost::posix_time::microsec_clock::
                local_time().time_of_day()).total_milliseconds();
          }
        }
      }
      // Repeat the system check probes regularly
      boost::this_thread::sleep(boost::posix_time::milliseconds(miner::system_check_period * 1000));
    }
    return true;
  }
}

