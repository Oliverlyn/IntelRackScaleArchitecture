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
 * */

#include "agent-framework/command/network/add_port_vlan.hpp"
#include "agent-framework/module/module_manager.hpp"
#include "api/switch_vlan.hpp"
#include "api/ies/switch_vlan.hpp"

using namespace agent_framework::command;
using namespace agent_framework::generic;

namespace agent {
namespace network {
namespace hw {
namespace fm6000 {

using namespace agent_framework;

class AddPortVlan : public command::network::AddPortVlan {

public:
    AddPortVlan() { }

    using agent_framework::command::network::AddPortVlan::execute;

    void execute(const Request& request, Response& response) {
#ifdef IES_FOUND
        const auto& uuid = request.get_component();
        auto vlan_id = request.get_vlan_id();
        const auto& port_identifier = request.get_port_identifier();
        auto tagged = request.get_tagged();
        try{
            api::ies::SwitchVlan vlan(uuid, std::to_string(vlan_id));
            vlan.add_port(port_identifier, tagged);
        }
        catch (runtime_error& error) {
            log_error(GET_LOGGER("fm6000"), "Cannot add port to vlan");
            log_debug(GET_LOGGER("fm6000"), error.what());
            throw exception::NotFound();
        }

        response.set_vlan_identifier(std::to_string(vlan_id));
#else
        (void)request;
#endif
        response.set_oem({});
    }

    ~AddPortVlan();
};

AddPortVlan::~AddPortVlan() { }

}
}
}
}

static Command::Register<agent::network::hw::fm6000::AddPortVlan> g("fm6000");
