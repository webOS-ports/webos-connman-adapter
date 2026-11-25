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

#include <fcntl.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "wifi_secret.h"

#define IDENT "wifi_0011223344aa_486f6d65_managed_psk"

static char *dir;

static void write_file(const char *relpath, const char *content, mode_t mode)
{
	char *path = g_build_filename(dir, relpath, NULL);
	int fd;

	g_unlink(path);
	fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
	g_assert(fd >= 0);
	g_assert(write(fd, content, strlen(content)) == (ssize_t) strlen(content));
	close(fd);
	g_assert(chmod(path, mode) == 0);
	g_free(path);
}

/* A settings file as connman 2.0's service_save() writes one. */
static void write_settings(const char *ident, const char *ssid_hex,
                           const char *name, const char *passphrase,
                           gboolean hidden)
{
	GKeyFile *keyfile = g_key_file_new();
	char *subdir = g_build_filename(dir, ident, NULL);
	char *rel = g_build_filename(ident, "settings", NULL);
	char *data;

	g_mkdir(subdir, 0700);

	if (name)
		g_key_file_set_string(keyfile, ident, "Name", name);

	if (ssid_hex)
		g_key_file_set_string(keyfile, ident, "SSID", ssid_hex);

	g_key_file_set_boolean(keyfile, ident, "Favorite", TRUE);

	if (hidden)
		g_key_file_set_boolean(keyfile, ident, "Hidden", TRUE);

	if (passphrase && *passphrase)
	{
		char *enc = g_strescape(passphrase, NULL);
		g_key_file_set_string(keyfile, ident, "Passphrase", enc);
		g_free(enc);
	}

	data = g_key_file_to_data(keyfile, NULL, NULL);
	write_file(rel, data, 0600);

	g_free(data);
	g_free(rel);
	g_free(subdir);
	g_key_file_free(keyfile);
}

/* A config as this adapter's store_network_config() writes one. */
static void write_config(const char *ssid, const char *security,
                         const char *passphrase)
{
	GKeyFile *keyfile = g_key_file_new();
	char *group = g_strdup_printf("service_%s", ssid);
	char *name = g_strdup_printf("wifi_%s_%s.config", ssid, security);
	char *data;

	g_key_file_set_string(keyfile, group, "Type", "wifi");
	g_key_file_set_string(keyfile, group, "Name", ssid);

	if (passphrase)
	{
		g_key_file_set_string(keyfile, group, "Passphrase", passphrase);
		g_key_file_set_string(keyfile, group, "Security", security);
	}

	data = g_key_file_to_data(keyfile, NULL, NULL);
	write_file(name, data, 0600);

	g_free(data);
	g_free(name);
	g_free(group);
	g_key_file_free(keyfile);
}

static char *hex_of(const char *s)
{
	GString *out = g_string_new(NULL);

	for (; *s; s++)
		g_string_append_printf(out, "%02x", (unsigned char) *s);

	return g_string_free(out, FALSE);
}

static void test_identifier(void)
{
	static const char *const good[] = {
		IDENT, "wifi_a", "wifi_001122334455_hidden_managed_psk",
	};
	static const char *const bad[] = {
		"", "wifi_", "wifi", "ethernet_0011_cable", "wifi_../x", "wifi_a/b",
		"wifi_..", "../wifi_a", "/wifi_a", "wifi_a b", "wifi_a.b", "wifi_a\\b",
		"Wifi_a", "wifi_\xc3\xa9", "wifi_a\n",
	};
	char *longest = g_strnfill(255, 'a');
	char *too_long = g_strnfill(256, 'a');
	size_t i;

	for (i = 0; i < G_N_ELEMENTS(good); i++)
		g_assert_true(wifi_secret_identifier_is_valid(good[i]));

	for (i = 0; i < G_N_ELEMENTS(bad); i++)
		g_assert_false(wifi_secret_identifier_is_valid(bad[i]));

	g_assert_false(wifi_secret_identifier_is_valid(NULL));

	memcpy(longest, "wifi_", 5);
	memcpy(too_long, "wifi_", 5);
	g_assert_true(wifi_secret_identifier_is_valid(longest));
	g_assert_false(wifi_secret_identifier_is_valid(too_long));

	g_free(longest);
	g_free(too_long);
}

static void test_security(void)
{
	static const char *const psk_wps[] = { "psk", "wps", NULL };
	static const char *const sae_psk[] = { "sae", "psk", NULL };
	static const char *const sae[] = { "sae", NULL };
	static const char *const wep[] = { "wep", NULL };
	static const char *const none[] = { "none", NULL };
	static const char *const eap[] = { "ieee8021x", NULL };
	static const char *const wps[] = { "wps", NULL };
	static const char *const empty[] = { NULL };

	g_assert_cmpstr(wifi_secret_qr_security(psk_wps), ==, "psk");
	g_assert_cmpstr(wifi_secret_qr_security(sae_psk), ==, "psk");
	g_assert_cmpstr(wifi_secret_qr_security(sae), ==, "sae");
	g_assert_cmpstr(wifi_secret_qr_security(wep), ==, "wep");
	g_assert_cmpstr(wifi_secret_qr_security(none), ==, "none");
	g_assert_null(wifi_secret_qr_security(eap));
	g_assert_null(wifi_secret_qr_security(wps));
	g_assert_null(wifi_secret_qr_security(empty));
	g_assert_null(wifi_secret_qr_security(NULL));
}

static void check_roundtrip(const char *passphrase)
{
	wifi_secret_t secret = { 0 };

	write_settings(IDENT, "486f6d65", "Home", passphrase, FALSE);
	g_assert_true(wifi_secret_read_settings(dir, IDENT, &secret));
	g_assert_cmpstr(secret.passphrase, ==, passphrase);
	g_assert_cmpstr(secret.ssid, ==, "Home");
	g_assert_false(secret.hidden);
	wifi_secret_clear(&secret);
	g_assert_null(secret.passphrase);
	g_assert_null(secret.ssid);
}

static void test_settings_passphrases(void)
{
	static const char *const odd[] = {
		"correct horse battery staple",
		"back\\slash",
		"quo\"te;semi:colon,comma",
		" leading and trailing ",
		"=equals=",
		"#not a comment",
		"[not a group]",
		"tab\there",
		"line\nbreak",
		"\\n literally",
		"caf\xc3\xa9 \xe2\x82\xac",
		"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcde",
	};
	size_t i;

	for (i = 0; i < G_N_ELEMENTS(odd); i++)
		check_roundtrip(odd[i]);
}

static void test_settings_ssid_and_hidden(void)
{
	wifi_secret_t secret = { 0 };
	char *hex = hex_of("Caf\xc3\xa9;Net");

	/* SSID hex wins over Name. */
	write_settings(IDENT, hex, "Other", "secret12", TRUE);
	g_assert_true(wifi_secret_read_settings(dir, IDENT, &secret));
	g_assert_cmpstr(secret.ssid, ==, "Caf\xc3\xa9;Net");
	g_assert_true(secret.hidden);
	wifi_secret_clear(&secret);

	/* Not UTF-8, or not hex: Name instead. */
	write_settings(IDENT, "ff fe", "Fallback", "secret12", FALSE);
	g_assert_true(wifi_secret_read_settings(dir, IDENT, &secret));
	g_assert_cmpstr(secret.ssid, ==, "Fallback");
	wifi_secret_clear(&secret);

	write_settings(IDENT, "fffe", "Fallback", "secret12", FALSE);
	g_assert_true(wifi_secret_read_settings(dir, IDENT, &secret));
	g_assert_cmpstr(secret.ssid, ==, "Fallback");
	wifi_secret_clear(&secret);

	/* An embedded NUL is no SSID a QR code can carry. */
	write_settings(IDENT, "410042", NULL, "secret12", FALSE);
	g_assert_true(wifi_secret_read_settings(dir, IDENT, &secret));
	g_assert_null(secret.ssid);
	wifi_secret_clear(&secret);

	g_free(hex);
}

static void test_settings_missing_passphrase(void)
{
	wifi_secret_t secret = { 0 };

	write_settings(IDENT, "486f6d65", "Home", NULL, FALSE);
	g_assert_true(wifi_secret_read_settings(dir, IDENT, &secret));
	g_assert_null(secret.passphrase);
	g_assert_cmpstr(secret.ssid, ==, "Home");
	wifi_secret_clear(&secret);

	/* Passphrase in another group is not this service's. */
	write_file(IDENT "/settings",
	           "[wifi_other]\nPassphrase=nope\n[" IDENT "]\nName=Home\n", 0600);
	g_assert_true(wifi_secret_read_settings(dir, IDENT, &secret));
	g_assert_null(secret.passphrase);
	wifi_secret_clear(&secret);

	/* No such service directory, or garbage: nothing read. */
	g_assert_false(wifi_secret_read_settings(dir, "wifi_nosuch", &secret));
	write_file(IDENT "/settings", "not a key file\n", 0600);
	g_assert_false(wifi_secret_read_settings(dir, IDENT, &secret));
	g_assert_null(secret.ssid);
}

static void test_settings_refused(void)
{
	wifi_secret_t secret = { 0 };
	char *target = g_build_filename(dir, "elsewhere", NULL);
	char *link_dir = g_build_filename(dir, "wifi_linked", NULL);
	char *settings = g_build_filename(dir, IDENT, "settings", NULL);

	/* Writable by others. */
	write_settings(IDENT, "486f6d65", "Home", "secret12", FALSE);
	g_assert(chmod(settings, 0620) == 0);
	g_assert_false(wifi_secret_read_settings(dir, IDENT, &secret));
	g_assert(chmod(settings, 0602) == 0);
	g_assert_false(wifi_secret_read_settings(dir, IDENT, &secret));
	g_assert(chmod(settings, 0644) == 0);
	g_assert_true(wifi_secret_read_settings(dir, IDENT, &secret));
	wifi_secret_clear(&secret);

	/* settings itself a symlink. */
	write_file("elsewhere", "[" IDENT "]\nPassphrase=stolen\n", 0600);
	g_unlink(settings);
	g_assert(symlink(target, settings) == 0);
	g_assert_false(wifi_secret_read_settings(dir, IDENT, &secret));
	g_unlink(settings);

	/* The service directory a symlink. */
	write_settings(IDENT, "486f6d65", "Home", "secret12", FALSE);
	g_assert(symlink(IDENT, link_dir) == 0);
	g_assert_false(wifi_secret_read_settings(dir, "wifi_linked", &secret));

	/* Not a regular file. */
	g_unlink(settings);
	g_assert(g_mkdir(settings, 0700) == 0);
	g_assert_false(wifi_secret_read_settings(dir, IDENT, &secret));
	g_rmdir(settings);

	/* A second link to it. */
	{
		char *other = g_build_filename(dir, "other-link", NULL);

		write_settings(IDENT, "486f6d65", "Home", "secret12", FALSE);
		g_assert(link(settings, other) == 0);
		g_assert_false(wifi_secret_read_settings(dir, IDENT, &secret));
		g_unlink(other);
		g_assert_true(wifi_secret_read_settings(dir, IDENT, &secret));
		wifi_secret_clear(&secret);
		g_unlink(settings);
		g_free(other);
	}

	/* A FIFO: refused, and without blocking on the open. */
	g_assert(mkfifo(settings, 0600) == 0);
	g_assert_false(wifi_secret_read_settings(dir, IDENT, &secret));
	g_unlink(settings);

	/* An identifier that walks out. */
	g_assert_false(wifi_secret_read_settings(dir, "../" IDENT, &secret));
	g_assert_false(wifi_secret_read_settings(NULL, IDENT, &secret));
	g_assert_null(secret.passphrase);

	g_unlink(link_dir);
	g_unlink(target);
	g_free(settings);
	g_free(link_dir);
	g_free(target);
}

static void test_config(void)
{
	char *pass;
	char *target = g_build_filename(dir, "elsewhere.config", NULL);
	char *linked = g_build_filename(dir, "wifi_Linked_psk.config", NULL);

	write_config("Cafe Net", "psk", "quo\"te;semi:colon\\");
	pass = wifi_secret_read_config(dir, "Cafe Net", "psk");
	g_assert_cmpstr(pass, ==, "quo\"te;semi:colon\\");
	wifi_secret_free(pass);

	/* Other security, other file. */
	g_assert_null(wifi_secret_read_config(dir, "Cafe Net", "wep"));

	/* No passphrase key (open network). */
	write_config("Open", "none", NULL);
	g_assert_null(wifi_secret_read_config(dir, "Open", "none"));

	/* Names that would leave the directory, or are not SSIDs: with a file
	 * planted where each would lead. */
	{
		char *up = g_build_filename(dir, "wifi_..", NULL);
		char *sub = g_build_filename(dir, "wifi_a", NULL);

		g_assert(g_mkdir(up, 0700) == 0);
		g_assert(g_mkdir(sub, 0700) == 0);
		write_file("wifi_../x_psk.config", "[service_../x]\nPassphrase=planted\n", 0600);
		write_file("wifi_a/b_psk.config", "[service_a/b]\nPassphrase=planted\n", 0600);
		g_free(up);
		g_free(sub);
	}
	g_assert_null(wifi_secret_read_config(dir, "../x", "psk"));
	g_assert_null(wifi_secret_read_config(dir, "a/b", "psk"));
	g_assert_null(wifi_secret_read_config(dir, "", "psk"));
	g_assert_null(wifi_secret_read_config(dir, "123456789012345678901234567890123", "psk"));
	{
		char *secdir = g_build_filename(dir, "wifi_Cafe Net_psk", NULL);

		g_assert(g_mkdir(secdir, 0700) == 0);
		write_file("x.config", "[service_Cafe Net]\nPassphrase=planted\n", 0600);
		g_assert_null(wifi_secret_read_config(dir, "Cafe Net", "psk/../x"));
		g_rmdir(secdir);
		g_free(secdir);
	}
	g_assert_null(wifi_secret_read_config(dir, "Cafe Net", NULL));
	g_assert_null(wifi_secret_read_config(NULL, "Cafe Net", "psk"));

	/* A symlinked config is not read. */
	write_file("elsewhere.config", "[service_Linked]\nPassphrase=stolen\n", 0600);
	g_assert(symlink(target, linked) == 0);
	g_assert_null(wifi_secret_read_config(dir, "Linked", "psk"));

	g_unlink(linked);
	g_unlink(target);
	g_free(linked);
	g_free(target);
}

static void test_free_null(void)
{
	wifi_secret_t secret = { 0 };

	wifi_secret_free(NULL);
	wifi_secret_clear(&secret);
}

int main(int argc, char **argv)
{
	int ret;
	char *rm[] = { "rm", "-rf", NULL, NULL };

	g_test_init(&argc, &argv, NULL);

	dir = g_dir_make_tmp("test-wifi-secret-XXXXXX", NULL);
	g_assert(dir != NULL);

	g_test_add_func("/wifi-secret/identifier", test_identifier);
	g_test_add_func("/wifi-secret/security", test_security);
	g_test_add_func("/wifi-secret/settings-passphrases", test_settings_passphrases);
	g_test_add_func("/wifi-secret/settings-ssid-hidden", test_settings_ssid_and_hidden);
	g_test_add_func("/wifi-secret/settings-missing", test_settings_missing_passphrase);
	g_test_add_func("/wifi-secret/settings-refused", test_settings_refused);
	g_test_add_func("/wifi-secret/config", test_config);
	g_test_add_func("/wifi-secret/free-null", test_free_null);

	ret = g_test_run();

	rm[2] = dir;
	g_spawn_sync(NULL, rm, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL,
	             NULL, NULL);
	g_free(dir);
	return ret;
}
