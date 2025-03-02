# ========================= eCAL LICENSE =================================
#
# Copyright (C) 2016 - 2025 Continental Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
# 
#      http://www.apache.org/licenses/LICENSE-2.0
# 
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# ========================= eCAL LICENSE =================================

find_package(CapnProto REQUIRED)

##########################
# Capnproto core communication extension
##########################
add_library(capnproto_core INTERFACE)
add_library(eCAL::capnproto_core ALIAS capnproto_core)

target_link_libraries(capnproto_core 
  INTERFACE 
    CapnProto::capnp
    eCAL::message_core
)

target_sources(capnproto_core
  INTERFACE
    FILE_SET capnproto_core_headers
    TYPE HEADERS
    BASE_DIRS include
    FILES
      include/ecal/msg/capnproto/dynamic.h
      include/ecal/msg/capnproto/helper.h
      include/ecal/msg/capnproto/publisher.h
      include/ecal/msg/capnproto/subscriber.h      
)

target_compile_features(capnproto_core INTERFACE cxx_std_17)

install(
  TARGETS capnproto_core
  EXPORT eCALCoreTargets  
  ARCHIVE       DESTINATION "${eCAL_install_archive_dir}" COMPONENT sdk
  LIBRARY       DESTINATION "${eCAL_install_lib_dir}"     COMPONENT sdk
  FILE_SET capnproto_core_headers COMPONENT sdk
)