/*!
 * @section LICENSE
 *
 * @copyright
 * Copyright (c) 2015 Intel Corporation
 *
 * @copyright
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * @copyright
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * @copyright
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * @section DESCRIPTION
 *
 * */

#include "eventing_server.hpp"
#include "json/json.hpp"

using namespace psme::command;
using namespace psme::app::eventing;

EventingServer::EventingServer(const json::Value& config) :
    m_config(config),
    m_http_server{m_config},
    m_command_json_server{m_http_server}
{}

EventingServer::~EventingServer() {
    stop();
}
void EventingServer::start() {
    if (m_config["eventing"]["enabled"].as_bool()) {
        if (!m_running) {
            m_running = true;
            m_thread = std::thread{&CommandJsonServer::start,
                &m_command_json_server};
        }
    }
}

void EventingServer::stop() {
    if (m_running) {
        m_running = false;
        m_command_json_server.stop();
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }
}

void EventingServer::add(const psme::command::CommandJson::Map::command_map_t&
                                                                command_map) {
    m_command_json_server.add(command_map);
}
