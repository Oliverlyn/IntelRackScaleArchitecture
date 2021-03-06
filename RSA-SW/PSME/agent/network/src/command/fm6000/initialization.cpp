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

#include "network_config.hpp"
#include "agent-framework/command/network/initialization.hpp"
#include "agent-framework/logger_ext.hpp"
#include "hw/fm6000/network_controller_manager.hpp"
#include "hw/fm6000/network_controller.hpp"

using namespace agent::network::hw;
using namespace agent_framework::command;

namespace agent {
namespace network {
namespace hw {
namespace fm6000 {

using namespace agent_framework;

/*! Initialization command */
class Initialization : public command::network::Initialization {
public:
    /*! Initialization */
    Initialization();

    /*! Deinitialization */
    ~Initialization();
};

}
}
}
}

fm6000::Initialization::Initialization() {
    log_debug(GET_LOGGER("fm6000"), "Initialization");
#ifdef IES_FOUND
    auto network_controller = NetworkControllerManager::get_network_controller();
    network_controller->initialize();
#endif
}

fm6000::Initialization::~Initialization() {
    log_debug(GET_LOGGER("fm6000"), "Deinitialization");
#ifdef IES_FOUND
    auto network_controller = NetworkControllerManager::get_network_controller();
    network_controller->deinitialize();
    NetworkControllerManager::cleanup();
#endif
}
static Command::Register<fm6000::Initialization> g("fm6000");
