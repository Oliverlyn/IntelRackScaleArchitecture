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
 */

#include "iscsi/tgt/config/tgt_target_config.hpp"
#include "iscsi/tgt/config/tgt_target_lun_config.hpp"
#include <cstring>
#include <sstream>
#include <exception>

using namespace agent::storage::iscsi::tgt::config;
using namespace std;

constexpr const char* TGT_CONFIG_PROPERTY_INITIATOR_ADDRESS = "initiator-address";
constexpr const char* TAB = "\t";

const string TgtTargetConfig::to_string() const {
    const auto& target_iqn = m_target->get_target_iqn();
    if (target_iqn.empty()) {
        throw invalid_argument("Target iqn is empty");
    }
    const auto& target_luns = m_target->get_target_lun();
    if (0 == target_luns.size()) {
        throw invalid_argument("No lun for target");
    }
    ostringstream content;
    content << get_target_begin_tag() << endl;
    for (auto& lun : target_luns) {
        TgtTargetLunConfig lunConf(lun);
        content << lunConf.to_string();
    }
    if (!m_target->get_initiator_iqn().empty()) {
        content << TAB << get_target_initiator_address() << endl;
    }
    content << get_target_end_tag() << endl;
    return content.str();
}

const string TgtTargetConfig::get_target_begin_tag() const {
    return "<target " + m_target->get_target_iqn() + ">";
}

const string TgtTargetConfig::get_target_end_tag() const {
    return string("</target>");
}

const string TgtTargetConfig::get_target_initiator_address() const {
    return TGT_CONFIG_PROPERTY_INITIATOR_ADDRESS + string(" ") + m_target->get_initiator_iqn();
}

