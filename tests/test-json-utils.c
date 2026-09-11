// Copyright (c) 2026 LG Electronics, Inc.
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

#include <glib.h>
#include <string.h>
#include <stdarg.h>
#include <pbnjson.h>

#include "json_utils.h"

static jvalue_ref parse_json(const char *str)
{
	JSchemaInfo schemaInfo;
	jschema_info_init(&schemaInfo, jschema_all(), NULL, NULL);
	jvalue_ref obj = jdom_parse(j_cstr_to_buffer(str), DOMOPT_NOOPT,
	                            &schemaInfo);
	g_assert(!jis_null(obj));
	return obj;
}

static char *convert(jvalue_ref json, ...)
{
	va_list args;
	char *error;

	va_start(args, json);
	error = json_convert_to_native_valist(json, &args);
	va_end(args);

	return error;
}

static void test_convert_basic_types(void)
{
	jvalue_ref json = parse_json(
	                      "{\"b\": true, \"i\": -42, \"s\": \"hello\"}");
	bool b = false;
	gint32 i = 0;
	const char *s = NULL;
	char *error;

	error = convert(json,
	                json_object_start,
	                json_type_boolean, "b", &b, TRUE,
	                json_type_int32, "i", &i, TRUE,
	                json_type_string, "s", &s, TRUE,
	                json_object_end);

	g_assert(error == NULL);
	g_assert(b == true);
	g_assert(i == -42);
	g_assert(s != NULL && strcmp(s, "hello") == 0);

	j_release(&json);
}

static void test_convert_unsigned_ranges(void)
{
	/* 256 does not fit an uint8, 65536 does not fit an uint16: the
	 * conversion must fail instead of silently truncating */
	jvalue_ref json = parse_json("{\"v\": 256}");
	guint8 u8 = 0;
	char *error;

	error = convert(json,
	                json_object_start,
	                json_type_uint8, "v", &u8, TRUE,
	                json_object_end);
	g_assert(error != NULL);
	g_free(error);
	j_release(&json);

	json = parse_json("{\"v\": 255}");
	error = convert(json,
	                json_object_start,
	                json_type_uint8, "v", &u8, TRUE,
	                json_object_end);
	g_assert(error == NULL);
	g_assert(u8 == 255);
	j_release(&json);

	json = parse_json("{\"v\": 65536}");
	guint16 u16 = 0;
	error = convert(json,
	                json_object_start,
	                json_type_uint16, "v", &u16, TRUE,
	                json_object_end);
	g_assert(error != NULL);
	g_free(error);
	j_release(&json);

	json = parse_json("{\"v\": -1}");
	guint32 u32 = 0;
	error = convert(json,
	                json_object_start,
	                json_type_uint32, "v", &u32, TRUE,
	                json_object_end);
	g_assert(error != NULL);
	g_free(error);
	j_release(&json);
}

static void test_convert_missing_mandatory(void)
{
	jvalue_ref json = parse_json("{}");
	bool b = false;
	char *error;

	error = convert(json,
	                json_object_start,
	                json_type_boolean, "missing", &b, TRUE,
	                json_object_end);

	g_assert(error != NULL);
	g_free(error);
	j_release(&json);
}

static char *generate(jvalue_ref *result, ...)
{
	va_list args;
	char *error;

	va_start(args, result);
	error = json_generate_from_native_valist(result, &args);
	va_end(args);

	return error;
}

static void test_generate_roundtrip(void)
{
	jvalue_ref obj = NULL;
	char *error;

	error = generate(&obj,
	                 json_object_start,
	                 json_type_boolean, "flag", TRUE, TRUE,
	                 json_type_int32, "num", 1234, TRUE,
	                 json_type_string, "text", "abc", TRUE,
	                 json_object_end);

	g_assert(error == NULL);
	g_assert(obj != NULL);

	jvalue_ref field = NULL;
	bool flag = false;
	g_assert(jobject_get_exists(obj, J_CSTR_TO_BUF("flag"), &field));
	jboolean_get(field, &flag);
	g_assert(flag == true);

	gint32 num = 0;
	g_assert(jobject_get_exists(obj, J_CSTR_TO_BUF("num"), &field));
	jnumber_get_i32(field, &num);
	g_assert(num == 1234);

	j_release(&obj);
}

int main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/json_utils/convert_basic_types",
	                test_convert_basic_types);
	g_test_add_func("/json_utils/convert_unsigned_ranges",
	                test_convert_unsigned_ranges);
	g_test_add_func("/json_utils/convert_missing_mandatory",
	                test_convert_missing_mandatory);
	g_test_add_func("/json_utils/generate_roundtrip",
	                test_generate_roundtrip);

	return g_test_run();
}
