/* ========================= eCAL LICENSE =================================
 *
 * Copyright (C) 2016 - 2019 Continental Corporation
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

#pragma once

#include <ctime>
#include <list>
#include <string>
#include <vector>
#include <comdef.h>
#include <Wbemidl.h>

#include "ressource.h"

class Network : public Resource
{
 public:
  Network();
  ~Network();
  std::list<std::shared_ptr<Resource>> GetResourceInfo(const HQUERY& h_query_, PDH_STATUS& pdhStatus, QueryManager& query_manager, std::string type = "2|3");
 private:
  const std::string ConvertBSTRToStdString(const BSTR &bstr);

  std::vector <std::string>  receive_net_speed;
  std::vector <std::string>  send_net_speed;
  std::time_t old_timestamp;
  bool first_time_function_call;
};
