/* ========================= eCAL LICENSE =================================
 *
 * Copyright (C) 2016 - 2024 Continental Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *      http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * ========================= eCAL LICENSE =================================
*/

/**
 * @brief  common eCAL data reader
**/

#include <ecal/ecal_config.h>
#include <ecal/ecal_log.h>
#include <ecal/ecal_process.h>

#if ECAL_CORE_REGISTRATION
#include "registration/ecal_registration_provider.h"
#endif

#include "ecal_reader.h"
#include "ecal_global_accessors.h"
#include "ecal_reader_layer.h"
#include "ecal_transport_layer.h"

#if ECAL_CORE_TRANSPORT_UDP
#include "udp/ecal_reader_udp.h"
#include "config/builder/udp_attribute_builder.h"
#endif

#if ECAL_CORE_TRANSPORT_SHM
#include "shm/ecal_reader_shm.h"
#include "config/builder/shm_attribute_builder.h"
#endif

#if ECAL_CORE_TRANSPORT_TCP
#include "tcp/ecal_reader_tcp.h"
#include "config/builder/tcp_attribute_builder.h"
#endif

#include <algorithm>
#include <chrono>
#include <iostream>
#include <iterator>
#include <list>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>

namespace eCAL
{
  ////////////////////////////////////////
  // CDataReader
  ////////////////////////////////////////
  CDataReader::CDataReader(const SDataTypeInformation& topic_info_, const eCAL::eCALReader::SAttributes& attr_) :
                 m_topic_info(topic_info_),
                 m_topic_size(0),
                 m_attributes(attr_),
                 m_receive_time(0),
                 m_clock(0),
                 m_frequency_calculator(3.0f),
                 m_created(false)
  {
#ifndef NDEBUG
    // log it
    Logging::Log(log_level_debug1, m_attributes.topic_name + "::CDataReader::Constructor");
#endif

    // build topic id
    std::stringstream counter;
    counter << std::chrono::steady_clock::now().time_since_epoch().count();
    m_topic_id = counter.str();

    // start transport layers
    InitializeLayers();
    StartTransportLayer();

    // mark as created
    m_created = true;
  }

  CDataReader::~CDataReader()
  {
#ifndef NDEBUG
    // log it
    Logging::Log(log_level_debug1, m_attributes.topic_name + "::CDataReader::Destructor");
#endif

    Stop();
  }

  bool CDataReader::Stop()
  {
    if (!m_created) return false;
#ifndef NDEBUG
    // log it
    Logging::Log(log_level_debug1, m_attributes.topic_name + "::CDataReader::Stop");
#endif

    // stop transport layers
    StopTransportLayer();

    // reset receive callback
    {
      const std::lock_guard<std::mutex> lock(m_receive_callback_mtx);
      m_receive_callback = nullptr;
    }

    // reset event callback map
    {
      const std::lock_guard<std::mutex> lock(m_event_callback_map_mtx);
      m_event_callback_map.clear();
    }

    // mark as no more created
    m_created = false;

    // and unregister
    Unregister();

    return true;
  }

  bool CDataReader::Read(std::string& buf_, long long* time_ /* = nullptr */, int rcv_timeout_ms_ /* = 0 */)
  {
    if (!m_created) return(false);

    std::unique_lock<std::mutex> read_buffer_lock(m_read_buf_mtx);

    // No need to wait (for whatever time) if something has been received
    if (!m_read_buf_received)
    {
      if (rcv_timeout_ms_ < 0)
      {
        m_read_buf_cv.wait(read_buffer_lock, [this]() { return this->m_read_buf_received; });
      }
      else if (rcv_timeout_ms_ > 0)
      {
        m_read_buf_cv.wait_for(read_buffer_lock, std::chrono::milliseconds(rcv_timeout_ms_), [this]() { return this->m_read_buf_received; });
      }
    }

    // did we receive new samples ?
    if (m_read_buf_received)
    {
#ifndef NDEBUG
      // log it
      Logging::Log(log_level_debug3, m_attributes.topic_name + "::CDataReader::Receive");
#endif
      // copy content to target string
      buf_.clear();
      buf_.swap(m_read_buf);
      m_read_buf_received = false;

      // apply time
      if (time_ != nullptr) *time_ = m_read_time;

      // return success
      return(true);
    }

    return(false);
  }

  bool CDataReader::AddReceiveCallback(ReceiveIDCallbackT callback_)
  {
    if (!m_created) return(false);

    // store receive callback
    {
      const std::lock_guard<std::mutex> lock(m_receive_callback_mtx);
#ifndef NDEBUG
      // log it
      Logging::Log(log_level_debug2, m_attributes.topic_name + "::CDataReader::AddReceiveCallback");
#endif
      m_receive_callback = std::move(callback_);
    }

    return(true);
  }

  bool CDataReader::RemReceiveCallback()
  {
    if (!m_created) return(false);

    // reset receive callback
    {
      const std::lock_guard<std::mutex> lock(m_receive_callback_mtx);
#ifndef NDEBUG
      // log it
      Logging::Log(log_level_debug2, m_attributes.topic_name + "::CDataReader::RemReceiveCallback");
#endif
      m_receive_callback = nullptr;
    }

    return(true);
  }

  bool CDataReader::AddEventCallback(eCAL_Subscriber_Event type_, SubEventCallbackT callback_)
  {
    if (!m_created) return(false);

    // store event callback
    {
#ifndef NDEBUG
      // log it
      Logging::Log(log_level_debug2, m_attributes.topic_name + "::CDataReader::AddEventCallback");
#endif
      const std::lock_guard<std::mutex> lock(m_event_callback_map_mtx);
      m_event_callback_map[type_] = std::move(callback_);
    }

    return(true);
  }

  bool CDataReader::RemEventCallback(eCAL_Subscriber_Event type_)
  {
    if (!m_created) return(false);

    // reset event callback
    {
#ifndef NDEBUG
      // log it
      Logging::Log(log_level_debug2, m_attributes.topic_name + "::CDataReader::RemEventCallback");
#endif
      const std::lock_guard<std::mutex> lock(m_event_callback_map_mtx);
      m_event_callback_map[type_] = nullptr;
    }

    return(true);
  }

  bool CDataReader::SetAttribute(const std::string& attr_name_, const std::string& attr_value_)
  {
    m_attr[attr_name_] = attr_value_;

#ifndef NDEBUG
    // log it
    Logging::Log(log_level_debug2, m_attributes.topic_name + "::CDataReader::SetAttribute");
#endif

    return(true);
  }

  bool CDataReader::ClearAttribute(const std::string& attr_name_)
  {
    m_attr.erase(attr_name_);

#ifndef NDEBUG
    // log it
    Logging::Log(log_level_debug2, m_attributes.topic_name + "::CDataReader::ClearAttribute");
#endif

    return(true);
  }

  void CDataReader::SetFilterIDs(const std::set<long long>& filter_ids_)
  {
    m_id_set = filter_ids_;
  }

  void CDataReader::ApplyPublication(const SPublicationInfo& publication_info_, const SDataTypeInformation& data_type_info_, const SLayerStates& pub_layer_states_)
  {
    // flag write enabled from publisher side (information not used yet)
#if ECAL_CORE_TRANSPORT_UDP
    m_layers.udp.write_enabled = pub_layer_states_.udp.write_enabled;
#endif
#if ECAL_CORE_TRANSPORT_SHM
    m_layers.shm.write_enabled = pub_layer_states_.shm.write_enabled;
#endif
#if ECAL_CORE_TRANSPORT_TCP
    m_layers.tcp.write_enabled = pub_layer_states_.tcp.write_enabled;
#endif

    // add key to connection map, including connection state
    bool is_new_connection     = false;
    bool is_updated_connection = false;
    {
      const std::lock_guard<std::mutex> lock(m_connection_map_mtx);
      auto publication_info_iter = m_connection_map.find(publication_info_);

      if (publication_info_iter == m_connection_map.end())
      {
        // add publisher to connection map, connection state false
        m_connection_map[publication_info_] = SConnection{ data_type_info_, pub_layer_states_, false };
      }
      else
      {
        // existing connection, we got the second update now
        auto& connection = publication_info_iter->second;

        // if this connection was inactive before
        // activate it now and flag a new connection finally
        if (!connection.state)
        {
          is_new_connection = true;
        }
        // the connection was active, so we just update it
        else
        {
          is_updated_connection = true;
        }

        // update the data type and layer states, even if the connection is not new
        connection = SConnection{ data_type_info_, pub_layer_states_, true };
      }

      // update connection count
      m_connection_count = GetConnectionCount();
    }

    // handle these events outside the lock
    if (is_new_connection)
    {
      // fire connect event
      FireConnectEvent(publication_info_.entity_id, data_type_info_);
    }
    else if (is_updated_connection)
    {
      // fire update event
      FireUpdateEvent(publication_info_.entity_id, data_type_info_);
    }

#ifndef NDEBUG
    // log it
    Logging::Log(log_level_debug3, m_attributes.topic_name + "::CDataReader::ApplyPublication");
#endif
  }

  void CDataReader::RemovePublication(const SPublicationInfo& publication_info_)
  {
    // remove key from connection map
    bool last_connection_gone(false);
    {
      const std::lock_guard<std::mutex> lock(m_connection_map_mtx);

      m_connection_map.erase(publication_info_);
      last_connection_gone = m_connection_map.empty();

      // update connection count
      m_connection_count = GetConnectionCount();
    }

    if (last_connection_gone)
    {
      // fire disconnect event
      FireDisconnectEvent();
    }

#ifndef NDEBUG
    // log it
    Logging::Log(log_level_debug3, m_attributes.topic_name + "::CDataReader::RemovePublication");
#endif
  }

  void CDataReader::ApplyLayerParameter(const SPublicationInfo& publication_info_, eTLayerType type_, const Registration::ConnectionPar& parameter_)
  {
    SReaderLayerPar par;
    par.host_name  = publication_info_.host_name;
    par.process_id = publication_info_.process_id;
    par.topic_name = m_attributes.topic_name;
    par.topic_id   = publication_info_.entity_id;
    par.parameter  = parameter_;

    switch (type_)
    {
    case tl_ecal_shm:
#if ECAL_CORE_TRANSPORT_SHM
      CSHMReaderLayer::Get()->SetConnectionParameter(par);
#endif
      break;
    case tl_ecal_tcp:
#if ECAL_CORE_TRANSPORT_TCP
      CTCPReaderLayer::Get()->SetConnectionParameter(par);
#endif
      break;
    default:
      break;
    }
  }

  void CDataReader::InitializeLayers()
  {
    // initialize udp layer
#if ECAL_CORE_TRANSPORT_UDP
    if (m_attributes.udp.enable)
    {
      CUDPReaderLayer::Get()->Initialize(eCAL::eCALReader::BuildUDPAttributes(m_attributes));
    }
#endif

    // initialize shm layer
#if ECAL_CORE_TRANSPORT_SHM
    if (m_attributes.shm.enable)
    {
      CSHMReaderLayer::Get()->Initialize(eCAL::eCALReader::BuildSHMAttributes(m_attributes));
    }
#endif

    // initialize tcp layer
#if ECAL_CORE_TRANSPORT_TCP
    if (m_attributes.tcp.enable)
    {
      CTCPReaderLayer::Get()->Initialize(eCAL::eCALReader::BuildTCPLayerAttributes(m_attributes));
    }
#endif
  }

  size_t CDataReader::ApplySample(const Payload::TopicInfo& topic_info_, const char* payload_, size_t size_, long long id_, long long clock_, long long time_, size_t hash_, eTLayerType layer_)
  {
    // ensure thread safety
    const std::lock_guard<std::mutex> lock(m_receive_callback_mtx);
    if (!m_created) return(0);

    // check receive layer configuration
    switch (layer_)
    {
    case tl_ecal_udp:
      if (!m_attributes.udp.enable) return 0;
      break;
    case tl_ecal_shm:
      if (!m_attributes.shm.enable) return 0;
      break;
    case tl_ecal_tcp:
      if (!m_attributes.tcp.enable) return 0;
      break;
    default:
      break;
    }

    // store receive layer
    m_layers.udp.active |= layer_ == tl_ecal_udp;
    m_layers.shm.active |= layer_ == tl_ecal_shm;
    m_layers.tcp.active |= layer_ == tl_ecal_tcp;

    // number of hash values to track for duplicates
    constexpr int hash_queue_size(64);

    // use hash to discard multiple receives of the same payload
    //   if a hash is in the queue we received this message recently (on another transport layer ?)
    //   so we return and do not process this sample again
    if(std::find(m_sample_hash_queue.begin(), m_sample_hash_queue.end(), hash_) != m_sample_hash_queue.end())
    {
#ifndef NDEBUG
      // log it
      Logging::Log(log_level_debug3, m_attributes.topic_name + "::CDataReader::AddSample discard sample because of multiple receive");
#endif
      return(size_);
    }
    //   this is a new sample -> store its hash
    m_sample_hash_queue.push_back(hash_);

    // limit size of hash queue to the last 64 messages
    while (m_sample_hash_queue.size() > hash_queue_size) m_sample_hash_queue.pop_front();

    // check id
    if (!m_id_set.empty())
    {
      if (m_id_set.find(id_) == m_id_set.end()) return(0);
    }

    // check the current message clock
    // if the function returns false we detected
    //  - a dropped message
    //  - an out-of-order message
    //  - a multiple sent message
    if (!CheckMessageClock(topic_info_.tid, clock_))
    {
      // we will not process that message
      return(0);
    }

#ifndef NDEBUG
    // log it
    Logging::Log(log_level_debug3, m_attributes.topic_name + "::CDataReader::AddSample");
#endif

    // increase read clock
    m_clock++;

    // Update frequency calculation
    {
      const auto receive_time = std::chrono::steady_clock::now();
      const std::lock_guard<std::mutex> freq_lock(m_frequency_calculator_mtx);
      m_frequency_calculator.addTick(receive_time);
    }

    // reset timeout
    m_receive_time = 0;

    // store size
    m_topic_size = size_;

    // execute callback
    bool processed = false;
    {
      // call user receive callback function
      if(m_receive_callback)
      {
#ifndef NDEBUG
        // log it
        Logging::Log(log_level_debug3, m_attributes.topic_name + "::CDataReader::AddSample::ReceiveCallback");
#endif
        // prepare data struct
        SReceiveCallbackData cb_data;
        cb_data.buf   = const_cast<char*>(payload_);
        cb_data.size  = long(size_);
        cb_data.id    = id_;
        cb_data.time  = time_;
        cb_data.clock = clock_;

        Registration::STopicId topic_id;
        topic_id.topic_name          = topic_info_.tname;
        topic_id.topic_id.host_name  = topic_info_.hname;
        topic_id.topic_id.entity_id  = topic_info_.tid;
        topic_id.topic_id.process_id = topic_info_.pid;

        SPublicationInfo pub_info;
        pub_info.entity_id  = topic_info_.tid;
        pub_info.host_name  = topic_info_.hname;
        pub_info.process_id = topic_info_.pid;

        // execute it
        std::lock_guard<std::mutex> lock(m_connection_map_mtx);
        (m_receive_callback)(topic_id, m_connection_map[pub_info].data_type_info, cb_data);
        processed = true;
      }
    }

    // if not consumed by user receive call
    if (!processed)
    {
      // push sample into read buffer
      const std::lock_guard<std::mutex> read_buffer_lock(m_read_buf_mtx);
      m_read_buf.clear();
      m_read_buf.assign(payload_, payload_ + size_);
      m_read_time = time_;
      m_read_buf_received = true;

      // inform receive
      m_read_buf_cv.notify_one();
#ifndef NDEBUG
      // log it
      Logging::Log(log_level_debug3, m_attributes.topic_name + "::CDataReader::AddSample::Receive::Buffered");
#endif
    }

    return(size_);
  }

  std::string CDataReader::Dump(const std::string& indent_ /* = "" */)
  {
    std::stringstream out;

    out << '\n';
    out << indent_ << "------------------------------------" << '\n';
    out << indent_ << " class CDataReader " << '\n';
    out << indent_ << "------------------------------------" << '\n';
    out << indent_ << "m_host_name:                        " << m_attributes.host_name << '\n';
    out << indent_ << "m_host_group_name:                  " << m_attributes.host_group_name << '\n';
    out << indent_ << "m_topic_name:                       " << m_attributes.topic_name << '\n';
    out << indent_ << "m_topic_id:                         " << m_topic_id << '\n';
    out << indent_ << "m_topic_info.encoding:              " << m_topic_info.encoding << '\n';
    out << indent_ << "m_topic_info.name:                  " << m_topic_info.name << '\n';
    out << indent_ << "m_topic_info.desc:                  " << m_topic_info.descriptor << '\n';
    out << indent_ << "m_topic_size:                       " << m_topic_size << '\n';
    out << indent_ << "m_read_buf.size():                  " << m_read_buf.size() << '\n';
    out << indent_ << "m_read_time:                        " << m_read_time << '\n';
    out << indent_ << "m_clock:                            " << m_clock << '\n';
    out << indent_ << "frequency [mHz]:                    " << GetFrequency() << '\n';
    out << indent_ << "m_created:                          " << m_created << '\n';
    out << '\n';

    return(out.str());
  }

  void CDataReader::Register()
  {
#if ECAL_CORE_REGISTRATION
    Registration::Sample sample;
    GetRegistrationSample(sample);
    if (g_registration_provider() != nullptr) g_registration_provider()->RegisterSample(sample);

#ifndef NDEBUG
    // log it
    Logging::Log(log_level_debug4, m_attributes.topic_name + "::CDataReader::Register");
#endif
#endif // ECAL_CORE_REGISTRATION
  }

  void CDataReader::Unregister()
  {
#if ECAL_CORE_REGISTRATION
    Registration::Sample sample;
    GetUnregistrationSample(sample);
    if (g_registration_provider() != nullptr) g_registration_provider()->UnregisterSample(sample);

#ifndef NDEBUG
    // log it
    Logging::Log(log_level_debug4, m_attributes.topic_name + "::CDataReader::Unregister");
#endif
#endif // ECAL_CORE_REGISTRATION
  }

  void CDataReader::GetRegistration(Registration::Sample& sample)
  {
    // return registration
    return GetRegistrationSample(sample);
  }

  bool CDataReader::IsPublished() const
  {
    return m_connection_count > 0;
  }

  size_t CDataReader::GetPublisherCount() const
  {
    return m_connection_count;
  }
    
  void CDataReader::GetRegistrationSample(Registration::Sample& ecal_reg_sample)
  {
    ecal_reg_sample.cmd_type = bct_reg_subscriber;

    auto& ecal_reg_sample_identifier = ecal_reg_sample.identifier;
    ecal_reg_sample_identifier.process_id = m_attributes.process_id;
    ecal_reg_sample_identifier.entity_id  = m_topic_id;
    ecal_reg_sample_identifier.host_name  = m_attributes.host_name;

    auto& ecal_reg_sample_topic = ecal_reg_sample.topic;
    ecal_reg_sample_topic.hgname = m_attributes.host_group_name;
    ecal_reg_sample_topic.tname  = m_attributes.topic_name;
    // topic_information
    {
      auto& ecal_reg_sample_tdatatype = ecal_reg_sample_topic.tdatatype;
      if (m_attributes.share_topic_type)
      {
        ecal_reg_sample_tdatatype.encoding = m_topic_info.encoding;
        ecal_reg_sample_tdatatype.name     = m_topic_info.name;
      }
      if (m_attributes.share_topic_description)
      {
        ecal_reg_sample_tdatatype.descriptor = m_topic_info.descriptor;
      }
    }
    ecal_reg_sample_topic.attr  = m_attr;
    ecal_reg_sample_topic.tsize = static_cast<int32_t>(m_topic_size);

#if ECAL_CORE_TRANSPORT_UDP
    // udp multicast layer
    {
      Registration::TLayer udp_tlayer;
      udp_tlayer.type      = tl_ecal_udp;
      udp_tlayer.version   = ecal_transport_layer_version;
      udp_tlayer.enabled   = m_layers.udp.read_enabled;
      udp_tlayer.active    = m_layers.udp.active;
      ecal_reg_sample_topic.tlayer.push_back(udp_tlayer);
    }
#endif

#if ECAL_CORE_TRANSPORT_SHM
    // shm layer
    {
      Registration::TLayer shm_tlayer;
      shm_tlayer.type      = tl_ecal_shm;
      shm_tlayer.version   = ecal_transport_layer_version;
      shm_tlayer.enabled   = m_layers.shm.read_enabled;
      shm_tlayer.active    = m_layers.shm.active;
      ecal_reg_sample_topic.tlayer.push_back(shm_tlayer);
    }
#endif

#if ECAL_CORE_TRANSPORT_TCP
    // tcp layer
    {
      Registration::TLayer tcp_tlayer;
      tcp_tlayer.type      = tl_ecal_tcp;
      tcp_tlayer.version   = ecal_transport_layer_version;
      tcp_tlayer.enabled   = m_layers.tcp.read_enabled;
      tcp_tlayer.active    = m_layers.tcp.active;
      ecal_reg_sample_topic.tlayer.push_back(tcp_tlayer);
    }
#endif

    ecal_reg_sample_topic.pname         = m_attributes.process_name;
    ecal_reg_sample_topic.uname         = m_attributes.unit_name;
    ecal_reg_sample_topic.dclock        = m_clock;
    ecal_reg_sample_topic.dfreq         = GetFrequency();
    ecal_reg_sample_topic.message_drops = static_cast<int32_t>(m_message_drops);

    // we do not know the number of connections ..
    ecal_reg_sample_topic.connections_loc = 0;
    ecal_reg_sample_topic.connections_ext = 0;
  }

  void CDataReader::GetUnregistrationSample(Registration::Sample& ecal_unreg_sample)
  {
    ecal_unreg_sample.cmd_type = bct_unreg_subscriber;

    auto& ecal_reg_sample_identifier = ecal_unreg_sample.identifier;
    ecal_reg_sample_identifier.process_id = m_attributes.process_id;
    ecal_reg_sample_identifier.entity_id = m_topic_id;
    ecal_reg_sample_identifier.host_name = m_attributes.host_name;

    auto& ecal_reg_sample_topic = ecal_unreg_sample.topic;
    ecal_reg_sample_topic.hgname = m_attributes.host_group_name;
    ecal_reg_sample_topic.pname  = m_attributes.process_name;
    ecal_reg_sample_topic.tname  = m_attributes.topic_name;
    ecal_reg_sample_topic.uname  = m_attributes.unit_name;
  }
  
  void CDataReader::StartTransportLayer()
  {
#if ECAL_CORE_TRANSPORT_UDP
    if (m_attributes.udp.enable)
    {
      // flag enabled
      m_layers.udp.read_enabled = true;

      // subscribe to layer (if supported)
      CUDPReaderLayer::Get()->AddSubscription(m_attributes.host_name, m_attributes.topic_name, m_topic_id);
    }
#endif

#if ECAL_CORE_TRANSPORT_SHM
    if (m_attributes.shm.enable)
    {
      // flag enabled
      m_layers.shm.read_enabled = true;

      // subscribe to layer (if supported)
      CSHMReaderLayer::Get()->AddSubscription(m_attributes.host_name, m_attributes.topic_name, m_topic_id);
    }
#endif

#if ECAL_CORE_TRANSPORT_TCP
    if (m_attributes.tcp.enable)
    {
      // flag enabled
      m_layers.tcp.read_enabled = true;

      // subscribe to layer (if supported)
      CTCPReaderLayer::Get()->AddSubscription(m_attributes.host_name, m_attributes.topic_name, m_topic_id);
    }
#endif
  }
  
  void CDataReader::StopTransportLayer()
  {
#if ECAL_CORE_TRANSPORT_UDP
    if (m_attributes.udp.enable)
    {
      // flag disabled
      m_layers.udp.read_enabled = false;

      // unsubscribe from layer (if supported)
      CUDPReaderLayer::Get()->RemSubscription(m_attributes.host_name, m_attributes.topic_name, m_topic_id);
    }
#endif

#if ECAL_CORE_TRANSPORT_SHM
    if (m_attributes.shm.enable)
    {
      // flag disabled
      m_layers.shm.read_enabled = false;

      // unsubscribe from layer (if supported)
      CSHMReaderLayer::Get()->RemSubscription(m_attributes.host_name, m_attributes.topic_name, m_topic_id);
    }
#endif

#if ECAL_CORE_TRANSPORT_TCP
    if (m_attributes.tcp.enable)
    {
      // flag disabled
      m_layers.tcp.read_enabled = false;

      // unsubscribe from layer (if supported)
      CTCPReaderLayer::Get()->RemSubscription(m_attributes.host_name, m_attributes.topic_name, m_topic_id);
    }
#endif
  }

  void CDataReader::FireConnectEvent(const std::string& tid_, const SDataTypeInformation& tinfo_)
  {
    const std::lock_guard<std::mutex> lock(m_event_callback_map_mtx);
    auto iter = m_event_callback_map.find(sub_event_connected);
    if (iter != m_event_callback_map.end() && iter->second)
    {
      SSubEventCallbackData data;
      data.type      = sub_event_connected;
      data.time      = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
      data.clock     = 0;
      data.tid       = tid_;
      data.tdatatype = tinfo_;
      (iter->second)(m_attributes.topic_name.c_str(), &data);
    }
  }

  void CDataReader::FireUpdateEvent(const std::string& tid_, const SDataTypeInformation& tinfo_)
  {
    const std::lock_guard<std::mutex> lock(m_event_callback_map_mtx);
    auto iter = m_event_callback_map.find(sub_event_update_connection);
    if (iter != m_event_callback_map.end() && iter->second)
    {
      SSubEventCallbackData data;
      data.type      = sub_event_update_connection;
      data.time      = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
      data.clock     = 0;
      data.tid       = tid_;
      data.tdatatype = tinfo_;
      (iter->second)(m_attributes.topic_name.c_str(), &data);
    }
  }

  void CDataReader::FireDisconnectEvent()
  {
    const std::lock_guard<std::mutex> lock(m_event_callback_map_mtx);
    auto iter = m_event_callback_map.find(sub_event_disconnected);
    if (iter != m_event_callback_map.end() && iter->second)
    {
      SSubEventCallbackData data;
      data.type  = sub_event_disconnected;
      data.time  = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
      data.clock = 0;
      (iter->second)(m_attributes.topic_name.c_str(), &data);
    }
  }

  size_t CDataReader::GetConnectionCount()
  {
    // no need to lock map here for now, map locked by caller
    size_t count(0);
    for (const auto& sub : m_connection_map)
    {
      if (sub.second.state)
      {
        count++;
      }
    }
    return count;
  }

  bool CDataReader::CheckMessageClock(const std::string& tid_, long long current_clock_)
  {
    auto iter = m_writer_counter_map.find(tid_);
    
    // initial entry
    if (iter == m_writer_counter_map.end())
    {
      m_writer_counter_map[tid_] = current_clock_;
      return true;
    }
    // clock entry exists
    else
    {
      // calculate difference
      const long long last_clock = iter->second;
      const long long clock_difference = current_clock_ - last_clock;

      // this is perfect, the next message arrived
      if (clock_difference == 1)
      {
        // update the internal clock counter
        iter->second = current_clock_;

        // process it
        return true;
      }

      // that should never happen, maybe there is a publisher
      // sending parallel on multiple layers ?
      // we ignore this message duplicate
      if (clock_difference == 0)
      {
        // do not update the internal clock counter

        // do not process it
        return false;
      }

      // that means we miss at least one message
      // -> we have a "message drop"
      if (clock_difference > 1)
      {
#if 0
        // we log this
        std::string msg = std::to_string(counter_ - counter_last) + " Messages lost ! ";
        msg += "(Unit: \'";
        msg += m_attributes.unit_name;
        msg += "@";
        msg += m_attributes.host_name;
        msg += "\' | Subscriber: \'";
        msg += m_attributes.topic_name;
        msg += "\')";
        Logging::Log(log_level_warning, msg);
#endif
        // we fire the message drop event
        {
          const std::lock_guard<std::mutex> lock(m_event_callback_map_mtx);
          auto citer = m_event_callback_map.find(sub_event_dropped);
          if (citer != m_event_callback_map.end() && citer->second)
          {
            SSubEventCallbackData data;
            data.type  = sub_event_dropped;
            data.time  = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            data.clock = current_clock_;
            (citer->second)(m_attributes.topic_name.c_str(), &data);
          }
        }

        // increase the drop counter
        m_message_drops += clock_difference;

        // update the internal clock counter
        iter->second = current_clock_;

        // process it
        return true;
      }

      // a negative clock difference may happen if a publisher
      // is using a shm ringbuffer and messages arrive in the wrong order
      if (clock_difference < 0)
      {
        // -----------------------------------
        // drop messages in the wrong order
        // -----------------------------------
        if (Config::Experimental::GetDropOutOfOrderMessages())
        {
          // do not update the internal clock counter

          // there is no need to fire the drop event, because
          // this event has been fired with the message before

          // do not process it
          return false;
        }
        // -----------------------------------
        // process messages in the wrong order
        // -----------------------------------
        else
        {
          // do not update the internal clock counter

          // but we log this
          std::string msg = "Subscriber: \'";
          msg += m_attributes.topic_name;
          msg += "\'";
          msg += " received a message in the wrong order";
          Logging::Log(log_level_warning, msg);

          // process it
          return true;
        }
      }
    }

    // should never be reached
    return false;
  }

  int32_t CDataReader::GetFrequency()
  {
    const auto frequency_time = std::chrono::steady_clock::now();
    const std::lock_guard<std::mutex> lock(m_frequency_calculator_mtx);
    return static_cast<int32_t>(m_frequency_calculator.getFrequency(frequency_time) * 1000);
  }
}
