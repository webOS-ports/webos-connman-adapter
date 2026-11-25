// Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#ifndef WIFI_SECRET_H_
#define WIFI_SECRET_H_

#include <glib.h>

/*
 * Reading back the passphrase of a saved Wi-Fi network, so that Settings can
 * show it as a QR code (getPassphrase). Kept free of luna and D-Bus so it can
 * be tested on its own.
 *
 * connman keeps what it learnt about a service in
 * <storagedir>/<identifier>/settings, a GKeyFile with one group named after
 * the identifier: "Passphrase" (g_strescape()d), "SSID" (the raw SSID in
 * hex), "Name" and "Hidden". Networks this adapter provisioned through
 * connect() also have <storagedir>/wifi_<ssid>_<security>.config, group
 * "service_<ssid>", whose "Passphrase" is plain (see store_network_config()).
 *
 * Nothing here follows a symbolic link below storagedir, reads a file that is
 * not a regular file with one link, owned by the caller and writable by it
 * alone, or builds a path from a name with a '/' in it.
 */

typedef struct wifi_secret
{
	gchar *ssid;       /* NULL if the settings named none that is UTF-8 */
	gchar *passphrase; /* NULL if none is stored; wiped by wifi_secret_clear() */
	gboolean hidden;
} wifi_secret_t;

/*
 * Whether identifier looks like a connman Wi-Fi service identifier
 * ("wifi_<mac>_<ssid hex>_managed_psk"): "wifi_" and then only ASCII letters,
 * digits and '_', at most 255 bytes. That keeps it a single, harmless path
 * component; whether it is a service connman actually has is the caller's
 * to check.
 */
gboolean wifi_secret_identifier_is_valid(const char *identifier);

/*
 * The security a Wi-Fi QR code should name for a service with connman's
 * Security list: "psk", "sae", "wep" or "none", or NULL when the network
 * cannot be joined from a QR code (802.1X, or nothing known). "psk" wins over
 * "sae", so a WPA2/WPA3 transition network stays joinable for WPA2 devices.
 * connman 2.0 itself reports SAE networks as "psk".
 */
const char *wifi_secret_qr_security(const char *const *security);

/*
 * Reads <storagedir>/<identifier>/settings into out (which must be zeroed or
 * cleared). Returns FALSE, with out untouched, if identifier is not valid or
 * the file cannot be read safely; TRUE otherwise, even when it holds no
 * passphrase.
 */
gboolean wifi_secret_read_settings(const char *storagedir,
                                   const char *identifier, wifi_secret_t *out);

/*
 * The passphrase in this adapter's own <storagedir>/wifi_<ssid>_<security>.config,
 * or NULL. ssid must be 1 to 32 bytes with no '/'; security one of "none",
 * "wep", "psk", "sae", "ieee8021x". Free the result with wifi_secret_free().
 */
gchar *wifi_secret_read_config(const char *storagedir, const char *ssid,
                               const char *security);

/* Overwrites a NUL-terminated secret with zeros and frees it. NULL is fine. */
void wifi_secret_free(gchar *secret);

/* Frees what out holds, wiping the passphrase, and zeroes it. */
void wifi_secret_clear(wifi_secret_t *out);

#endif /* WIFI_SECRET_H_ */
