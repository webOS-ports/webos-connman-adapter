/* @@@LICENSE
*
*      Copyright (c) 2012 Hewlett-Packard Development Company, L.P.
*      Copyright (c) 2013 Simon Busch <morphis@gravedo.de>
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
 * @file  wan_service.c
 *
 * @brief Implements all of the com.palm.wan methods using connman APIs
 * in the backend
 *
 */


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
#include "wan_service.h"
#include "lunaservice_utils.h"

static LSHandle *pLsHandle, *pLsPublicHandle;

/**
 *  @brief Returns true if cellular technology is powered on
 *
 */

static gboolean is_cellular_powered(void)
{
	connman_technology_t *technology = connman_manager_find_technology(manager, CONNMAN_TECHNOLOGY_CELLULAR);
	if(NULL != technology)
		return technology->powered;
	return FALSE;
}

/**
 *  @brief Check if the cellular technology is available
 *   Send an error luna message if its not available
 *
 *  @param sh
 *  @param message
 */

static gboolean cellular_technology_status_check(LSHandle *sh, LSMessage *message)
{
	if(NULL == connman_manager_find_technology(manager, CONNMAN_TECHNOLOGY_CELLULAR))
	{
		LSMessageReplyCustomError(sh, message, "Cellular technology unavailable");
		return false;
	}
	return true;
}

/**
 * @brief Convert connman's service status to a status code of the WAN service.
 *
 * @param service Service which status should be converted
 *
 * @return String containing the status of the service in terms of our WAN service.
 */

const char* service_to_wan_status(connman_service_t *service)
{
	switch (connman_service_get_state(service->state)) {
	case CONNMAN_SERVICE_STATE_ASSOCIATION:
	case CONNMAN_SERVICE_STATE_CONFIGURATION:
	case CONNMAN_SERVICE_STATE_READY:
		return "connecting";
	case CONNMAN_SERVICE_STATE_ONLINE:
		return "active";
	case CONNMAN_SERVICE_STATE_DISCONNECT:
	case CONNMAN_SERVICE_STATE_FAILURE:
	case CONNMAN_SERVICE_STATE_UNKNOWN:
	case CONNMAN_SERVICE_STATE_IDLE:
		return "disconnected";
	default:
		break;
	}

	return "unknown";
}

/**
 *  @brief Add details about specific provided service
 *
 *  @param service_obj JSON service object
 *  @param service Connman service object which status should be added
 *
 */

static void add_connected_service_status(jvalue_ref reply_obj, connman_service_t *service)
{
	jvalue_ref provided_services_obj;

	if (NULL == reply_obj || NULL == service)
		return;

	jobject_put(reply_obj, J_CSTR_TO_JVAL("connectstatus"),
				jstring_create(service_to_wan_status(service)));

	provided_services_obj = jarray_create(NULL);

	/* NOTE connman only supports ofono's internet context objects and no other */
	jarray_append(provided_services_obj, jstring_create("internet"));

	jobject_put(reply_obj, J_CSTR_TO_JVAL("service"), provided_services_obj);
}

/**
 * @brief Fill in all status information to be sent with 'getstatus' method
 *
 * @param reply JSON object
 */

static void create_connection_status_reply(jvalue_ref reply)
{
	jvalue_ref service_obj = NULL;
	jvalue_ref connected_services_obj = NULL;
	GSList *iter = NULL;
	connman_service_t *connected_service;
	gboolean dataaccess_usable = FALSE;

	if (NULL == reply)
		return;

	jobject_put(reply, J_CSTR_TO_JVAL("state"),
				jstring_create(is_cellular_powered() ? "enabled" : "disabled"));

	/* see ofono/doc/connman-api.txt: When Radio Packet Service is in notattached state
	 * all contexts and all cellular services are disconnected and not available anymore */
	jobject_put(reply, J_CSTR_TO_JVAL("networkstatus"),
				jstring_create(manager->cellular_services != NULL ? "attached" : "notattached"));

	connected_services_obj = jarray_create(NULL);

	for (iter = manager->cellular_services; iter != NULL; iter = g_slist_next(iter)) {
		connected_service = iter->data;
		service_obj = jobject_create();
		add_connected_service_status(service_obj, connected_service);
		jarray_append(connected_services_obj, service_obj);

		/* If at least one cellular service is online we mark dataaccess as usable */
		dataaccess_usable |= (connman_service_get_state(connected_service->state) == CONNMAN_SERVICE_STATE_ONLINE);
	}

	jobject_put(reply, J_CSTR_TO_JVAL("dataaccess"), jstring_create(dataaccess_usable ? "usable" : "unusable"));

	/* FIXME we need to determine somehow the network type of the service */
	jobject_put(reply, J_CSTR_TO_JVAL("networktype"), jstring_create("umts"));

	jobject_put(reply, J_CSTR_TO_JVAL("connectedservices"), connected_services_obj);
}

/**
 * @brief Send out a status update to all registered subscribers.
 */

static void send_connection_status_update()
{
	jvalue_ref reply_obj;
	LSError lserror;
	const char *payload = NULL;

	LSErrorInit(&lserror);

	reply_obj = jobject_create();
	jobject_put(reply_obj, J_CSTR_TO_JVAL("returnValue"), jboolean_create(true));

	create_connection_status_reply(reply_obj);

	jschema_ref response_schema = jschema_parse (j_cstr_to_buffer("{}"), DOMOPT_NOOPT, NULL);
	if (response_schema) {
		payload = jvalue_tostring(reply_obj, response_schema);

		if (!LSSubscriptionPost(pLsHandle, "/", LUNA_METHOD_GETSTATUS, payload, &lserror)) {
			LSErrorPrint(&lserror, stderr);
			LSErrorFree(&lserror);
		}

		jschema_release(&response_schema);
	}

	j_release(&reply_obj);
}

/**
 * @brief Handle any changes to connected cellular services.
 *
 * @param data Connman service object
 * @param state Conman service state
 */
static void celluar_service_changed_cb(gpointer data, const gchar *key, GVariant *value)
{
	send_connection_status_update();
}

/**
 * @brief Register for status updates on all available cellular services.
 */
static void register_for_cellular_service_updates(void)
{
	GSList *iter = NULL;
	connman_service_t *service = NULL;

	for (iter = manager->cellular_services; iter != NULL; iter = g_slist_next(iter)) {
		service = iter->data;
		connman_service_register_property_changed_cb(service, celluar_service_changed_cb);
	}
}

/**
 * @brief Callback function registered with connman manager whenever any of its services change
 * This would happen whenever any existing service is changed/deleted, or a new service is added.
 *
 * @param data Additional supplied user data (unused here)
 */

static void cellular_services_changed_cb(gpointer data)
{
	/* reassign status handler to all service objects */
	register_for_cellular_service_updates();

	/* send out update to all subscribers */
	send_connection_status_update();
}

/**
 *  @brief Handler for "getstatus" command.
 *  Get the current wifi connection status, details of the access point if connected to one,
 *  and the ip related info like address, gateway, dns if the service is online
 *
 *  JSON format:
 *
 *  luna://com.palm.wan/getstatus {}
 *  luna://com.palm.wan/getstatus {"subscribe":true}
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

	if (!connman_status_check(manager, sh, message))
		goto cleanup;

	if (!cellular_technology_status_check(sh, message))
		goto cleanup;

	jobject_put(reply, J_CSTR_TO_JVAL("returnValue"), jboolean_create(true));

	create_connection_status_reply(reply);

	jschema_ref response_schema = jschema_parse (j_cstr_to_buffer("{}"), DOMOPT_NOOPT, NULL);
	if (!response_schema)
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
 * com.palm.connectionmanager service Luna Method Table
 */

static LSMethod wan_methods[] = {
	{ LUNA_METHOD_GETSTATUS, handle_get_status_command },
	{ },
};

/**
 *  @brief Initialize com.palm.wan service and all of its methods
 *  Also initialize a manager instance
 */

int initialize_wan_ls2_calls(GMainLoop *mainloop)
{
	LSError lserror;
	LSErrorInit (&lserror);
	pLsHandle       = NULL;
	pLsPublicHandle = NULL;

	if (NULL == mainloop)
		goto Exit;

	if (LSRegisterPubPriv(WAN_LUNA_SERVICE_NAME, &pLsHandle, false, &lserror) == false)
	{
		g_error("LSRegister() private returned error");
		goto Exit;
	}

	if (LSRegisterPubPriv(WAN_LUNA_SERVICE_NAME, &pLsPublicHandle, true, &lserror) == false)
	{
		g_error("LSRegister() public returned error");
		goto Exit;
	}

	if (LSRegisterCategory(pLsHandle, NULL, wan_methods, NULL, NULL, &lserror) == false)
	{
		g_error("LSRegisterCategory() returned error");
		goto Exit;
	}

	if (LSGmainAttach(pLsHandle, mainloop, &lserror) == false)
	{
		g_error("LSGmainAttach() private returned error");
		goto Exit;
	}

	if (LSGmainAttach(pLsPublicHandle, mainloop, &lserror) == false)
	{
		g_error("LSGmainAttach() public returned error");
		goto Exit;
	}

	connman_manager_register_services_changed_cb(manager, cellular_services_changed_cb, NULL);

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
		if (LSUnregister(pLsHandle, &lserror) == false)
		{
			LSErrorPrint(&lserror, stderr);
			LSErrorFree(&lserror);
		}
	}

	if (pLsPublicHandle)
	{
		LSErrorInit (&lserror);
		if (LSUnregister(pLsPublicHandle, &lserror) == false)
		{
			LSErrorPrint(&lserror, stderr);
			LSErrorFree(&lserror);
		}
	}

	return -1;
}
