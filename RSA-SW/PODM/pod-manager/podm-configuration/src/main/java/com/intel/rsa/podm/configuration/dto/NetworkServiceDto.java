/*
 * Copyright (c) 2015 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.intel.rsa.podm.configuration.dto;

import com.fasterxml.jackson.annotation.JsonProperty;
import com.intel.rsa.common.types.Health;
import com.intel.rsa.common.types.State;

public class NetworkServiceDto {
    @JsonProperty("Name")
    private String name;
    @JsonProperty("Description")
    private String description;
    @JsonProperty("State")
    private State state;
    @JsonProperty("Health")
    private Health health;
    @JsonProperty("HostName")
    private String hostName;
    @JsonProperty("FQDN")
    private String fqdn;
    @JsonProperty("HTTP")
    private NetworkProtocolDto http;
    @JsonProperty("HTTPS")
    private NetworkProtocolDto https;
    @JsonProperty("IPMI")
    private NetworkProtocolDto ipmi;
    @JsonProperty("SSH")
    private NetworkProtocolDto ssh;
    @JsonProperty("SNMP")
    private NetworkProtocolDto snmp;
    @JsonProperty("VirtualMedia")
    private NetworkProtocolDto virtualMedia;
    @JsonProperty("SSDP")
    private NetworkProtocolDto ssdp;
    @JsonProperty("Telnet")
    private NetworkProtocolDto telnet;
    @JsonProperty("KVMIP")
    private NetworkProtocolDto kvmip;

    public String getName() {
        return name;
    }

    public String getDescription() {
        return description;
    }

    public Health getHealth() {
        return health;
    }

    public State getState() {
        return state;
    }

    public String getHostName() {
        return hostName;
    }

    public String getFqdn() {
        return fqdn;
    }

    public NetworkProtocolDto getHttp() {
        return http;
    }

    public NetworkProtocolDto getHttps() {
        return https;
    }

    public NetworkProtocolDto getIpmi() {
        return ipmi;
    }

    public NetworkProtocolDto getSsh() {
        return ssh;
    }

    public NetworkProtocolDto getSnmp() {
        return snmp;
    }

    public NetworkProtocolDto getVirtualMedia() {
        return virtualMedia;
    }

    public NetworkProtocolDto getSsdp() {
        return ssdp;
    }

    public NetworkProtocolDto getTelnet() {
        return telnet;
    }

    public NetworkProtocolDto getKvmip() {
        return kvmip;
    }
}

