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

#include "wifi_secret.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* connman's settings for one service are a few hundred bytes. */
#define WIFI_SECRET_MAX_FILE_SIZE ((off_t) 64 * 1024)
#define WIFI_SECRET_MAX_IDENTIFIER 255
#define WIFI_SECRET_MAX_SSID 32

gboolean wifi_secret_identifier_is_valid(const char *identifier)
{
	const char *p;
	size_t len;

	if (identifier == NULL || !g_str_has_prefix(identifier, "wifi_"))
		return FALSE;

	len = strlen(identifier);

	if (len <= strlen("wifi_") || len > WIFI_SECRET_MAX_IDENTIFIER)
		return FALSE;

	for (p = identifier; *p; p++)
	{
		if (!g_ascii_isalnum(*p) && *p != '_')
			return FALSE;
	}

	return TRUE;
}

static gboolean security_listed(const char *const *security, const char *name)
{
	for (; *security; security++)
	{
		if (g_strcmp0(*security, name) == 0)
			return TRUE;
	}

	return FALSE;
}

const char *wifi_secret_qr_security(const char *const *security)
{
	if (security == NULL)
		return NULL;

	if (security_listed(security, "psk"))
		return "psk";

	if (security_listed(security, "sae"))
		return "sae";

	if (security_listed(security, "wep"))
		return "wep";

	if (security_listed(security, "none"))
		return "none";

	return NULL;
}

void wifi_secret_free(gchar *secret)
{
	if (secret == NULL)
		return;

	explicit_bzero(secret, strlen(secret));
	g_free(secret);
}

void wifi_secret_clear(wifi_secret_t *out)
{
	g_free(out->ssid);
	wifi_secret_free(out->passphrase);
	memset(out, 0, sizeof(*out));
}

/*
 * Reads name, relative to dirfd, without following a symbolic link: a
 * regular file owned by us that nobody else may write, of a sane size. The
 * buffer is NUL-terminated; wipe and free it with wifi_secret_free().
 */
static gchar *read_file_at(int dirfd, const char *name, gsize *length)
{
	struct stat st;
	gchar *buffer = NULL;
	gsize done = 0;
	int fd;

	/* O_NONBLOCK: a FIFO planted in its place must not hang the daemon. */
	fd = openat(dirfd, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NOCTTY | O_NONBLOCK);

	if (fd < 0)
		return NULL;

	/* One link only: connman and this adapter both write by rename(). */
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_nlink != 1 ||
	        st.st_uid != geteuid() || (st.st_mode & (S_IWGRP | S_IWOTH)) ||
	        st.st_size < 0 || st.st_size > WIFI_SECRET_MAX_FILE_SIZE)
	{
		close(fd);
		return NULL;
	}

	buffer = g_malloc0((gsize) st.st_size + 1);

	while (done < (gsize) st.st_size)
	{
		ssize_t n = read(fd, buffer + done, (gsize) st.st_size - done);

		if (n < 0 && errno == EINTR)
			continue;

		if (n <= 0)
			break;

		done += (gsize) n;
	}

	close(fd);

	/* Shorter than fstat() said: it changed under us, so trust none of it. */
	if (done != (gsize) st.st_size)
	{
		explicit_bzero(buffer, (gsize) st.st_size);
		g_free(buffer);
		return NULL;
	}

	buffer[done] = '\0';
	*length = done;
	return buffer;
}

static GKeyFile *load_keyfile_at(int dirfd, const char *name)
{
	GKeyFile *keyfile;
	gsize length = 0;
	gchar *data = read_file_at(dirfd, name, &length);
	gboolean loaded;

	if (data == NULL)
		return NULL;

	keyfile = g_key_file_new();
	loaded = g_key_file_load_from_data(keyfile, data, length, G_KEY_FILE_NONE,
	                                   NULL);
	explicit_bzero(data, length);
	g_free(data);

	if (!loaded)
	{
		g_key_file_free(keyfile);
		return NULL;
	}

	return keyfile;
}

/* The SSID connman stores as hex, if it decodes to UTF-8 text. */
static gchar *ssid_from_hex(const char *hex)
{
	size_t len, i;
	GString *ssid;

	if (hex == NULL)
		return NULL;

	len = strlen(hex);

	if (len == 0 || len % 2 != 0 || len / 2 > WIFI_SECRET_MAX_SSID)
		return NULL;

	ssid = g_string_sized_new(len / 2);

	for (i = 0; i < len; i += 2)
	{
		int high = g_ascii_xdigit_value(hex[i]);
		int low = g_ascii_xdigit_value(hex[i + 1]);

		if (high < 0 || low < 0 || (high == 0 && low == 0))
		{
			g_string_free(ssid, TRUE);
			return NULL;
		}

		g_string_append_c(ssid, (gchar)((high << 4) | low));
	}

	if (!g_utf8_validate(ssid->str, (gssize) ssid->len, NULL))
	{
		g_string_free(ssid, TRUE);
		return NULL;
	}

	return g_string_free(ssid, FALSE);
}

gboolean wifi_secret_read_settings(const char *storagedir,
                                   const char *identifier, wifi_secret_t *out)
{
	GKeyFile *keyfile;
	gchar *value;
	int basefd, dirfd;

	if (storagedir == NULL || out == NULL ||
	        !wifi_secret_identifier_is_valid(identifier))
		return FALSE;

	/* storagedir is ours to choose and may itself be a link; below it, nothing is. */
	basefd = open(storagedir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);

	if (basefd < 0)
		return FALSE;

	dirfd = openat(basefd, identifier,
	               O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	close(basefd);

	if (dirfd < 0)
		return FALSE;

	keyfile = load_keyfile_at(dirfd, "settings");
	close(dirfd);

	if (keyfile == NULL)
		return FALSE;

	value = g_key_file_get_string(keyfile, identifier, "Passphrase", NULL);

	if (value != NULL)
	{
		/* connman writes it through g_strescape(); undo that as it does. */
		gchar *passphrase = g_strcompress(value);

		wifi_secret_free(value);

		if (*passphrase)
			out->passphrase = passphrase;
		else
			wifi_secret_free(passphrase);
	}

	value = g_key_file_get_string(keyfile, identifier, "SSID", NULL);
	out->ssid = ssid_from_hex(value);
	g_free(value);

	if (out->ssid == NULL)
	{
		value = g_key_file_get_string(keyfile, identifier, "Name", NULL);

		if (value != NULL && *value && strlen(value) <= WIFI_SECRET_MAX_SSID &&
		        g_utf8_validate(value, -1, NULL))
			out->ssid = value;
		else
			g_free(value);
	}

	out->hidden = g_key_file_get_boolean(keyfile, identifier, "Hidden", NULL);

	/* GKeyFile keeps its own copy, which g_key_file_free() cannot be told to
	 * wipe; it is gone with the key file, at least. */
	g_key_file_free(keyfile);
	return TRUE;
}

static gboolean config_security_is_valid(const char *security)
{
	static const char *const known[] = { "none", "wep", "psk", "sae", "ieee8021x", NULL };

	return security != NULL && security_listed(known, security);
}

gchar *wifi_secret_read_config(const char *storagedir, const char *ssid,
                               const char *security)
{
	GKeyFile *keyfile;
	gchar *name, *group, *passphrase;
	size_t ssid_len;
	int basefd;

	if (storagedir == NULL || ssid == NULL || !config_security_is_valid(security))
		return NULL;

	ssid_len = strlen(ssid);

	if (ssid_len == 0 || ssid_len > WIFI_SECRET_MAX_SSID || strchr(ssid, '/'))
		return NULL;

	basefd = open(storagedir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);

	if (basefd < 0)
		return NULL;

	/* Starts with "wifi_" and holds no '/': one component, never "." or "..". */
	name = g_strdup_printf("wifi_%s_%s.config", ssid, security);
	keyfile = load_keyfile_at(basefd, name);
	close(basefd);
	g_free(name);

	if (keyfile == NULL)
		return NULL;

	group = g_strdup_printf("service_%s", ssid);
	passphrase = g_key_file_get_string(keyfile, group, "Passphrase", NULL);
	g_free(group);
	g_key_file_free(keyfile);

	if (passphrase != NULL && *passphrase == '\0')
	{
		wifi_secret_free(passphrase);
		passphrase = NULL;
	}

	return passphrase;
}
