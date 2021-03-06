/**
 * Copyright (c)  2015, Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *   http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#include <sys/sysinfo.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <openssl/evp.h>
#include <openssl/bio.h>

#include "rmcp.h"
#include "rmcp+.h"
#include "ipmi.h"

#include "libutils/dump.h"
#include "libutils/types.h"
#include "libutils/string.h"
#include "libutils/sock.h"
#include "libutils/rmm.h"
#include "librmmlog/rmmlog.h"
#include "libjipmi/common.h"
#include "libjipmi/jrpc_ipmi.h"
#include "libjson/json.h"
#include "libjsonrpc/jsonrpc.h"
#include "librmmcfg/rmm_cfg.h"
#include "libutils/base64.h"

#define STR_PARAMS	"params"
#define MAX_KEY_NAME	32

enum json_rpc_ipmi_cmd_type {
	JSON_RPC_IPMI_CMD_TYPE_RMCP = 0x00,
	JSON_RPC_IPMI_CMD_TYPE_IPMB,
	JSON_RPC_IPMI_CMD_TYPE_SERIAL,
	JSON_RPC_IPMI_CMD_TYPE_RMCP_BR,
};

typedef void (*jrpc_ipmi_cmd_handle_fn) (char *dest_msg, struct ipmi_msg *req, ipmi_json_ipc_header_t header, va_list *valist);
struct jrpc_ipmi_cmd_handle_fn_table {
	char index;
	unsigned char type;
	/*unsigned char netfn;*/
	/*unsigned char cmd;*/
	jrpc_ipmi_cmd_handle_fn handle;
};

static int format_rmcp_cmd(char *dest_msg, struct ipmi_msg *req, ipmi_json_ipc_header_t header,
		unsigned int host, unsigned int port, const char *uname, const char *passwd);
static int format_ipmb_cmd(char *dest_msg, struct ipmi_msg *req, ipmi_json_ipc_header_t header, unsigned char sa);
static int format_serial_cmd(char *dest_msg, struct ipmi_msg *req, ipmi_json_ipc_header_t header);
static int format_rmcp_br_cmd(char *dest_msg, struct ipmi_msg *request, ipmi_json_ipc_header_t header,
		unsigned int host, unsigned int port,
		unsigned long target_addr, unsigned short target_channel, const char *uname, const char *passwd,
		unsigned long transit_addr, unsigned short transit_channel, int bridge_level);



static void rmcp_handle_common(char *dest_msg, struct ipmi_msg *req, ipmi_json_ipc_header_t header,
		va_list *valist
		/*unsigned int host, unsigned int port, const char *uname, const char *passwd*/);
static void rmcp_br_handle_common(char *dest_msg, struct ipmi_msg *req,	ipmi_json_ipc_header_t header,
		va_list *valist
		/*unsigned int host, unsigned int port, unsigned long target_addr, unsigned short target_ch, char * uname, char * passwd,
		unsigned long transit_addr, unsigned short transit_ch, int bridge_level*/);
static void ipmb_handle_common(char *dest_msg, struct ipmi_msg *req, ipmi_json_ipc_header_t header,
		va_list *valist
		/*unsigned char sa*/);
static void serial_handle_common(char *dest_msg, struct ipmi_msg *req, ipmi_json_ipc_header_t header,
					va_list *valist);

/**
  *  @brief dump hex data to ASCII format
  *
  *  @param[in] bp hex data array
  *  @param[in] length the hex data lenght
  *  @return
  */
static void dump_hex_ascii(const unsigned char *bp, unsigned int length)
{
	unsigned int ofs, j;
	char log_buf[1024] = { 0 };

	for (ofs = 0; ofs < length; ofs += 16) {
		char buf[80];

		snprintf(log_buf, sizeof(log_buf), "%04X: ", ofs);
		for (j = 0; j < 16 && j+ofs < length; j++) {
			memset(buf, 0, sizeof(buf));
			snprintf(buf, sizeof(buf), "%02X ", (unsigned)bp[j+ofs]);
			strncat(log_buf, buf, sizeof(buf));
		}
		if (j < 16) {
			memset(buf, 0, sizeof(buf));
			snprintf(buf, sizeof(buf), "%*s", 3*(16-j), "");
			strncat(log_buf, buf, sizeof(buf));
		}

		memset(buf, 0, sizeof(buf));
		for (j = 0; j < 16 && j+ofs < length; j++) {
			unsigned char ch = bp[j+ofs];

			if (ch < ' ')
				ch = '.';

			buf[j] = ch;
		}
		char tmp[128] = {0};
		snprintf(tmp, sizeof(tmp), "%.*s", (int)j, buf);
		strncat(log_buf, tmp, sizeof(tmp));
	}
	rmm_log(DBG, "%s\n", log_buf);
}


/**
  *  @brief get the integer value from json format structure
  *
  *  @param[in] req json format struture
  *  @param[out] value
  *  @param[in] key
  *  @return
  *  @retval -1 failure
  *  @retval 0 successful
  */
static inline int jrpc_get_int(json_t *req, int64 *value, char *key)
{
	json_t *json_obj = NULL;

	if (NULL == req ||
	NULL == (json_obj = json_object_get(req, key)) ||
	-1 == (*value = json_integer_value(json_obj)) ||
	*value == '\0')
		return JSONRPC_FAILED;

	return JSONRPC_SUCCESS;
}

/**
  *  @brief get the string value from json format structure
  *
  *  @param[in] req json format struture
  *  @param[out] value
  *  @param[in] key
  *  @return
  *  @retval -1 failure
  *  @retval 0 successful
  */
static inline int jrpc_get_string(json_t *req, char **value, char *key)
{
	json_t *json_obj = NULL;
	char *p_value = NULL;

	if (NULL == req ||
	NULL == (json_obj = json_object_get(req, key)) ||
	NULL == (p_value = json_string_value(json_obj)) ||
	p_value == NULL)
		return JSONRPC_FAILED;

	if (strlen(p_value) < MAX_KEY_NAME)
		memcpy(value, p_value, strlen(p_value));
	else
		return JSONRPC_FAILED;

	return JSONRPC_SUCCESS;
}

/**
  *  @brief get the value from second level json format structure
  *
  *  @param[in] req json format struture
  *  @param[in] param second json object key
  *  @param[out] value
  *  @param[in] key
  *  @return
  *  @retval -1 failure
  *  @retval 0 successful
  */
static int jrpc_get_key_value(json_t *req, char **value, char *param, char *key)
{
	json_t *json_obj = NULL;

	if (NULL == req ||
	NULL == (json_obj = json_object_get(req, param)) ||
	json_obj == NULL)
		return JSONRPC_FAILED;

	return jrpc_get_string(json_obj, value, key);
}

/**
  *  @brief build a json RPC string
  *
  *  @param[in] method JSON RPC method
  *  @param[in] id JSON RPC id
  *  @param[in] ipmi_data IPMI data
  *  @param[in] len IPMI data length
  *  @param[out] jrpc_data JSON RPC format data
  *  @param[in] jrpc_data_len JSON RPC format data lenght
  *  @return output JSON RPC data length
  */
static int jrpc_string_build(unsigned char *method, int64 id,
									unsigned char *ipmi_data, unsigned short len,
									unsigned char *jrpc_data, unsigned int jrpc_data_len)
{
	jrpc_param_t param[2] = { { 0 } };
	json_t *req = NULL;
	json_t *rsp = NULL;
	int rs_len = 0;
	char buffer[IPMI_JSONRPC_MAX_STRING_LEN] = {0};
	char str_buf_len[IPMI_JSONRPC_MAX_STRING_LEN] = {0};

	/*base64_encode(buffer, (char *)ipmi_data, (int)len);*/
	buf2hexstr((const char *)ipmi_data, len, buffer);

	req = json_object();
	if (NULL == req) {
		rmm_log(ERROR, "json_object failed\n");
		return 0;
	}

	/**use the real data len*/
	snprintf(str_buf_len, IPMI_JSONRPC_MAX_STRING_LEN, "%d", len);
	json_object_add(req, STR_DATA_LEN, json_string(str_buf_len));
	json_object_add(req, STR_DATA, json_string(buffer));

	rsp = jrpc_create_result_rsp(id, JSONRPC_ID_TYPE_NORMAL, req, JSON_OBJECT);
	if ((NULL != rsp) && (NULL != jrpc_data)) {
		if (jrpc_format_string(rsp, (char *)jrpc_data, (int)jrpc_data_len) == JSONRPC_FAILED) {
			rmm_log(ERROR, "\njrpc_data len exceed jrpc_data failed!\n");
		} else {
			rmm_log(DBG, "string build is : %s\n", jrpc_data);
			rs_len = strlen((const char *)jrpc_data);
		}

		json_free(rsp);
	}

	return rs_len;
}


static struct jrpc_ipmi_cmd_handle_fn_table handle_table[] = {
	{ 0, JSON_RPC_IPMI_CMD_TYPE_RMCP,       rmcp_handle_common      },
	{ 1, JSON_RPC_IPMI_CMD_TYPE_IPMB,       ipmb_handle_common      },
	{ 2, JSON_RPC_IPMI_CMD_TYPE_SERIAL,     serial_handle_common    },
	{ 3, JSON_RPC_IPMI_CMD_TYPE_RMCP_BR,    rmcp_br_handle_common   },

	/*don't modify below line*/
	{ 0xFF, 0, NULL }
};

/**
  *  @brief handle RMCP command
  *
  *  @param[out] dest_msg buffer
  *  @param[in] req include request information
  *  @param[in] header include JSON RPC information
  *  @param[in] valist valist
  *  @return retrun value
  */
static void rmcp_handle_common(char *dest_msg, struct ipmi_msg *req, ipmi_json_ipc_header_t header,
			va_list *valist)
{
	unsigned int host = 0;
	unsigned int port = 0;
	char *uname = NULL;
	char *passwd = NULL;

	host = va_arg(*valist, unsigned int);
	port = va_arg(*valist, unsigned int);
	uname = va_arg(*valist, char *);
	passwd = va_arg(*valist, char *);
	format_rmcp_cmd(dest_msg, req, header, host, port, uname, passwd);
}

/**
  *  @brief handle RMCP bridge command
  *
  *  @param[out] dest_msg buffer
  *  @param[in] req include request information
  *  @param[in] header include JSON RPC information
  *  @param[in] valist valist
  *  @return retrun value
  */
static void rmcp_br_handle_common(char *dest_msg, struct ipmi_msg *req,	ipmi_json_ipc_header_t header,
				va_list *valist)
{
	unsigned int host = 0;
	unsigned int port = 0;
	unsigned long target_addr = 0;
	unsigned short target_ch = 0;
	unsigned int int_target_ch = 0;
	char *uname = NULL;
	char *passwd = NULL;
	unsigned long transit_addr = 0;
	unsigned short transit_ch = 0;
	unsigned int int_transit_ch = 0;
	int bridge_level = 0;

	host = va_arg(*valist, unsigned int);
	port = va_arg(*valist, unsigned int);
	target_addr = va_arg(*valist, unsigned long);
	int_target_ch = va_arg(*valist, unsigned int);
	uname = va_arg(*valist, char *);
	passwd = va_arg(*valist, char *);
	transit_addr = va_arg(*valist, unsigned long);
	int_transit_ch = va_arg(*valist, unsigned int);
	bridge_level = va_arg(*valist, int);

	target_ch = (unsigned short)int_target_ch;
	transit_ch = (unsigned short)int_transit_ch;
	format_rmcp_br_cmd(dest_msg, req, header, host, port,
				target_addr, target_ch, uname, passwd,
				transit_addr, transit_ch, bridge_level);
}

/**
  *  @brief handle IPMB command
  *
  *  @param[out] dest_msg buffer
  *  @param[in] req include request information
  *  @param[in] header include JSON RPC information
  *  @param[in] valist valist
  *  @return retrun value
  */
static void ipmb_handle_common(char *dest_msg, struct ipmi_msg *req, ipmi_json_ipc_header_t header,
				va_list *valist)
{
	unsigned char sa = 0;
	unsigned int int_sa = 0;

	int_sa = va_arg(*valist, unsigned int);
	sa = (unsigned char)int_sa;

	format_ipmb_cmd(dest_msg, req, header, sa);
}

/**
  *  @brief handle serial command
  *
  *  @param[out] dest_msg buffer
  *  @param[in] req include request information
  *  @param[in] header include JSON RPC information
  *  @param[in] valist valist
  *  @return retrun value
  */
static void serial_handle_common(char *dest_msg, struct ipmi_msg *req, ipmi_json_ipc_header_t header,
				va_list *valist)
{
	format_serial_cmd(dest_msg, req, header);
}

static unsigned int gen_req_msgid(void)
{
	static unsigned int msgid = 1;

	if (++msgid == 0)
		msgid = 1;

	return msgid;
}

/**
  *  @brief format to app_msg_union format data, which acceptable by previous ipmi_module
  *
  *  @param[out] dest_msg buffer
  *  @param[in] addr IPMI device IP address
  *  @param[in] req include request information
  *  @param[in] header include JSON RPC information
  *  @param[in] timeo timeout
  *  @param[in] broadcast is broadcast
  *  @return retrun value
  *  @retval -1 failure
  *  @retval 0 sucessful
  */
static int convert_ipmi_appmsg(char *dest_msg, struct ipmi_addr *addr, const struct ipmi_msg *req,
			ipmi_json_ipc_header_t header, unsigned int timeo, int broadcast)
{
	unsigned int msgid;
	struct appmsg_ipmi_msg *ipmi;
	union app_msg_union msg;
	unsigned int new_timeo = timeo;

	if ((dest_msg == NULL) || (addr == NULL) || (req == NULL)) {
		rmm_log(ERROR, "%s: empty pointer!\n", __func__);
		return -1;
	}

	if (timeo > IPMI_MAX_TIMEOUT_MS)
		new_timeo = IPMI_MAX_TIMEOUT_MS;
	if (timeo == 0)
		new_timeo = IPMI_DFLT_TIMEOUT_MS;

	msg.hdr.type    = APPMSG_IPMI_MSG;
	msg.hdr.datalen = APPMSG_IPMI_MSG_LEN(req->data_len);

	ipmi = (struct appmsg_ipmi_msg *)msg.hdr.data;
	memset(ipmi, 0, sizeof(struct appmsg_ipmi_msg) - 4);
	ipmi->id = gen_req_msgid();
	ipmi->timeo = new_timeo;
	ipmi->user_port = header.port;
	memset(ipmi->header.method, 0, sizeof(JRPC_METHOD_MAX));
	ipmi->header.ip = header.ip;
	ipmi->header.port = header.port;
	ipmi->header.json_ipc_id = header.json_ipc_id;

	if (strlen((const char *)header.method) < JRPC_METHOD_MAX)
		memcpy(ipmi->header.method, header.method, strlen((const char *)header.method));

	memcpy(&ipmi->addr, addr, sizeof(struct ipmi_addr));
	ipmi->msg.netfn = req->netfn;
	ipmi->msg.cmd   = req->cmd;

	ipmi->msg.union_app_req.bridge.bridge_level = req->union_app_req.bridge.bridge_level;
	if (IPMI_BRIDGE_MSG_NONE != ipmi->msg.union_app_req.bridge.bridge_level) {
		ipmi->msg.union_app_req.bridge.my_addr = req->union_app_req.bridge.my_addr;
		ipmi->msg.union_app_req.bridge.transit_addr = req->union_app_req.bridge.transit_addr;
		ipmi->msg.union_app_req.bridge.transit_channel = req->union_app_req.bridge.transit_channel;
		ipmi->msg.union_app_req.bridge.target_addr = req->union_app_req.bridge.target_addr;
		ipmi->msg.union_app_req.bridge.target_channel = req->union_app_req.bridge.target_channel;
		ipmi->msg.union_app_req.bridge.br_netfn = req->union_app_req.bridge.br_netfn;
		ipmi->msg.union_app_req.bridge.br_cmd = req->union_app_req.bridge.br_cmd;
	}

	if ((IPMI_SERIAL_OPEN_DEV == req->union_app_req.serial.serial_flag)
		|| (IPMI_SERIAL_CLOSE_DEV == req->union_app_req.serial.serial_flag)) {
		ipmi->msg.union_app_req.serial.serial_flag = req->union_app_req.serial.serial_flag;
	} else if (IPMI_SERIAL_OPERATION == req->union_app_req.serial.serial_flag) {
		ipmi->msg.union_app_req.serial.serial_flag = req->union_app_req.serial.serial_flag;
		ipmi->msg.union_app_req.serial.serial_fd = req->union_app_req.serial.serial_fd;
	}

	ipmi->msg.data_len = req->data_len;
	if ((req->data_len > 0) && (req->data_len <= IPMI_MAX_DATA_LENGTH))
		memcpy(ipmi->msg.data, req->data, req->data_len);

	memcpy(dest_msg, &(msg.hdr), msg.hdr.datalen + APP_MSG_HDR_SIZE);
	return 0;
}

/**
  *  @brief format IPMB command with timeout condition
  *
  *  @param[out] dest_msg buffer
  *  @param[in] sa IPMI device source address
  *  @param[in] req include request information
  *  @param[in] header include JSON RPC information
  *  @param[in] timeo timeout
  *  @return retrun value
  *  @retval -1 failure
  *  @retval 0 sucessful
  */
static int format_ipmb_cmd_timeout(char *dest_msg, struct ipmi_msg *req, ipmi_json_ipc_header_t header,
			unsigned char sa, unsigned int timeo)
{
	struct ipmi_addr addr;

	if ((dest_msg == NULL) || (req == NULL)) {
		rmm_log(ERROR, "%s:empty pointer!\n", __func__);
		return -1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.type = IPMI_ADDR_TYPE_IPMB;
	addr.addr.ipmb.sa = sa;
	addr.addr.ipmb.lun = IPMI_BMC_CMD_LUN;

	return convert_ipmi_appmsg(dest_msg, &addr, req, header, timeo, is_ipmb_addr_bcast(sa));
}

/**
  *  @brief format IPMB command
  *
  *  @param[out] dest_msg buffer
  *  @param[in] req include request information
  *  @param[in] header json rpc header
  *  @param[in] sa IPMB source address
  *  @return retrun value
  *  @retval -1 failure
  *  @retval 0 sucessful
  */
static int format_ipmb_cmd(char *dest_msg, struct ipmi_msg *req, ipmi_json_ipc_header_t header, unsigned char sa)
{
	return format_ipmb_cmd_timeout(dest_msg, req, header, sa, IPMI_DFLT_TIMEOUT_MS);
}

#if 0
/**
  *  @brief format IPMB broadcast command
  *
  *  @param[out] dest msg buffer
  *  @param[in] req include request information
  *  @return retrun value
  *  @retval -1 failure
  *  @retval 0 sucessful
  */
static int format_ipmb_cmd_broadcast(char *dest_msg, struct ipmi_msg *req, ipmi_json_ipc_header_t header)
{
	return format_ipmb_cmd_timeout(dest_msg, req, header, IPMI_IPMB_BCAST_ADDR, IPMI_DFLT_TIMEOUT_MS);
}
#endif

/**
  *  @brief format IPMI command via serial interface with timeout condition
  *
  *  @param[out] dest msg buffer
  *  @param[in] req include request information
  *  @param[in] header include JSON RPC information
  *  @param[in] timeo timeout
  *  @return retrun value
  *  @retval -1 failure
  *  @retval 0 sucessful
  */
static int format_serial_cmd_timeout(char *dest_msg, struct ipmi_msg *req,
				ipmi_json_ipc_header_t header, unsigned int timeo)
{
	struct ipmi_addr addr = {0};

	if ((dest_msg == NULL) || (req == NULL)) {
		rmm_log(ERROR, "%s: empty pointer!\n", __func__);
		return -1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.type = IPMI_ADDR_TYPE_SERIAL;

	return convert_ipmi_appmsg(dest_msg, &addr, req, header, timeo, 0);
}

/**
  *  @brief format IPMI command via serial interface
  *
  *  @param[out] dest msg buffer
  *  @param[in] req include request information
  *  @param[in] header include JSON RPC information
  *  @return retrun value
  *  @retval -1 failure
  *  @retval 0 sucessful
  */
static int format_serial_cmd(char *dest_msg, struct ipmi_msg *req, ipmi_json_ipc_header_t header)
{
	return format_serial_cmd_timeout(dest_msg, req, header, IPMI_DFLT_TIMEOUT_MS);
}


/**
  *  @brief format RMCP command with time out condition
  *
  *  @param[out] dest msg buffer
  *  @param[in] host IPMI device IP address
  *  @param[in] port ipmid socket port
  *  @param[in] req include request information
  *  @param[in] header include JSON RPC information
  *  @param[in] timeo timeout
  *  @param[in] uname RMCP username
  *  @param[in] passwd RMCP password
  *  @return retrun value
  *  @retval -1 failure
  *  @retval 0 sucessful
  */
static int format_rmcp_cmd_timeout(char *dest_msg, unsigned int host, unsigned int port, struct ipmi_msg *req,
			ipmi_json_ipc_header_t header, unsigned int timeo, const char *uname, const char *passwd)
{
	struct ipmi_addr addr;

	if ((dest_msg == NULL) || (req == NULL)) {
		rmm_log(ERROR, "%s: empty pointer!\n", __func__);
		return -1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.type = IPMI_ADDR_TYPE_RMCP;
	addr.addr.rmcp.host = host;
	addr.addr.rmcp.port = port;
	strncpy_safe(addr.addr.rmcp.username, uname, RMCP_USERNAME_LEN, RMCP_USERNAME_LEN - 1);
	strncpy_safe(addr.addr.rmcp.password, passwd, RMCP_PASSWORD_LEN, RMCP_PASSWORD_LEN - 1);

	return convert_ipmi_appmsg(dest_msg, &addr, req, header, timeo, 0);
}


/**
  *  @brief format RMCP command
  *
  *  @param[out] dest msg buffer
  *  @param[in] host IPMI device IP address
  *  @param[in] port ipmid socket port
  *  @param[in] req include request information
  *  @param[in] header include JSON RPC information
  *  @param[in] uname RMCP username
  *  @param[in] passwd RMCP password
  *  @return retrun value
  *  @retval -1 failure
  *  @retval 0 sucessful
  */
static int format_rmcp_cmd(char *dest_msg, struct ipmi_msg *req, ipmi_json_ipc_header_t header,
		unsigned int host, unsigned int port, const char *uname, const char *passwd)
{
	return format_rmcp_cmd_timeout(dest_msg, host, port, req, header, IPMI_DFLT_TIMEOUT_MS, uname, passwd);
}

/**
  *  @brief format RMCP bridge command
  *
  *  @param[out] dest msg buffer
  *  @param[in] host IPMI device IP address
  *  @param[in] request include request information
  *  @param[in] target_addr IPMI target device IPMI address
  *  @param[in] target_channel IPMI target device channel
  *  @return retrun value
  *  @retval -1 failure
  *  @retval 0 sucessful
  */
static int format_rmcp_br_cmd(char *dest_msg, struct ipmi_msg *request, ipmi_json_ipc_header_t header,
			unsigned int host, unsigned int port,
			unsigned long target_addr, unsigned short target_channel, const char *uname, const char *passwd,
			unsigned long transit_addr, unsigned short transit_channel, int bridge_level)
{
	struct ipmi_msg *req;

	req = malloc(sizeof(*req));
	if (req == NULL) {
		rmm_log(ERROR, "%s: failed for no memory!\n", __func__);
		return -1;
	}

	memset(req, 0, sizeof(*req));

	req->netfn = IPMI_BRIDGE_NETFN_APP;
	req->cmd   = IPMI_BRIDGE_IPMI_CMD;
	req->union_app_req.bridge.bridge_level = bridge_level;
	req->union_app_req.bridge.my_addr = IPMI_BMC_SLAVE_ADDR;

	req->union_app_req.bridge.transit_addr = transit_addr;
	req->union_app_req.bridge.transit_channel = transit_channel;
	req->union_app_req.bridge.target_addr = target_addr;
	req->union_app_req.bridge.target_channel = target_channel;
	req->union_app_req.bridge.br_netfn = request->netfn;
	req->union_app_req.bridge.br_cmd = request->cmd;
	req->data_len = request->data_len;

	if (0 != request->data_len) {
		memcpy(req->data, request->data, request->data_len);
	}

	return format_rmcp_cmd_timeout(dest_msg, host, port, req, header, IPMI_DFLT_TIMEOUT_MS, uname, passwd);
}

/**
  *  @brief find handle
  *
  *  @param[in] type
  *  @param[in] netfn IPMI netfn
  *  @param[in] cmd IPMI command
  *  @return
  *  @retval -1 failure
  *  @retval > 0 handle index
  */
static int find_handle(unsigned char type/*, unsigned char netfn, unsigned char cmd*/)
{
	int i = 0;

	while (0xff != handle_table[i].index) {
		if ((type == handle_table[i].type)
			/*&&(netfn == handle_table[i].netfn)
			&&(cmd == handle_table[i].cmd)
			&&(NULL != handle_table[i].handle)*/) {
			return i;
		}
		i++;
	}
	return -1;
}

/**
  *  @brief RMCP message handle
  *
  *  @param[out] buffer output json rpc buffer
  *  @param[in] request request
  *  @param[in] header JSON RPC header include some information
  *  @return
  */
static void jrpc_rmcp_handle_request_cmd(char *dest_msg, struct ipmi_msg *req, ipmi_json_ipc_header_t header, ...)
{
	int i = -1;
	va_list args;

	va_start(args, header);

	i = find_handle(JSON_RPC_IPMI_CMD_TYPE_RMCP/*, req->netfn, req->cmd*/);
	if (i != -1)
		handle_table[i].handle(dest_msg, req, header, &args/*host, port, uname, passwd*/);
	else
		rmm_log(ERROR, "\nNot support request!!! type[%d] netfn[%d] cmd[%d]\n", JSON_RPC_IPMI_CMD_TYPE_RMCP, req->netfn, req->cmd);

	va_end(args);
}

/**
  *  @brief RMCP request message parse
  *
  *  @param[out] buffer output json rpc buffer
  *  @param[in] json_obj json object
  *  @param[in] header JSON RPC header include some information
  *  @return
  */
static void jrpc_rmcp_req_parse(char *dest_msg, json_t *json_obj, ipmi_json_ipc_header_t header)
{
	struct ipmi_msg req = {0};
	unsigned int host;
	unsigned int port;

	char value_netfn[MAX_KEY_NAME] = {0};
	char value_cmd[MAX_KEY_NAME] = {0};
	char value_data_len[MAX_KEY_NAME] = {0};
	char value_host[MAX_KEY_NAME] = {0};
	char value_port[MAX_KEY_NAME] = {0};
	char value_data[IPMI_JSONRPC_MAX_STRING_LEN] = {0};
	char value_uname[MAX_KEY_NAME] = {0};
	char value_passwd[MAX_KEY_NAME] = {0};
	int len = 0;
	char *w_buf = NULL;
	int i = 0;

	if (NULL == json_obj) {
		rmm_log(ERROR, "rmcp_req_handle json_obj == NULL\n");
		return;
	}

	if (jrpc_get_key_value(json_obj, (char **)&value_netfn, STR_PARAMS, STR_NETFN)) {
		rmm_log(ERROR, "jrpc_get_netfn failed.\n");
		return;
	} else if (jrpc_get_key_value(json_obj, (char **)&value_cmd, STR_PARAMS, STR_CMD)) {
		rmm_log(ERROR, "jrpc_get_cmd failed.\n");
		return;
	} else if (jrpc_get_key_value(json_obj, (char **)&value_data_len, STR_PARAMS, STR_DATA_LEN)) {
		rmm_log(ERROR, "jrpc_get_data_len failed.\n");
		return;
	} else if (jrpc_get_key_value(json_obj, (char **)&value_host, STR_PARAMS, STR_TARGET_IP)) {
		rmm_log(ERROR, "jrpc_get_target_ip failed.\n");
		return;
	} else if (jrpc_get_key_value(json_obj, (char **)&value_port, STR_PARAMS, STR_TARGET_PORT)) {
		rmm_log(ERROR, "jrpc_get_target_port failed.\n");
		return;
	} else if ((atoi((const char *)value_data_len) > 0) && (jrpc_get_key_value(json_obj, (char **)&value_data, STR_PARAMS, STR_DATA))) {
		rmm_log(ERROR, "jrpc_get_data failed.\n");
		return;
	} else if (jrpc_get_key_value(json_obj, (char **)&value_uname, STR_PARAMS, STR_NAME)) {
		rmm_log(ERROR, "jrpc_get_uname failed.\n");
		return;
	} else if (jrpc_get_key_value(json_obj, (char **)&value_passwd, STR_PARAMS, STR_PASSWD)) {
		rmm_log(ERROR, "jrpc_get_passwd failed.\n");
		return;
	} else
		rmm_log(DBG, "netfn[%s] cmd[%s] host[%s] len[%s]:\n", value_netfn, value_cmd, value_host, value_data_len);

	i = (int)atoi((const char *)value_data_len);
	if (i > 0) {
		/*w_buf = base64_decode(value_data, strlen(value_data), &len);*/
		w_buf = malloc(strlen(value_data));
		if (w_buf == NULL) {
			rmm_log(ERROR, "%s: w_buff is NULL\n", __func__);
			return;
		}
		len = hexstr2buf(value_data, strlen(value_data), w_buf);

		if (i != (int)len) {
			rmm_log(ERROR, "data len(%d != %d) is wrong!\n", i, len);
			return;
		} else {
			dump_hex_ascii((const unsigned char *)w_buf, (unsigned int)len);
		}
		memset(req.data, 0, IPMI_MAX_DATA_LENGTH);
		memcpy(req.data, w_buf, len);
		free(w_buf);
		w_buf = NULL;
	} else
		len = 0;

	req.netfn = (unsigned char)atoi((const char *)value_netfn);
	req.cmd   = (unsigned char)atoi((const char *)value_cmd);
	req.data_len = (unsigned short)len;
	host = (unsigned int)inet_addr((const char *)value_host);
	port = htons((unsigned int)atoi((const char *)value_port));

	jrpc_rmcp_handle_request_cmd(dest_msg, &req, header, host, port, value_uname, value_passwd);
}

/**
  *  @brief IPMB message handle
  *
  *  @param[out] buffer output json rpc buffer
  *  @param[in] request request
  *  @param[in] header JSON RPC header include some information
  *  @return
  */
static void jrpc_ipmb_handle_request_cmd(char *dest_msg, struct ipmi_msg *req, ipmi_json_ipc_header_t header, ...)
{
	int i = -1;
	va_list args;

	va_start(args, header);

	i = find_handle(JSON_RPC_IPMI_CMD_TYPE_IPMB/*, req->netfn, req->cmd*/);
	if (i != -1) {
		handle_table[i].handle(dest_msg, req, header, &args);
	} else
		rmm_log(ERROR, "\nNot support request!!! type[%d] netfn[%d] cmd[%d]\n", JSON_RPC_IPMI_CMD_TYPE_IPMB, req->netfn, req->cmd);

	va_end(args);
}

/**
  *  @brief IPMB request message parse
  *
  *  @param[out] buffer output json rpc buffer
  *  @param[in] json_obj json object
  *  @param[in] header JSON RPC header include some information
  *  @return
  */
static void jrpc_ipmb_req_parse(char *dest_msg, json_t *json_obj, ipmi_json_ipc_header_t header)
{
	struct ipmi_msg req = {0};
	unsigned char sa = 0;
	unsigned int int_sa = 0;
	char value_netfn[MAX_KEY_NAME] = {0};
	char value_cmd[MAX_KEY_NAME] = {0};
	char value_data_len[MAX_KEY_NAME] = {0};
	char value_src_addr[MAX_KEY_NAME] = {0};
	char value_data[IPMI_JSONRPC_MAX_STRING_LEN] = {0};

	int len = 0;
	char *w_buf = NULL;
	int i = 0;

	if (NULL == json_obj) {
		rmm_log(ERROR, "rmcp_req_handle json_obj == NULL\n");
		return;
	}

	if (jrpc_get_key_value(json_obj, (char **)&value_netfn, STR_PARAMS, STR_NETFN)) {
		rmm_log(ERROR, "jrpc_get_netfn failed.\n");
		return;
	} else if (jrpc_get_key_value(json_obj, (char **)&value_cmd, STR_PARAMS, STR_CMD)) {
		rmm_log(ERROR, "jrpc_get_cmd failed.\n");
		return;
	} else if (jrpc_get_key_value(json_obj, (char **)&value_data_len, STR_PARAMS, STR_DATA_LEN)) {
		rmm_log(ERROR, "jrpc_get_data_len failed.\n");
		return;
	} else if (jrpc_get_key_value(json_obj, (char **)&value_src_addr, STR_PARAMS, STR_IPMB_SRC_ADDR)) {
		rmm_log(ERROR, "jrpc_get_ipmb_src_addr failed.\n");
		return;
	} else if ((atoi((const char *)value_data_len) > 0) && (jrpc_get_key_value(json_obj, (char **)&value_data, STR_PARAMS, STR_DATA))) {
		rmm_log(ERROR, "jrpc_get_data failed.\n");
		return;
	} else
		rmm_log(DBG, "netfn[%s] cmd[%s] sa[%s] len[%s]:\n", value_netfn, value_cmd, value_src_addr, value_data_len);

	i = (int)atoi((const char *)value_data_len);
	if (i > 0) {
		/*w_buf = base64_decode(value_data, strlen(value_data), &len);*/
		w_buf = malloc(strlen(value_data));	
		if (w_buf == NULL) {
			rmm_log(ERROR, "%s: w_buff is NULL\n", __func__);
			return;
		}
		len = hexstr2buf(value_data, strlen(value_data), w_buf);

		if (i != (int)len) {
			rmm_log(ERROR, "data len(%d != %d) is wrong!\n", i, len);
			return;
		} else {
			dump_hex_ascii((const unsigned char *)w_buf, (unsigned int)len);
		}
		memset(req.data, 0, IPMI_MAX_DATA_LENGTH);
		memcpy(req.data, w_buf, len);
		free(w_buf);
		w_buf = NULL;
	} else
		len = 0;

	req.netfn = (unsigned char)atoi((const char *)value_netfn);
	req.cmd   = (unsigned char)atoi((const char *)value_cmd);
	req.data_len = (unsigned short)len;

	sa = (unsigned char)atoi((const char *)value_src_addr);
	int_sa = (unsigned int)sa;

	jrpc_ipmb_handle_request_cmd(dest_msg, &req, header, IPMI_DFLT_TIMEOUT_MS, int_sa);
}

/**
  *  @brief serial message handle
  *
  *  @param[out] buffer output json rpc buffer
  *  @param[in] request request
  *  @param[in] header JSON RPC header include some information
  *  @return
  */
static void jrpc_serial_handle_request_cmd(char *dest_msg, struct ipmi_msg *req, ipmi_json_ipc_header_t header,
						...)
{
	int i = -1;
	va_list args;

	va_start(args, header);
	i = find_handle(JSON_RPC_IPMI_CMD_TYPE_SERIAL/*, req->netfn, req->cmd*/);
	if (i != -1) {
		handle_table[i].handle(dest_msg, req, header, &args);
	} else
		rmm_log(ERROR, "\nNot support request!!! type[%d] netfn[%d] cmd[%d]\n", JSON_RPC_IPMI_CMD_TYPE_SERIAL, req->netfn, req->cmd);

	va_end(args);
}

/**
  *  @brief serial request message parse
  *
  *  @param[out] buffer output json rpc buffer
  *  @param[in] json_obj json object
  *  @param[in] header JSON RPC header include some information
  *  @return
  */
static void jrpc_serail_req_parse(char *dest_msg, json_t *json_obj, ipmi_json_ipc_header_t header)
{
	struct ipmi_msg req = {0};
	unsigned char serial_flag = 0;
	unsigned char serial_fd = 0;

	char value_netfn[MAX_KEY_NAME] = {0};
	char value_cmd[MAX_KEY_NAME] = {0};
	char value_data_len[MAX_KEY_NAME] = {0};
	char value_ser_flag[MAX_KEY_NAME] = {0};
	char value_ser_fd[MAX_KEY_NAME] = {0};
	char value_data[IPMI_JSONRPC_MAX_STRING_LEN] = {0};
	int len = 0;
	char *w_buf = NULL;
	int i = 0;

	if (NULL == json_obj) {
		rmm_log(ERROR, "rmcp_req_handle json_obj == NULL\n");
		return;
	}

	if (jrpc_get_key_value(json_obj, (char **)&value_netfn, STR_PARAMS, STR_NETFN)) {
		rmm_log(ERROR, "jrpc_get_netfn failed.\n");
		return;
	} else if (jrpc_get_key_value(json_obj, (char **)&value_cmd, STR_PARAMS, STR_CMD)) {
		rmm_log(ERROR, "jrpc_get_cmd failed.\n");
		return;
	} else if (jrpc_get_key_value(json_obj, (char **)&value_data_len, STR_PARAMS, STR_DATA_LEN)) {
		rmm_log(ERROR, "jrpc_get_data_len failed.\n");
		return;
	} else if (jrpc_get_key_value(json_obj, (char **)&value_ser_flag, STR_PARAMS, STR_SERIAL_FLAG)) {
		rmm_log(ERROR, "jrpc_get_serial_flag failed.\n");
		return;
	} else if ((atoi((const char *)value_data_len) > 0) && (jrpc_get_key_value(json_obj, (char **)&value_data, STR_PARAMS, STR_DATA))) {
		rmm_log(ERROR, "jrpc_get_data failed.\n");
		return;
	}

	if (0 == strcmp((char *)header.method, JSON_RPC_SERIAL_CMD_REQ)) {
		if (jrpc_get_key_value(json_obj, (char **)&value_ser_fd, STR_PARAMS, STR_SERIAL_FD)) {
			rmm_log(ERROR, "jrpc_get_serial_fd failed.\n");
			return;
		}
	}

	rmm_log(DBG, "netfn[%s] cmd[%s] serial_flag[%s] serial_fd[%s] len[%s]:\n", value_netfn, value_cmd, value_ser_flag, value_ser_fd, value_data_len);

	i = (int)atoi((const char *)value_data_len);
	if (i > 0) {
		/*w_buf = base64_decode(value_data, strlen(value_data), &len);*/
		w_buf = malloc(strlen(value_data));
		if (w_buf == NULL) {
			rmm_log(ERROR, "%s: w_buff is NULL\n", __func__);
			return;
		}
		len = hexstr2buf(value_data, strlen(value_data), w_buf);

		if (i != (int)len) {
			rmm_log(ERROR, "data len(%d != %d) is wrong!\n", i, len);
			return;
		} else {
			dump_hex_ascii((const unsigned char *)w_buf, (unsigned int)len);
		}
		memset(req.data, 0, IPMI_MAX_DATA_LENGTH);
		memcpy(req.data, w_buf, len);
		free(w_buf);
		w_buf = NULL;
	} else
		len = 0;

	req.netfn = (unsigned char)atoi((const char *)value_netfn);
	req.cmd   = (unsigned char)atoi((const char *)value_cmd);
	req.data_len = (unsigned short)len;

	req.union_app_req.serial.serial_flag = (unsigned char)atoi((const char *)value_ser_flag);
	if (0 == strcmp((char *)header.method, JSON_RPC_SERIAL_CMD_REQ))
		req.union_app_req.serial.serial_fd = (unsigned char)atoi((const char *)value_ser_fd);

	jrpc_serial_handle_request_cmd(dest_msg, &req, header/*, dummy_arg*/);
}

/**
  *  @brief RMCP bridge message handle
  *
  *  @param[out] buffer output json rpc buffer
  *  @param[in] request request
  *  @param[in] header JSON RPC header include some information
  *  @return
  */
static void jrpc_rmcp_br_handle_request_cmd(char *dest_msg, struct ipmi_msg *request, ipmi_json_ipc_header_t header, ...)
{
	int i = -1;
	va_list args;

	va_start(args, header);
	i = find_handle(JSON_RPC_IPMI_CMD_TYPE_RMCP_BR/*, request->netfn, request->cmd*/);
	if (i != -1) {
		handle_table[i].handle(dest_msg, request, header, &args/*host, port, target_addr, target_ch, uname, passwd, transit_addr, transit_ch, bridge_level*/);
	} else
		rmm_log(ERROR, "\nNot support request!!! type[%d] netfn[%d] cmd[%d]\n", JSON_RPC_IPMI_CMD_TYPE_RMCP_BR, request->netfn, request->cmd);

	va_end(args);
}

/**
  *  @brief RMCP bridge message handle
  *
  *  @param[out] buffer output json rpc buffer
  *  @param[in] json_obj json object
  *  @param[in] header JSON RPC header include some information
  *  @return
  */
static void jrpc_rmcp_br_req_parse(char *dest_msg, json_t *json_obj, ipmi_json_ipc_header_t header)
{
	struct ipmi_msg req = {0};
	unsigned int ip = 0;
	unsigned int port = 0;
	unsigned long transit_addr = 0;
	unsigned int int_transit_ch = 0;
	unsigned long target_addr = 0;
	unsigned int int_target_ch = 0;
	int bridge_level = 0;

	char value_netfn[MAX_KEY_NAME] = {0};
	char value_cmd[MAX_KEY_NAME] = {0};
	char value_data_len[MAX_KEY_NAME] = {0};
	char value_target_ip[MAX_KEY_NAME] = {0};
	char value_target_port[MAX_KEY_NAME] = {0};
	char value_target_addr[MAX_KEY_NAME] = {0};
	char value_target_ch[MAX_KEY_NAME] = {0};
	char value_transit_addr[MAX_KEY_NAME] = {0};
	char value_transit_ch[MAX_KEY_NAME] = {0};
	char value_data[IPMI_JSONRPC_MAX_STRING_LEN] = {0};
	char value_uname[MAX_KEY_NAME] = {0};
	char value_passwd[MAX_KEY_NAME] = {0};
	char value_bridge_level[MAX_KEY_NAME] = {0};
	int len = 0;
	char *w_buf = NULL;
	int i = 0;

	if (NULL == json_obj) {
		rmm_log(ERROR, "rmcp_req_handle json_obj == NULL\n");
		return;
	}

	if (jrpc_get_key_value(json_obj, (char **)&value_netfn, STR_PARAMS, STR_NETFN)) {
		rmm_log(ERROR, "jrpc_get_netfn failed.\n");
		return;
	} else if (jrpc_get_key_value(json_obj, (char **)&value_cmd, STR_PARAMS, STR_CMD)) {
		rmm_log(ERROR, "jrpc_get_cmd failed.\n");
		return;
	} else if (jrpc_get_key_value(json_obj, (char **)&value_data_len, STR_PARAMS, STR_DATA_LEN)) {
		rmm_log(ERROR, "jrpc_get_data_len failed.\n");
		return;
	} else if (jrpc_get_key_value(json_obj, (char **)&value_target_ip, STR_PARAMS, STR_TARGET_IP)) {
		rmm_log(ERROR, "jrpc_get_target_ip failed.\n");
		return;
	} else if (jrpc_get_key_value(json_obj, (char **)&value_target_port, STR_PARAMS, STR_TARGET_PORT)) {
		rmm_log(ERROR, "jrpc_get_target_port failed.\n");
		return;
	} else if (jrpc_get_key_value(json_obj, (char **)&value_transit_addr, STR_PARAMS, STR_TRANSIT_ADDR)) {
		rmm_log(ERROR, "jrpc_get_target_addr failed.\n");
		return;
	} else if (jrpc_get_key_value(json_obj, (char **)&value_transit_ch, STR_PARAMS, STR_TRANSIT_CH)) {
		rmm_log(ERROR, "jrpc_get_transit_ch failed.\n");
		return;
	} else if (jrpc_get_key_value(json_obj, (char **)&value_target_addr, STR_PARAMS, STR_TARGET_ADDR)) {
		rmm_log(ERROR, "jrpc_get_target_addr failed.\n");
		return;
	} else if (jrpc_get_key_value(json_obj, (char **)&value_target_ch, STR_PARAMS, STR_TARGET_CH)) {
		rmm_log(ERROR, "jrpc_get_target_ch failed.\n");
		return;
	} else if ((atoi((const char *)value_data_len) > 0) && (jrpc_get_key_value(json_obj, (char **)&value_data, STR_PARAMS, STR_DATA))) {
		rmm_log(ERROR, "jrpc_get_data failed.\n");
		return;
	} else if (jrpc_get_key_value(json_obj, (char **)&value_uname, STR_PARAMS, STR_NAME)) {
		rmm_log(ERROR, "jrpc_get_uname failed.\n");
		return;
	} else if (jrpc_get_key_value(json_obj, (char **)&value_passwd, STR_PARAMS, STR_PASSWD)) {
		rmm_log(ERROR, "jrpc_get_passwd failed.\n");
		return;
	} else if (jrpc_get_key_value(json_obj, (char **)&value_bridge_level, STR_PARAMS, STR_BRIDGE_LEVEL)) {
		rmm_log(ERROR, "jrpc_get_bridge_level failed.\n");
		return;
	} else
		rmm_log(DBG, "netfn[%s] cmd[%s] ip[%s] addr[%s] chan[%s] len[%s]:\n",
			value_netfn, value_cmd, value_target_ip, value_target_addr, value_target_ch, value_data_len);

	i = (int)atoi((const char *)value_data_len);
	if (i > 0) {
		/*w_buf = base64_decode(value_data, strlen(value_data), &len);*/
		w_buf = malloc(strlen(value_data));
		if (w_buf == NULL) {
			rmm_log(ERROR, "%s: w_buff is NULL\n", __func__);
			return;
		}
		len = hexstr2buf(value_data, strlen(value_data), w_buf);

		if (i != (int)len) {
			rmm_log(ERROR, "data len(%d != %d) is wrong!\n", i, len);
			return;
		} else {
			dump_hex_ascii((const unsigned char *)w_buf, (unsigned int)len);
		}
		memset(req.data, 0, IPMI_MAX_DATA_LENGTH);
		memcpy(req.data, w_buf, len);
		free(w_buf);
		w_buf = NULL;
	} else
		len = 0;


	req.netfn = (unsigned char)atoi((const char *)value_netfn);
	req.cmd   = (unsigned char)atoi((const char *)value_cmd);
	req.data_len = (unsigned short)len;
	ip = (unsigned int)inet_addr((const char *)value_target_ip);
	port = htons((unsigned int)atoi((const char *)value_target_port));
	transit_addr = (unsigned long)atoi((const char *)value_transit_addr);
	int_transit_ch = (unsigned int)atoi((const char *)value_transit_ch);
	target_addr = (unsigned long)atoi((const char *)value_target_addr);
	int_target_ch = (unsigned int)atoi((const char *)value_target_ch);
	bridge_level = atoi((const char *)value_bridge_level);

	jrpc_rmcp_br_handle_request_cmd(dest_msg, &req, header, ip, port, target_addr, int_target_ch, value_uname, value_passwd, transit_addr, int_transit_ch, bridge_level);

}



/**
  *  @brief receive socket packets from applications, parse different msg
  *
  *  @param[in] fd socket fd receive data from applications
  *  @param[out] dest_msg decode json rpc, get the raw ipmi data
  *  @return
  */
void app_json_rpc_msg(int fd, char *dest_msg)
{
	int rc;
	struct sockaddr_in addr;
	socklen_t addr_len = sizeof(addr);
	ipmi_json_ipc_header_t header = {0};
	char buffer[IPMI_JSONRPC_MAX_STRING_LEN] = {0};
	json_t *parsed_req = NULL;
	jrpc_req_type_t req_type = JSONRPC_REQ_INVALID;
	char *method = NULL;
	jrpc_id_type_t id_type = JSONRPC_ID_TYPE_INVALID;
	int64 id = 0;
	char value_rsp_type[MAX_KEY_NAME] = {0};

	if (fd > 0) {
		rc = recvfrom(fd, &buffer, sizeof(buffer), 0, (struct sockaddr *)&addr, (socklen_t *)&addr_len);
		if (rc <= 0)
			return;

		rmm_log(DBG, "Receive %d data from app %s:%d\nbuffer is %s.\n", rc, inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), buffer);
		if (jrpc_parse_req(buffer, &parsed_req, &req_type)) {
			rmm_log(ERROR, "parsed_req failed.\n");
			return;
		} else if (jrpc_get_method(parsed_req, &method)) {
			rmm_log(ERROR, "jrpc_get_method failed.\n");
			return;
		} else if (jrpc_get_id(parsed_req, &id_type, &id)) {
			rmm_log(ERROR, "jrpc_get_id failed.\n");
			return;
		} else if (jrpc_get_key_value(parsed_req, (char **)&value_rsp_type, STR_PARAMS, STR_RSP_TYPE)) {
			rmm_log(ERROR, "jrpc_get_rsp_type failed.\n");
			return;
		} else
			rmm_log(DBG, "id[%lld]  method[%s] rsp_type[%s]\n", id, method, value_rsp_type);

		header.ip = (uint32)ntohl(addr.sin_addr.s_addr);
		header.port = (uint16)ntohs(addr.sin_port);
		header.json_ipc_id = id;

		/** handle app packets */
		if (0 == strcmp((char *)value_rsp_type, STR_RSP_TYPE_UDP)) {
			if (0 == strcmp((char *)method, JSON_RPC_IPMB_REQ)) {
				memcpy(header.method, JSON_RPC_IPMB_REQ, sizeof(JSON_RPC_IPMB_REQ));
				jrpc_ipmb_req_parse(dest_msg, parsed_req, header);
			} else if (0 == strcmp((char *)method, JSON_RPC_RMCP_REQ)) {
				memcpy(header.method, JSON_RPC_RMCP_REQ, sizeof(JSON_RPC_RMCP_REQ));
				jrpc_rmcp_req_parse(dest_msg, parsed_req, header);
			} else if (0 == strcmp((char *)method, JSON_RPC_SERIAL_OP_REQ)) {
				memcpy(header.method, JSON_RPC_SERIAL_OP_REQ, sizeof(JSON_RPC_SERIAL_OP_REQ));
				jrpc_serail_req_parse(dest_msg, parsed_req, header);
			} else if (0 == strcmp((char *)method, JSON_RPC_SERIAL_CMD_REQ)) {
				memcpy(header.method, JSON_RPC_SERIAL_CMD_REQ, sizeof(JSON_RPC_SERIAL_CMD_REQ));
				jrpc_serail_req_parse(dest_msg, parsed_req, header);
			} else if (0 == strcmp((char *)method, JSON_RPC_RMCP_BR_REQ)) {
				memcpy(header.method, JSON_RPC_RMCP_BR_REQ, sizeof(JSON_RPC_RMCP_BR_REQ));
				jrpc_rmcp_br_req_parse(dest_msg, parsed_req, header);
			}
		}

		if (parsed_req)
			json_free(parsed_req);
	}
}

/**
  *  @brief parse json msg
  *
  *  @param[in] list
  *  @param[out] dest_msg decode json rpc, get the raw ipmi data
  *  @return
  */
void app_json_parse(struct app_recv_msg *list, char *dest_msg)
{
	ipmi_json_ipc_header_t header = {0};
	char *buffer = NULL;
	json_t *parsed_req = NULL;
	jrpc_req_type_t req_type = JSONRPC_REQ_INVALID;
	char *method = NULL;
	jrpc_id_type_t id_type = JSONRPC_ID_TYPE_INVALID;
	int64 id = 0;
	char value_rsp_type[MAX_KEY_NAME] = {0};

	if ((list != NULL) && (dest_msg != NULL)) {
		buffer = (char *)&list->appmsg;

		if (jrpc_parse_req(buffer, &parsed_req, &req_type)) {
			rmm_log(ERROR, "parsed_req failed.\n");
			return;
		} else if (jrpc_get_method(parsed_req, &method)) {
			rmm_log(ERROR, "jrpc_get_method failed.\n");
			return;
		} else if (jrpc_get_id(parsed_req, &id_type, &id)) {
			rmm_log(ERROR, "jrpc_get_id failed.\n");
			return;
		} else if (jrpc_get_key_value(parsed_req, (char **)&value_rsp_type, STR_PARAMS, STR_RSP_TYPE)) {
			rmm_log(ERROR, "jrpc_get_rsp_type failed.\n");
			return;
		} else
			rmm_log(DBG, "id[%lld]  method[%s] rsp_type[%s]\n", id, method, value_rsp_type);

		header.ip = (uint32)ntohl(list->addr.sin_addr.s_addr);
		header.port = (uint16)ntohs(list->addr.sin_port);
		header.json_ipc_id = id;

		/** handle app packets */
		if (0 == strcmp((char *)value_rsp_type, STR_RSP_TYPE_UDP)) {
			if (0 == strcmp((char *)method, JSON_RPC_IPMB_REQ)) {
				memcpy(header.method, JSON_RPC_IPMB_REQ, sizeof(JSON_RPC_IPMB_REQ));
				jrpc_ipmb_req_parse(dest_msg, parsed_req, header);
			} else if (0 == strcmp((char *)method, JSON_RPC_RMCP_REQ)) {
				memcpy(header.method, JSON_RPC_RMCP_REQ, sizeof(JSON_RPC_RMCP_REQ));
				jrpc_rmcp_req_parse(dest_msg, parsed_req, header);
			} else if (0 == strcmp((char *)method, JSON_RPC_SERIAL_OP_REQ)) {
				memcpy(header.method, JSON_RPC_SERIAL_OP_REQ, sizeof(JSON_RPC_SERIAL_OP_REQ));
				jrpc_serail_req_parse(dest_msg, parsed_req, header);
			} else if (0 == strcmp((char *)method, JSON_RPC_SERIAL_CMD_REQ)) {
				memcpy(header.method, JSON_RPC_SERIAL_CMD_REQ, sizeof(JSON_RPC_SERIAL_CMD_REQ));
				jrpc_serail_req_parse(dest_msg, parsed_req, header);
			} else if (0 == strcmp((char *)method, JSON_RPC_RMCP_BR_REQ)) {
				memcpy(header.method, JSON_RPC_RMCP_BR_REQ, sizeof(JSON_RPC_RMCP_BR_REQ));
				jrpc_rmcp_br_req_parse(dest_msg, parsed_req, header);
			}
		}

		if (parsed_req)
			json_free(parsed_req);
	}
	else
		rmm_log(ERROR, "Input is NULL.\n");
}

/**
  *  @brief format the IPMI response data to JSON RPC format
  *
  *  @param[out] buffer output json rpc buffer
  *  @param[in] header json rpc header
  *  @param[in] msg ipmi msg
  *  @return
  *  @retval -1 failure
  *  @retval > 0 the output buffer length
  */
int format_app_jrpc_to_user(unsigned char *buffer, ipmi_json_ipc_header_t header, const struct app_msg_hdr *msg)
{
	int str_len = 0;
	char str_ip[16] = {0};
	struct        ipmi_msg *ipmi_mesg;
	struct appmsg_ipmi_msg *ipmi_info;

	ipmi_info = (struct appmsg_ipmi_msg *)msg->data;
	if (msg->datalen != APPMSG_IPMI_MSG_LEN(ipmi_info->msg.data_len)) {
		rmm_log(ERROR, "Invalid IPMI appmsg!\n");
		return -1;
	}

	ipmi_mesg = &ipmi_info->msg;

	if (buffer == NULL) {
		rmm_log(ERROR, "buffer is NULL!\n");
		return -2;
	} else
		memset(buffer, 0, IPMI_JSONRPC_MAX_STRING_LEN);

	if (0 == (str_len = jrpc_string_build(header.method, header.json_ipc_id,
				ipmi_mesg->data, ipmi_mesg->data_len, buffer,
				IPMI_JSONRPC_MAX_STRING_LEN))){
		rmm_log(ERROR, "jrpc_string_build len is 0\n");
		return -3;
	}

	snprintf(str_ip, sizeof(str_ip), "%d.%d.%d.%d", header.ip&0xff, (header.ip>>8)&0xff, (header.ip>>16)&0xff, (header.ip>>24)&0xff);
	rmm_log(DBG, "Send data to app %s:%d:\n",  str_ip, header.port);
	rmm_log(DBG, "id[%lld] method[%s] len[%d]\n", header.json_ipc_id, header.method, (unsigned int)ipmi_mesg->data_len);

	dump_hex_ascii((const unsigned char *)ipmi_mesg->data, (unsigned int)ipmi_mesg->data_len);
	dump_hex_ascii((const unsigned char *)buffer, (unsigned int)str_len);

	return str_len;
}


