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
 * @file   ecal_callback.h
 * @brief  eCAL subscriber callback interface
**/

#pragma once

#include <ecal/ecal_deprecate.h>
#include <ecal/cimpl/ecal_callback_cimpl.h>
#include <ecal/ecal_types.h>

#include <functional>
#include <string>

namespace eCAL
{
  /**
   * @brief eCAL subscriber receive callback struct.
  **/
  struct SReceiveCallbackData
  {
    void*     buf   = nullptr;  //!< payload buffer
    long      size  = 0;        //!< payload buffer size
    long long id    = 0;        //!< publisher id (SetId())
    long long time  = 0;        //!< publisher send time in µs
    long long clock = 0;        //!< publisher send clock
  };

  /**
   * @brief eCAL publisher event callback struct.
  **/
  struct SPubEventCallbackData
  {
    eCAL_Publisher_Event type{pub_event_none};  //!< publisher event type
    long long            time{0};               //!< publisher event time in µs
    long long            clock{0};              //!< publisher event clock
    std::string          tid;                   //!< topic id of the of the connected subscriber                 (for pub_event_update_connection only)
    SDataTypeInformation tdatatype;             //!< datatype description of the connected subscriber            (for pub_event_update_connection only)
  };

  /**
   * @brief eCAL subscriber event callback struct.
  **/
  struct SSubEventCallbackData
  {
    eCAL_Subscriber_Event type{sub_event_none}; //!< subscriber event type
    long long             time{0};              //!< subscriber event time in µs
    long long             clock{0};             //!< subscriber event clock
    std::string           tid;                  //!< topic id of the of the connected publisher              (for sub_event_update_connection only)
    SDataTypeInformation  tdatatype;            //!< topic information of the connected subscriber           (for pub_event_update_connection only)
  };

  /**
   * @brief eCAL service attributes.
  **/
  struct SServiceAttr
  {
    std::string    key;              //!< unique service key (internal)
    std::string    hname;            //!< host name
    std::string    pname;            //!< process name
    std::string    uname;            //!< process unit name
    std::string    sname;            //!< service name
    std::string    sid;              //!< service id
    int            pid         = 0;  //!< process id

    // internal protocol specifics
    unsigned int   version     = 0;  //!< service protocol version
    unsigned short tcp_port_v0 = 0;  //!< service tcp port protocol version 0
    unsigned short tcp_port_v1 = 0;  //!< service tcp port protocol version 1
  };

  /**
   * @brief eCAL client event callback struct.
  **/
  struct SClientEventCallbackData
  {
    eCAL_Client_Event type = client_event_none;  //!< event type
    long long         time = 0;                  //!< event time in µs
    SServiceAttr      attr;                      //!< event related service attributes
  };

  /**
   * @brief eCAL client attributes.
  **/
  struct SClientAttr
  {
    std::string    key;           //!< unique service key (internal)
    std::string    hname;         //!< host name
    std::string    pname;         //!< process name
    std::string    uname;         //!< process unit name
    std::string    sname;         //!< service name
    std::string    sid;           //!< service id
    int            pid = 0;       //!< process id

    unsigned int   version = 0;   //!< client version
  };

  /**
   * @brief eCAL server event callback struct.
  **/
  struct SServerEventCallbackData
  {
    eCAL_Server_Event type = server_event_none;  //!< event type
    long long         time = 0;                  //!< event time in µs
  };

  /**
   * @brief Receive callback function type with topic name and data struct. (deprecated)
   *
   * @param topic_name_  The topic name of the received message.
   * @param data_        Data struct containing payload, timestamp and publication clock.
  **/
  using ReceiveCallbackT = std::function<void (const char* topic_name_, const struct SReceiveCallbackData* data_)>;

  /**
   * @brief Receive callback function type with topic id and data struct. The topic id contains the topic name, the process
   *          name, the host name and a uniques topic identifier.
   *
   * @param topic_id_        The topic id struct of the received message.
   * @param data_type_info_  Topic data type information (encoding, type, descriptor).
   * @param data_            Data struct containing payload, timestamp and publication clock.
  **/
  using ReceiveIDCallbackT = std::function<void(const Registration::STopicId& topic_id_, const SDataTypeInformation& data_type_info_, const SReceiveCallbackData& data_)>;

  /**
   * @brief Timer callback function type.
  **/
  using TimerCallbackT = std::function<void ()>;

  /**
   * @brief Registration callback type.
   *
   * @param sample_       The sample protocol buffer registration payload buffer.
   * @param sample_size_  The payload buffer size.
  **/
  using RegistrationCallbackT = std::function<void (const char* sample_, int sample_size_)>;

  /**
   * @brief Publisher event callback function type. (deprecated)
   *
   * @param topic_name_  The topic name of the publisher that triggered the event.
   * @param data_        Event callback data structure with the event specific information.
  **/
  using PubEventCallbackT = std::function<void (const char* topic_name_, const struct SPubEventCallbackData* data_)>;

  /**
   * @brief Publisher event callback function type.
   *
   * @param topic_id_  The topic id struct of the received message.
   * @param data_      Event callback data structure with the event specific information.
  **/
  using PubEventIDCallbackT = std::function<void(const Registration::STopicId& topic_id_, const struct SPubEventCallbackData& data_)>;

  /**
   * @brief Subscriber event callback function type. (deprecated)
   *
   * @param topic_name_  The topic name of the subscriber that triggered the event.
   * @param data_        Event callback data structure with the event specific information.
  **/
  using SubEventCallbackT = std::function<void (const char* topic_name_, const struct SSubEventCallbackData* data_)>;

  /**
   * @brief Subscriber event callback function type.
   *
   * @param topic_id_  The topic id struct of the received message.
   * @param data_      Event callback data structure with the event specific information.
  **/
  using SubEventIDCallbackT = std::function<void(const Registration::STopicId& topic_id_, const struct SSubEventCallbackData& data_)>;

  /**
   * @brief Client event callback function type. (deprecated)
   *
   * @param service_name_  The service name of the connection that triggered the event.
   * @param data_          Event callback data structure with the event specific information.
  **/
  using ClientEventCallbackT = std::function<void (const char* service_name_, const struct SClientEventCallbackData* data_)>;

  /**
   * @brief Client event callback function type.
   *
   * @param service_id_  The service id struct of the connection that triggered the event.
   * @param data_        Event callback data structure with the event specific information.
  **/
  using ClientEventIDCallbackT = std::function<void(const Registration::SServiceMethodId& service_id_, const struct SClientEventCallbackData& data_)>;

  /**
   * @brief Server event callback function type. (deprecated)
   *
   * @param service_name_  The service name of the connection that triggered the event.
   * @param data_          Event callback data structure with the event specific information.
  **/
  using ServerEventCallbackT = std::function<void (const char* service_name_, const struct SServerEventCallbackData* data_)>;

  /**
   * @brief Server event callback function type.
   *
   * @param service_id_  The service id struct of the connection that triggered the event.
   * @param data_        Event callback data structure with the event specific information.
  **/
  using ServerEventIDCallbackT = std::function<void(const Registration::SServiceMethodId& service_id_, const struct SServerEventCallbackData& data_)>;
}
