/* @@@LICENSE
*
* Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
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
 * @file wifi_ap_info.c
 *
 * @brief Reads the associated access point's details from wpa_supplicant.
 *
 * Everything here comes off the BSS object wpa_supplicant already publishes
 * for the association: BSSID, Frequency, Signal and the raw information
 * elements from the beacon. The channel width is not a property of its own,
 * so it is worked out from the HT and VHT operation elements inside IEs, the
 * same ones "iw dev <iface> link" reports from.
 *
 * These calls are synchronous. They are made only while answering getstatus,
 * against a local daemon, and each is a single property read, so the cost is
 * a round trip on the system bus rather than anything that can block on the
 * network.
 */

#include "wifi_ap_info.h"

#include <gio/gio.h>
#include <string.h>

#define WPA_SUPPLICANT_SERVICE    "fi.w1.wpa_supplicant1"
#define WPA_SUPPLICANT_PATH       "/fi/w1/wpa_supplicant1"
#define WPA_SUPPLICANT_INTERFACE  "fi.w1.wpa_supplicant1"
#define WPA_INTERFACE_INTERFACE   "fi.w1.wpa_supplicant1.Interface"
#define WPA_BSS_INTERFACE         "fi.w1.wpa_supplicant1.BSS"

#define DBUS_CALL_TIMEOUT_MS      2000

/* Information element ids, as numbered by IEEE 802.11. */
#define WLAN_EID_HT_OPERATION     61
#define WLAN_EID_VHT_OPERATION    192

/**
 * @brief Channel number for a frequency in MHz, 0 when out of the ranges
 * where the arithmetic is exact rather than guessed.
 */
static guint16 channel_from_frequency(guint16 frequency)
{
	if (frequency == 2484)
	{
		return 14;
	}

	if (frequency >= 2412 && frequency <= 2472)
	{
		return (frequency - 2407) / 5;
	}

	if (frequency >= 5160 && frequency <= 5885)
	{
		return (frequency - 5000) / 5;
	}

	if (frequency >= 5955 && frequency <= 7115)
	{
		return (frequency - 5950) / 5;
	}

	return 0;
}

/**
 * @brief Operating width in MHz from the beacon's information elements.
 *
 * An HT operation element widens the channel to 40MHz, and a VHT operation
 * element on top of it to 80, 160 or 80+80 depending on how far apart the two
 * channel centre segments are. Without either the AP is running 20MHz.
 *
 * HE (802.11ax) is not read: on 2.4 and 5GHz an HE access point still carries
 * the VHT operation element, so this answers correctly there. A 6GHz-only HE
 * BSS carries neither, and reports 20MHz.
 */
static guint16 width_from_ies(const guint8 *ies, gsize ies_len)
{
	guint16 width = 20;
	const guint8 *ht_op = NULL;
	const guint8 *vht_op = NULL;
	gsize ht_op_len = 0;
	gsize vht_op_len = 0;
	gsize pos = 0;

	if (!ies)
	{
		return 0;
	}

	/* Elements are id, length, payload - walk them rather than seek. */
	while (pos + 2 <= ies_len)
	{
		guint8 id = ies[pos];
		guint8 len = ies[pos + 1];

		if (pos + 2 + len > ies_len)
		{
			/* Truncated element: nothing after it can be trusted. */
			break;
		}

		if (id == WLAN_EID_HT_OPERATION)
		{
			ht_op = &ies[pos + 2];
			ht_op_len = len;
		}
		else if (id == WLAN_EID_VHT_OPERATION)
		{
			vht_op = &ies[pos + 2];
			vht_op_len = len;
		}

		pos += 2 + len;
	}

	/*
	 * HT Operation: byte 1 holds the secondary channel offset in bits 0-1
	 * (1 above, 3 below) and the STA channel width in bit 2. Both have to
	 * say 40 for the channel to actually be 40MHz wide.
	 */
	if (ht_op && ht_op_len >= 2)
	{
		guint8 secondary = ht_op[1] & 0x03;

		if ((ht_op[1] & 0x04) && (secondary == 1 || secondary == 3))
		{
			width = 40;
		}
	}

	/*
	 * VHT Operation: byte 0 is the channel width field, bytes 1 and 2 the
	 * two channel centre frequency segments. Width 1 means 80MHz unless a
	 * second segment says otherwise - contiguous 160MHz puts the segments
	 * 16 channels apart, 80+80 leaves them further apart than 8.
	 */
	if (vht_op && vht_op_len >= 3)
	{
		guint8 vht_width = vht_op[0];
		guint8 segment0 = vht_op[1];
		guint8 segment1 = vht_op[2];

		switch (vht_width)
		{
			case 1:
				if (segment1 != 0)
				{
					int gap = (int)segment1 - (int)segment0;

					if (gap < 0)
					{
						gap = -gap;
					}

					if (gap == 16)
					{
						width = 160;
					}
					else if (gap > 16)
					{
						width = 8080;
					}
					else
					{
						width = 80;
					}
				}
				else
				{
					width = 80;
				}

				break;

			/* Both deprecated in favour of the segment encoding above. */
			case 2:
				width = 160;
				break;

			case 3:
				width = 8080;
				break;

			default:
				/* 0: defer to whatever HT said. */
				break;
		}
	}

	return width;
}

/**
 * @brief Read one property, returning NULL rather than logging when
 * wpa_supplicant is not there - it not running at all is a normal state.
 */
static GVariant *get_property(GDBusConnection *connection, const gchar *path,
                              const gchar *interface, const gchar *property)
{
	GVariant *result;
	GVariant *value = NULL;

	result = g_dbus_connection_call_sync(connection,
	                                     WPA_SUPPLICANT_SERVICE, path,
	                                     "org.freedesktop.DBus.Properties",
	                                     "Get",
	                                     g_variant_new("(ss)", interface, property),
	                                     G_VARIANT_TYPE("(v)"),
	                                     G_DBUS_CALL_FLAGS_NONE,
	                                     DBUS_CALL_TIMEOUT_MS, NULL, NULL);

	if (!result)
	{
		return NULL;
	}

	g_variant_get(result, "(v)", &value);
	g_variant_unref(result);

	return value;
}

/**
 * @brief Fetch a property only if it arrived as the type expected.
 *
 * The g_variant_get_* accessors abort on a type mismatch, and this runs inside
 * the daemon, so a wpa_supplicant that ever changed one of these would take
 * the adapter down with it. Checking first turns that into a missing field.
 */
static GVariant *get_property_typed(GDBusConnection *connection,
                                    const gchar *path, const gchar *interface,
                                    const gchar *property,
                                    const GVariantType *type)
{
	GVariant *value = get_property(connection, path, interface, property);

	if (!value)
	{
		return NULL;
	}

	if (!g_variant_is_of_type(value, type))
	{
		g_variant_unref(value);
		return NULL;
	}

	return value;
}

void wifi_ap_info_free(wifi_ap_info_t *info)
{
	if (!info)
	{
		return;
	}

	g_free(info->bssid);
	info->bssid = NULL;
}

bool wifi_ap_info_get(const gchar *ifname, wifi_ap_info_t *info)
{
	GDBusConnection *connection;
	GVariant *result;
	GVariant *value;
	const gchar *interface_path = NULL;
	const gchar *bss_path = NULL;
	gchar *interface_path_owned = NULL;
	gchar *bss_path_owned = NULL;
	bool found = false;

	if (!ifname || !info)
	{
		return false;
	}

	memset(info, 0, sizeof(*info));

	connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, NULL);

	if (!connection)
	{
		return false;
	}

	result = g_dbus_connection_call_sync(connection,
	                                     WPA_SUPPLICANT_SERVICE,
	                                     WPA_SUPPLICANT_PATH,
	                                     WPA_SUPPLICANT_INTERFACE,
	                                     "GetInterface",
	                                     g_variant_new("(s)", ifname),
	                                     G_VARIANT_TYPE("(o)"),
	                                     G_DBUS_CALL_FLAGS_NONE,
	                                     DBUS_CALL_TIMEOUT_MS, NULL, NULL);

	if (!result)
	{
		goto out;
	}

	g_variant_get(result, "(o)", &interface_path_owned);
	g_variant_unref(result);
	interface_path = interface_path_owned;

	/*
	 * CurrentBSS is "/" while the interface is not associated, which is not
	 * a failure - there is simply no access point to describe.
	 */
	value = get_property_typed(connection, interface_path,
	                           WPA_INTERFACE_INTERFACE, "CurrentBSS",
	                           G_VARIANT_TYPE_OBJECT_PATH);

	if (!value)
	{
		goto out;
	}

	bss_path_owned = g_variant_dup_string(value, NULL);
	g_variant_unref(value);
	bss_path = bss_path_owned;

	if (!bss_path || g_strcmp0(bss_path, "/") == 0)
	{
		goto out;
	}

	value = get_property_typed(connection, bss_path, WPA_BSS_INTERFACE,
	                           "BSSID", G_VARIANT_TYPE_BYTESTRING);

	if (value)
	{
		gsize length = 0;
		const guint8 *bssid = g_variant_get_fixed_array(value, &length,
		                                                sizeof(guint8));

		if (bssid && length == 6)
		{
			info->bssid = g_strdup_printf(
			                  "%02x:%02x:%02x:%02x:%02x:%02x",
			                  bssid[0], bssid[1], bssid[2],
			                  bssid[3], bssid[4], bssid[5]);
			found = true;
		}

		g_variant_unref(value);
	}

	value = get_property_typed(connection, bss_path, WPA_BSS_INTERFACE,
	                           "Frequency", G_VARIANT_TYPE_UINT16);

	if (value)
	{
		info->frequency = g_variant_get_uint16(value);
		info->channel = channel_from_frequency(info->frequency);
		g_variant_unref(value);
		found = true;
	}

	value = get_property_typed(connection, bss_path, WPA_BSS_INTERFACE,
	                           "Signal", G_VARIANT_TYPE_INT16);

	if (value)
	{
		info->signal_level = g_variant_get_int16(value);
		g_variant_unref(value);
		found = true;
	}

	value = get_property_typed(connection, bss_path, WPA_BSS_INTERFACE,
	                           "IEs", G_VARIANT_TYPE_BYTESTRING);

	if (value)
	{
		gsize length = 0;
		const guint8 *ies = g_variant_get_fixed_array(value, &length,
		                                              sizeof(guint8));

		info->width = width_from_ies(ies, length);

		if (info->width > 0)
		{
			found = true;
		}

		g_variant_unref(value);
	}

out:
	g_free(interface_path_owned);
	g_free(bss_path_owned);
	g_object_unref(connection);

	info->valid = found;

	if (!found)
	{
		wifi_ap_info_free(info);
	}

	return found;
}
