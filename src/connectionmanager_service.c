/* @@@LICENSE
*
*      Copyright (c) 2012-2013 LG Electronics, Inc.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
* LICENSE@@@ */

/**
 * @file  connectionmanager_service.c
 *
 * @brief Implements all of the com.palm.connectionmanager methods using connman APIs
 * in the backend
 *
 */

//->Start of API documentation comment block
/**
@page com_webos_connectionmanager com.webos.connectionmanager

@brief This service provides overall management of network connections.

Each call has a standard return in the case of a failure, as follows:

Name | Required | Type | Description
-----|--------|------|----------
returnValue | yes | Boolean | False to inidicate an error
errorCode | Yes | Integer | Error code
errorText | Yes | String | Error description

@{
@}
*/
//->End of API documentation comment block

#include <glib.h>
#include <stdbool.h>
#include <time.h>
#include <string.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <linux/if.h>
#include <pbnjson.h>

#include "common.h"
#include "connman_manager.h"
#include "connectionmanager_service.h"
#include "lunaservice_utils.h"
#include "logging.h"

static LSHandle *pLsHandle, *pLsPublicHandle;

/**
 * @brief Fill in information about the system's connection status
 *
 * @param status
 */

static void update_connection_status(connman_service_t *connected_service, jvalue_ref *status)
{
	if(NULL == connected_service || NULL == status)
		return;

	int connman_state = 0;
	connman_state = connman_service_get_state(connected_service->state);
	if(connman_state == CONNMAN_SERVICE_STATE_ONLINE
		|| connman_state == CONNMAN_SERVICE_STATE_READY)
	{
		connman_service_get_ipinfo(connected_service);

		jobject_put(*status, J_CSTR_TO_JVAL("state"), jstring_create("connected"));
		if(NULL != connected_service->ipinfo.iface)
			jobject_put(*status, J_CSTR_TO_JVAL("interfaceName"), jstring_create(connected_service->ipinfo.iface));
		if(NULL != connected_service->ipinfo.ipv4.address)
			jobject_put(*status, J_CSTR_TO_JVAL("ipAddress"), jstring_create(connected_service->ipinfo.ipv4.address));
		if(NULL != connected_service->ipinfo.ipv4.netmask)
			jobject_put(*status, J_CSTR_TO_JVAL("netmask"), jstring_create(connected_service->ipinfo.ipv4.netmask));
		if(NULL != connected_service->ipinfo.ipv4.gateway)
			jobject_put(*status, J_CSTR_TO_JVAL("gateway"), jstring_create(connected_service->ipinfo.ipv4.gateway));

		gsize i;
		char dns_str[16];
		for (i = 0; i < g_strv_length(connected_service->ipinfo.dns); i++)
		{
			sprintf(dns_str,"dns%d",i+1);
			jobject_put(*status, jstring_create(dns_str), jstring_create(connected_service->ipinfo.dns[i]));
		}

		if(NULL != connected_service->ipinfo.ipv4.method)
			jobject_put(*status, J_CSTR_TO_JVAL("method"), jstring_create(connected_service->ipinfo.ipv4.method));

		if(connman_service_type_wifi(connected_service))
		{
			if(NULL != connected_service->name)
				jobject_put(*status, J_CSTR_TO_JVAL("ssid"), jstring_create(connected_service->name));
			jobject_put(*status, J_CSTR_TO_JVAL("isWakeOnWifiEnabled"), jboolean_create(false));
			jobject_put(*status, J_CSTR_TO_JVAL("signalLevel"), jnumber_create_i32(connected_service->strength));
		}
		const char *s = (connman_state == CONNMAN_SERVICE_STATE_ONLINE)?"yes":"no";
		jobject_put(*status, J_CSTR_TO_JVAL("onInternet"), jstring_create(s));

		/* unless we have something to determine the confidence level we always
		 * provide execellent as proper default. */
		jobject_put(*status, J_CSTR_TO_JVAL("networkConfidenceLevel"), jstring_create("excellent"));
	}
	else
		jobject_put(*status, J_CSTR_TO_JVAL("state"), jstring_create("disconnected"));

}

/**
 * @brief Fill in all the status information to be sent with 'getstatus' method
 */

static void send_connection_status(jvalue_ref *reply)
{
        if(NULL == reply)
                return;

	gboolean online = connman_manager_is_manager_online(manager);
	jobject_put(*reply, J_CSTR_TO_JVAL("isInternetConnectionAvailable"), jboolean_create(online));
	jobject_put(*reply, J_CSTR_TO_JVAL("offlineMode"), jstring_create(!connman_manager_is_manager_available(manager) ? "enabled" : "disabled"));

	jvalue_ref connected_wired_status = jobject_create();
	jvalue_ref disconnected_wired_status = jobject_create();
	jvalue_ref connected_wifi_status = jobject_create();
	jvalue_ref disconnected_wifi_status = jobject_create();
	jvalue_ref connected_cellular_status = jobject_create();
	jvalue_ref disconnected_cellular_status = jobject_create();

	jobject_put(disconnected_wired_status, J_CSTR_TO_JVAL("state"), jstring_create("disconnected"));
	jobject_put(disconnected_wifi_status, J_CSTR_TO_JVAL("state"), jstring_create("disconnected"));
	jobject_put(disconnected_cellular_status, J_CSTR_TO_JVAL("state"), jstring_create("disconnected"));

	/* Get the service which is connecting or already in connected state */
	connman_service_t *connected_wired_service = connman_manager_get_connected_service(manager->wired_services);
	if(NULL != connected_wired_service)
	{
		update_connection_status(connected_wired_service, &connected_wired_status);
		jobject_put(*reply, J_CSTR_TO_JVAL("wired"), connected_wired_status);
		j_release(&disconnected_wired_status);
	}
	else
	{
		jobject_put(*reply, J_CSTR_TO_JVAL("wired"), disconnected_wired_status);
		j_release(&connected_wired_status);
	}

	connman_service_t *connected_wifi_service = connman_manager_get_connected_service(manager->wifi_services);
	if(NULL != connected_wifi_service)
	{
		update_connection_status(connected_wifi_service, &connected_wifi_status);
		jobject_put(*reply, J_CSTR_TO_JVAL("wifi"), connected_wifi_status);
		j_release(&disconnected_wifi_status);
	}
	else
	{
		jobject_put(*reply, J_CSTR_TO_JVAL("wifi"), disconnected_wifi_status);
		j_release(&connected_wifi_status);
	}

	connman_service_t *connected_cellular_service = connman_manager_get_connected_service(manager->cellular_services);
	if (NULL != connected_cellular_service)
	{
		update_connection_status(connected_cellular_service, &connected_cellular_status);
		jobject_put(*reply, J_CSTR_TO_JVAL("cellular"), connected_cellular_status);
		j_release(&disconnected_cellular_status);
	}
	else
	{
		jobject_put(*reply, J_CSTR_TO_JVAL("cellular"), disconnected_cellular_status);
		j_release(&connected_cellular_status);
	}

}


/**
 *  @brief Callback function registered with connman manager whenever any of its properties change
 *
 */

void connectionmanager_send_status(void)
{
	jvalue_ref reply = jobject_create();
	jobject_put(reply, J_CSTR_TO_JVAL("returnValue"), jboolean_create(true));

	if (manager)
		send_connection_status(&reply);

	jobject_put(reply, J_CSTR_TO_JVAL("returnValue"), jboolean_create(true));

	jschema_ref response_schema = jschema_parse (j_cstr_to_buffer("{}"), DOMOPT_NOOPT, NULL);
	if(response_schema)
	{
		const char *payload = jvalue_tostring(reply, response_schema);
		LSError lserror;
		LSErrorInit(&lserror);
		WCA_LOG_INFO("Sending payload %s",payload);
		if (!LSSubscriptionPost(pLsHandle, "/", "getstatus", payload, &lserror))
		{
			LSErrorPrint(&lserror, stderr);
			LSErrorFree(&lserror);
		}
		if (!LSSubscriptionPost(pLsHandle, "/", "getStatus", payload, &lserror))
		{
			LSErrorPrint(&lserror, stderr);
			LSErrorFree(&lserror);
		}
		jschema_release(&response_schema);
	}
	j_release(&reply);
}


//->Start of API documentation comment block
/**
@page com_webos_connectionmanager com.webos.connectionmanager
@{
@section com_webos_connectionmanager_getstatus getstatus

Gets the current status of network connections (both wifi and wired) on the system.

Callers of this method can subscribe to it so that they are notified whenever the
network status changes.

@par Parameters
Name | Required | Type | Description
-----|--------|------|----------
subcribe | no | Boolean | Subscribe to this method

@par Returns(Call)
Name | Required | Type | Description
-----|--------|------|----------
returnValue | yes | Boolean | True
isInternetConnectionAvailable | Yes | Boolean | Indicates if any internet connection is available
wired | yes | Object | State of wired connection (see below)
wifi | yes | Object | State of wifi connection (see below)

@par "wired" State Object
Optional fields are only present if "state" is "connected"
Name | Required | Type | Description
-----|--------|------|----------
state | yes | String | "connected" or "disconnected" to indicate status.
interfaceName | no | String | Interface name in use (e.g. "eth0")
ipAddress | no | String | IP address associated with the connection
netmask | no | String | Net mask value for the connection
gateway | no | String | IP address of network gateway
dns<n> | no | String | List of IP Addreses of dns servers for this connection
method | no | String | How the IP addressed was assigned (e.g. "Manual", "dhcp")
onInternet | no | String | "yes" or "no" to indicate if the service is "online"

@par "wifi" State Object
Optional fields are only present if "state" is "connected"
Name | Required | Type | Description
-----|--------|------|----------
state | yes | String | "connected" or "disconnected" to indicate status.
interfaceName | no | String | Interface name in use (e.g. "eth0")
ipAddress | no | String | IP address associated with the connection
netmask | no | String | Net mask value for the connection
gateway | no | String | IP address of network gateway
dns<n> | no | String | List of IP Addreses of dns servers for this connection
method | no | String | How the IP addressed was assigned (e.g. "Manual", "dhcp")
ssid | no | String | SSID of the connected service (if known)
isWakeOnWiFiEnabled | no | Boolean | True if "Wake on WIFI" is enabled
onInternet | no | String | "yes" or "no" to indicate if the service is "online"

@par Returns(Subscription)
The subcription update contains the same information as the initial call.

@}
*/
//->End of API documentation comment block

/**
 *  @brief Handler for "getstatus" command.
 *  Get the current network connection status on the system, both wifi and wired
 *
 *  JSON format:
 *
 *  luna://com.palm.connectionmanager/getstatus {}
 *  luna://com.palm.connectionmanager/getstatus {"subscribed":true}
 *
 *  @param sh
 *  @param message
 *  @param context
 */

static bool handle_get_status_command(LSHandle* sh, LSMessage *message, void* context)
{

	jvalue_ref reply = jobject_create();
	LSError lserror;
	LSErrorInit(&lserror);
	bool subscribed = false;

	if (LSMessageIsSubscription(message))
	{
		if (!LSSubscriptionProcess(sh, message, &subscribed, &lserror))
		{
			LSErrorPrint(&lserror, stderr);
			LSErrorFree(&lserror);
		}
		jobject_put(reply, J_CSTR_TO_JVAL("subscribed"), jboolean_create(subscribed));
	}

	if (manager)
		send_connection_status(&reply);

	jobject_put(reply, J_CSTR_TO_JVAL("returnValue"), jboolean_create(true));

response:
	{
		jschema_ref response_schema = jschema_parse (j_cstr_to_buffer("{}"), DOMOPT_NOOPT, NULL);
		if(!response_schema)
		{
			LSMessageReplyErrorUnknown(sh,message);
			goto cleanup;
		}
		if (!LSMessageReply(sh, message, jvalue_tostring(reply, response_schema), &lserror)) {
			LSErrorPrint(&lserror, stderr);
			LSErrorFree(&lserror);
		}
		jschema_release(&response_schema);
	}
cleanup:
	j_release(&reply);
	return true;
}

/**
 * @brief Loop through the manager's wifi services and match the one with the given ssid
 * If 'ssid' is NULL then return the wired service on the system
 *
 * @param ssid
 */


static connman_service_t *get_connman_service(gchar *ssid)
{
	if(NULL != ssid)
	{
		GSList *ap;

		/* Look up for the service with the given ssid */
		for (ap = manager->wifi_services; ap; ap = ap->next)
		{
			connman_service_t *service = (connman_service_t *)(ap->data);
			if((NULL != service->name) && g_str_equal(service->name, ssid))
			{
				return service;
			}
		}
	}
	else
	{
		GSList *ap;
		/* Return the first wired service (there will be just one on most systems) */
		for (ap = manager->wired_services; ap; ap = ap->next)
		{
			connman_service_t *service = (connman_service_t *)(ap->data);
			return service;

		}

	}
	return NULL;
}

//->Start of API documentation comment block
/**
@page com_webos_connectionmanager com.webos.connectionmanager
@{
@section com_webos_connectionmanager_setipv4 setipv4

Modify the parameters of an IPv4 connection (wired or WIFI)

If an SSID field is not provided in the request, the modifications are
applied to the wired connection.

@par Parameters
Name | Required | Type | Description
-----|--------|------|----------
"method" | yes | String | "dhcp" or "manual"
"address" | no | String | If specified, sets a new IP address
"netmask" | no | String | If specified, sets a new netmask
"gateway" | no | String | If specified, sets a new gateway IP address
"ssid" | no | String | Select the WIFI connection to modify. If absent, the wired connection is changed.

@par Returns(Call)
Name | Required | Type | Description
-----|--------|------|----------
returnValue | yes | Boolean | True

@par Returns(Subscription)
None

@}
*/
//->End of API documentation comment block

/**
 *  @brief Handler for "setipv4" command.
 *  Change the ipv4 properties for the given wifi ssid or for the wired connection
 *
 *  JSON format:
 *
 *  luna://com.palm.connectionmanager/setipv4 '{"method":"<dhcp/manual>","address":"<new address>",
 *		"netmask":"<new netmask>","gateway":<"new gateway">,"ssid":"<ssid value>"}'
 *
 *  @param sh
 *  @param message
 *  @param context
 */


static bool handle_set_ipv4_command(LSHandle *sh, LSMessage *message, void* context)
{
	if(!connman_status_check(manager, sh, message))
		return true;

	jvalue_ref parsedObj = {0};
	jschema_ref input_schema = jschema_parse (j_cstr_to_buffer("{}"), DOMOPT_NOOPT, NULL);
	if(!input_schema)
	return false;

	JSchemaInfo schemaInfo;
	jschema_info_init(&schemaInfo, input_schema, NULL, NULL); // no external refs & no error handlers
	parsedObj = jdom_parse(j_cstr_to_buffer(LSMessageGetPayload(message)), DOMOPT_NOOPT, &schemaInfo);
	jschema_release(&input_schema);

	if (jis_null(parsedObj))
	{
		LSMessageReplyErrorBadJSON(sh, message);
		return true;
	}

	jvalue_ref ssidObj = {0}, methodObj = {0}, addressObj = {0}, netmaskObj = {0}, gatewayObj = {0};
	ipv4info_t ipv4 = {0};
	gchar *ssid = NULL;
	gboolean invalidArg = TRUE;

	if(jobject_get_exists(parsedObj, J_CSTR_TO_BUF("method"), &methodObj))
	{
		raw_buffer method_buf = jstring_get(methodObj);
		ipv4.method = g_strdup(method_buf.m_str);
		jstring_free_buffer(method_buf);
		invalidArg = FALSE;
	}
	if(jobject_get_exists(parsedObj, J_CSTR_TO_BUF("address"), &addressObj))
	{
		raw_buffer address_buf = jstring_get(addressObj);
		ipv4.address = g_strdup(address_buf.m_str);
		jstring_free_buffer(address_buf);
		invalidArg = FALSE;
	}
	if(jobject_get_exists(parsedObj, J_CSTR_TO_BUF("netmask"), &netmaskObj))
	{
		raw_buffer netmask_buf = jstring_get(netmaskObj);
		ipv4.netmask = g_strdup(netmask_buf.m_str);
		jstring_free_buffer(netmask_buf);
		invalidArg = FALSE;
	}
	if(jobject_get_exists(parsedObj, J_CSTR_TO_BUF("gateway"), &gatewayObj))
	{
		raw_buffer gateway_buf = jstring_get(gatewayObj);
		ipv4.gateway = g_strdup(gateway_buf.m_str);
		jstring_free_buffer(gateway_buf);
		invalidArg = FALSE;
	}
	if(jobject_get_exists(parsedObj, J_CSTR_TO_BUF("ssid"), &ssidObj))
	{
		raw_buffer ssid_buf = jstring_get(ssidObj);
		ssid = g_strdup(ssid_buf.m_str);
		jstring_free_buffer(ssid_buf);
		invalidArg = FALSE;
	}
	if(invalidArg == TRUE)
	{
		LSMessageReplyErrorInvalidParams(sh, message);
		goto Exit;
	}

	connman_service_t *service = get_connman_service(ssid);
	if(NULL != service)
	{
		if(connman_service_set_ipv4(service, &ipv4))
			LSMessageReplySuccess(sh, message);
		else
			LSMessageReplyErrorUnknown(sh, message);
		goto Exit;
	}
	else
	{
		LSMessageReplyCustomError(sh, message, "Network not found");
	}

Exit:
	g_free(ipv4.method);
	g_free(ipv4.address);
	g_free(ipv4.netmask);
	g_free(ipv4.gateway);
	g_free(ssid);
	j_release(&parsedObj);
	return true;
}

//->Start of API documentation comment block
/**
@page com_webos_connectionmanager com.webos.connectionmanager
@{
@section com_webos_connectionmanager_setdns setdns

Change the DNS servers for the network.

If an SSID field is not provided in the request, the modifications are
applied to the wired connection.

@par Parameters
Name | Required | Type | Description
-----|--------|------|----------
dns | yes | Array of String | Each string provides the IP address of a dns server
ssid | no | String | SSID of WIFI connection to be modified.

@par Returns(Call)
Name | Required | Type | Description
-----|--------|------|----------
returnValue | yes | Boolean | True

@par Returns(Subscription)
None

@}
*/
//->End of API documentation comment block

/**
 *  @brief Handler for "setdns" command.
 *  Change the dns servers for the given wifi ssid or for the wired connection
 *
 *  JSON format:
 *
 *  luna://com.palm.connectionmanager/setipv4 '{"dns":"[list of dns servers]","ssid":"<ssid value>"}'
 *
 *  @param sh
 *  @param message
 *  @param context
 */


static bool handle_set_dns_command(LSHandle *sh, LSMessage *message, void* context)
{
	if(!connman_status_check(manager, sh, message))
		return true;

	jvalue_ref parsedObj = {0};
	jschema_ref input_schema = jschema_parse (j_cstr_to_buffer("{}"), DOMOPT_NOOPT, NULL);
	if(!input_schema)
		return false;

	JSchemaInfo schemaInfo;
	jschema_info_init(&schemaInfo, input_schema, NULL, NULL); // no external refs & no error handlers
	parsedObj = jdom_parse(j_cstr_to_buffer(LSMessageGetPayload(message)), DOMOPT_NOOPT, &schemaInfo);
	jschema_release(&input_schema);

	if (jis_null(parsedObj))
	{
		LSMessageReplyErrorBadJSON(sh, message);
		return true;
	}

	jvalue_ref ssidObj = {0}, dnsObj = {0};
	GStrv dns = NULL;
	gchar *ssid = NULL;

	if(jobject_get_exists(parsedObj, J_CSTR_TO_BUF("dns"), &dnsObj))
	{
		int i, dns_arrsize = jarray_size(dnsObj);
		dns = (GStrv) g_new0(GStrv, 1);
		for(i = 0; i < dns_arrsize; i++)
		{
			raw_buffer dns_buf = jstring_get(jarray_get(dnsObj, i));
			dns[i] = g_strdup(dns_buf.m_str);
			jstring_free_buffer(dns_buf);
		}
	}
	else
	{
		LSMessageReplyErrorInvalidParams(sh, message);
		goto Exit;
	}

	if(jobject_get_exists(parsedObj, J_CSTR_TO_BUF("ssid"), &ssidObj))
	{
		raw_buffer ssid_buf = jstring_get(ssidObj);
		ssid = g_strdup(ssid_buf.m_str);
		jstring_free_buffer(ssid_buf);
	}

	connman_service_t *service = get_connman_service(ssid);
	if(NULL != service)
	{
		if(connman_service_set_nameservers(service, dns))
			LSMessageReplySuccess(sh, message);
		else
			LSMessageReplyErrorUnknown(sh, message);
		goto Exit;
	}
	else
		LSMessageReplyCustomError(sh, message, "No connected network");
Exit:
	g_strfreev(dns);
	g_free(ssid);
	j_release(&parsedObj);
	return true;
}


/**
 *  @brief Returns true if wifi technology is powered on
 *
 */

static gboolean is_wifi_powered(void)
{
	connman_technology_t *technology = connman_manager_find_wifi_technology(manager);
	if(NULL != technology)
		return technology->powered;
	else
		return FALSE;
}

/**
 *  @brief Sets the wifi technologies powered state
 *
 *  @param state
 */

static gboolean set_wifi_state(bool state)
{
	return connman_technology_set_powered(connman_manager_find_wifi_technology(manager),state);
}

static gboolean set_offline_mode(bool state)
{
	return connman_manager_set_offline(manager, state);
}

/**
 *  @brief Returns true if ethernet technology is powered on
 *
 */

static gboolean is_ethernet_powered(void)
{
	connman_technology_t *technology = connman_manager_find_ethernet_technology(manager);
	if(NULL != technology)
		return technology->powered;
	else
		return FALSE;
}

/**
 *  @brief Sets the ethernet technologies powered state
 *
 *  @param state
 */

static gboolean set_ethernet_state(bool state)
{
	return connman_technology_set_powered(connman_manager_find_ethernet_technology(manager),state);
}

//->Start of API documentation comment block
/**
@page com_webos_connectionmanager com.webos.connectionmanager
@{
@section com_webos_connectionmanager_setstate setstate

Enable or disable the state of either or both wifi and wired technologies on the system

@par Parameters
Name | Required | Type | Description
-----|--------|------|----------
wifi | no | String | "enabled" or "disabled" to set status accordingly
wired | no | String | "enabled" or "disabled" to set status accordingly

@par Returns(Call)
Name | Required | Type | Description
-----|--------|------|----------
returnValue | yes | Boolean | True

@par Returns(Subscription)
None

@}
*/
//->End of API documentation comment block

/**
 *  @brief Handler for "setstate" command.
 *  Enable/disable the wifi service
 *
 *  JSON format:
 *  luna://com.palm.wifi/setstate {"wifi":"<enabled/disabled>","wired":"<enabled/disabled>"}
 *
 */

static bool handle_set_state_command(LSHandle *sh, LSMessage *message, void* context)
{
	jvalue_ref parsedObj = {0};
	jschema_ref input_schema = jschema_parse (j_cstr_to_buffer("{}"), DOMOPT_NOOPT, NULL);
	if(!input_schema)
		return false;

	JSchemaInfo schemaInfo;
	jschema_info_init(&schemaInfo, input_schema, NULL, NULL); // no external refs & no error handlers
	parsedObj = jdom_parse(j_cstr_to_buffer(LSMessageGetPayload(message)), DOMOPT_NOOPT, &schemaInfo);
	jschema_release(&input_schema);

	if (jis_null(parsedObj))
	{
		LSMessageReplyErrorBadJSON(sh, message);
		return true;
	}

	jvalue_ref wifiObj = {0}, wiredObj = {0}, offlineObj = {0};
	gboolean enable_wifi = FALSE, enable_wired = FALSE, enable_offline = FALSE;
	gboolean invalidArg = TRUE;

	if(jobject_get_exists(parsedObj, J_CSTR_TO_BUF("wifi"), &wifiObj))
	{
		if (jstring_equal2(wifiObj, J_CSTR_TO_BUF("enabled")))
		{
			enable_wifi = TRUE;
		}
		else if (jstring_equal2(wifiObj, J_CSTR_TO_BUF("disabled")))
		{
			enable_wifi = FALSE;
		}
		else
		{
			goto invalid_params;
		}
		/*
		 *  Check if we are enabling an already enabled service,
		 *  or disabling an already disabled service
		 */

		if((enable_wifi && is_wifi_powered()) || (!enable_wifi && !is_wifi_powered()))
		{
			WCA_LOG_DEBUG("Wifi technology already enabled/disabled");
		}
		else
		{
			set_wifi_state(enable_wifi);
		}
		invalidArg = FALSE;
	}

	if(jobject_get_exists(parsedObj, J_CSTR_TO_BUF("wired"), &wiredObj))
	{
		if (jstring_equal2(wiredObj, J_CSTR_TO_BUF("enabled")))
		{
			enable_wired = TRUE;
		}
		else if (jstring_equal2(wiredObj, J_CSTR_TO_BUF("disabled")))
		{
			enable_wired = FALSE;
		}
		else
		{
			goto invalid_params;
		}
		/*
		 *  Check if we are enabling an already enabled service,
		 *  or disabling an already disabled service
		 */
		if((enable_wired && is_ethernet_powered()) || (!enable_wired && !is_ethernet_powered()))
		{
			WCA_LOG_DEBUG("Wired technology already enabled/disabled");
		}
		else
		{
			set_ethernet_state(enable_wired);
		}
		invalidArg = FALSE;
	}

	if (jobject_get_exists(parsedObj, J_CSTR_TO_BUF("offlineMode"), &offlineObj))
	{
		if (jstring_equal2(offlineObj, J_CSTR_TO_BUF("enabled")))
			enable_offline = TRUE;
		else if (jstring_equal2(offlineObj, J_CSTR_TO_BUF("disabled")))
			enable_offline = FALSE;
		else
			goto invalid_params;

		gboolean offline = connman_manager_is_manager_available(manager);

		if ((enable_offline && !offline) || (!enable_offline && offline))
			WCA_LOG_DEBUG("Offline mode is already %s", enable_offline ? "enabled" : "disabled");
		else
			set_offline_mode(enable_offline);

		invalidArg = FALSE;
	}

	if(invalidArg == TRUE)
	{
		goto invalid_params;
	}

	LSMessageReplySuccess(sh,message);
	goto cleanup;

invalid_params:
	LSMessageReplyErrorInvalidParams(sh, message);
cleanup:
	j_release(&parsedObj);
	return true;

}

//->Start of API documentation comment block
/**
@page com_webos_connectionmanager com.webos.connectionmanager
@{
@section com_webos_connectionmanager_getinfo getinfo

Lists information about the WiFi and wired interface (currently just lists MAC addresses for both interfaces).

@par Parameters
None

@par Returns(Call)
Name | Required | Type | Description
-----|--------|------|----------
returnValue | yes | Boolean | True
wiredInfo | no | Object | Object containing information for the current wired connection.
wifiInfo | no | Object | Object containing information for the current wifi connection.

@par Information Object
Name | Required | Type | Description
-----|--------|------|----------
macAddress | yes | String | MAC address of the controller for the connection

@par Returns(Subscription)
None

@}
*/
//->End of API documentation comment block

/**
 * Handler for "getinfo" command.
 *
 * JSON format:
 * luna://com.palm.connectionmanager/getinfo {}
 */

static bool handle_get_info_command(LSHandle *sh, LSMessage *message, void* context)
{
	jvalue_ref reply = jobject_create();
	LSError lserror;
	LSErrorInit(&lserror);

	jobject_put(reply, J_CSTR_TO_JVAL("returnValue"), jboolean_create(true));


	connman_service_t *connected_wifi_service = connman_manager_get_connected_service(manager->wifi_services);
	const gchar *wifi_mac_address = get_service_mac_address(connected_wifi_service);
	if(wifi_mac_address != NULL)
	{
		jvalue_ref wifi_info = jobject_create();
		jobject_put(wifi_info, J_CSTR_TO_JVAL("macAddress"),jstring_create(wifi_mac_address));
		jobject_put(reply, J_CSTR_TO_JVAL("wifiInfo"), wifi_info);
	}
	else
		WCA_LOG_ERROR("Error in fetching mac address for wifi interface");


	connman_service_t *connected_wired_service = connman_manager_get_connected_service(manager->wired_services);
	const gchar *wired_mac_address = get_service_mac_address(connected_wired_service);
	if(wired_mac_address != NULL)
	{
		jvalue_ref wired_info = jobject_create();
		jobject_put(wired_info, J_CSTR_TO_JVAL("macAddress"),jstring_create(wired_mac_address));
		jobject_put(reply, J_CSTR_TO_JVAL("wiredInfo"), wired_info);
	}
	else
		WCA_LOG_ERROR("Error in fetching mac address for wired interface");

	jschema_ref response_schema = jschema_parse (j_cstr_to_buffer("{}"), DOMOPT_NOOPT, NULL);
	if(!response_schema)
	{
		LSMessageReplyErrorUnknown(sh,message);
		goto cleanup;
	}

	if (!LSMessageReply(sh, message, jvalue_tostring(reply, response_schema), &lserror))
	{
		LSErrorPrint(&lserror, stderr);
		LSErrorFree(&lserror);
	}

	jschema_release(&response_schema);

cleanup:
	if (LSErrorIsSet(&lserror))
	{
		LSErrorPrint(&lserror, stderr);
		LSErrorFree(&lserror);
	}

	j_release(&reply);
	return true;
}

/**
 *  @brief Callback function registered with connman technology whenever any of its properties change
 *
 *  @param data
 *  @param property
 *  @param value
 */

static void technology_property_changed_callback(gpointer data, const gchar *property, GVariant *value)
{
	connman_technology_t *technology = (connman_technology_t *)data;

	if(NULL == technology)
		return;

	/* Need to send getstatus method to all com.palm.connectionmanager subscribers whenever the
	   "powered" or "connected" state of the technology changes */

	if(g_str_equal(property,"Powered") || g_str_equal(property,"Connected"))
	{
		connectionmanager_send_status();
	}
}

/**
 * com.palm.connectionmanager service Luna Method Table
 */

static LSMethod connectionmanager_methods[] = {
    { LUNA_METHOD_GETSTATUS,            handle_get_status_command },
    { LUNA_METHOD_GETSTATUS2,            handle_get_status_command },
    { LUNA_METHOD_SETIPV4,              handle_set_ipv4_command },
    { LUNA_METHOD_SETDNS,               handle_set_dns_command },
    { LUNA_METHOD_SETSTATE,             handle_set_state_command },
    { LUNA_METHOD_GETINFO,		handle_get_info_command },
    { },
};

static LSMethod connectionmanager_public_methods[] = {
    { LUNA_METHOD_GETSTATUS,            handle_get_status_command },
    { LUNA_METHOD_GETSTATUS2,            handle_get_status_command },
    { },
};

static gboolean check_cellular_status_cb(gpointer user_data)
{
	connman_technology_t *technology = connman_manager_find_cellular_technology(manager);
	if (technology)
		connman_technology_register_property_changed_cb(technology, technology_property_changed_callback);
}

/**
 *  @brief Initialize com.palm.connectionmanager service and all of its methods
 *  Also initialize a manager instance
 */

int initialize_connectionmanager_ls2_calls( GMainLoop *mainloop )
{
	LSError lserror;
	LSErrorInit (&lserror);
	pLsHandle       = NULL;
	pLsPublicHandle = NULL;

	if(NULL == mainloop)
		goto Exit;

	if (LSRegisterPubPriv(CONNECTIONMANAGER_LUNA_SERVICE_NAME, &pLsHandle, false, &lserror) == false)
	{
		WCA_LOG_FATAL("LSRegister() private returned error");
		goto Exit;
	}

	if (LSRegisterPubPriv(CONNECTIONMANAGER_LUNA_SERVICE_NAME, &pLsPublicHandle, true, &lserror) == false)
	{
		WCA_LOG_FATAL("LSRegister() public returned error");
		goto Exit;
	}

	if (LSRegisterCategory(pLsHandle, NULL, connectionmanager_methods, NULL, NULL, &lserror) == false)
	{
		WCA_LOG_FATAL("LSRegisterCategory() returned error");
		goto Exit;
	}

	if (LSRegisterCategory(pLsPublicHandle, NULL, connectionmanager_public_methods, NULL, NULL, &lserror) == false)
	{
		WCA_LOG_FATAL("LSRegisterCategory() returned error");
		goto Exit;
	}

	if (LSGmainAttach(pLsHandle, mainloop, &lserror) == false)
	{
		WCA_LOG_FATAL("LSGmainAttach() private returned error");
		goto Exit;
	}

	if (LSGmainAttach(pLsPublicHandle, mainloop, &lserror) == false)
	{
		WCA_LOG_FATAL("LSGmainAttach() public returned error");
		goto Exit;
	}

	/* Register for Wired technology's "PropertyChanged" signal (For wifi its being done in wifi_service.c*/
	connman_technology_t *technology = connman_manager_find_ethernet_technology(manager);
	if(technology)
	{
		connman_technology_register_property_changed_cb(technology, technology_property_changed_callback);
	}

	technology = connman_manager_find_cellular_technology(manager);
	if (technology)
	{
		connman_technology_register_property_changed_cb(technology, technology_property_changed_callback);
	}
	else
	{
		g_timeout_add(500, check_cellular_status_cb, 0);
	}

	return 0;

Exit:
        if (LSErrorIsSet(&lserror))
	{
		LSErrorPrint(&lserror, stderr);
		LSErrorFree(&lserror);
	}

        if (pLsHandle)
	{
		LSErrorInit (&lserror);
		if(LSUnregister(pLsHandle, &lserror) == false)
		{
			LSErrorPrint(&lserror, stderr);
			LSErrorFree(&lserror);
		}
	}

        if (pLsPublicHandle)
        {
		LSErrorInit (&lserror);
		if(LSUnregister(pLsPublicHandle, &lserror) == false)
		{
			LSErrorPrint(&lserror, stderr);
			LSErrorFree(&lserror);
		}
        }
	return -1;
}
