/*
 * util.c
 *
 * This file is part of ktap by Jovi Zhangwei.
 *
 * Copyright (C) 2012-2013 Jovi Zhangwei <jovi.zhangwei@gmail.com>.
 *
 * Copyright (C) 1994-2013 Lua.org, PUC-Rio.
 *  - The part of code in this file is copied from lua initially.
 *  - lua's MIT license is compatible with GPL.
 *
 * ktap is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * ktap is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../include/ktap_types.h"
#include "../include/ktap_opcodes.h"
#include "ktapc.h"

ktap_number ktapc_arith(int op, ktap_number v1, ktap_number v2)
{
	switch (op) {
	case KTAP_OPADD: return NUMADD(v1, v2);
	case KTAP_OPSUB: return NUMSUB(v1, v2);
	case KTAP_OPMUL: return NUMMUL(v1, v2);
	case KTAP_OPDIV: return NUMDIV(v1, v2);
	case KTAP_OPMOD: return NUMMOD(v1, v2);
	//case KTAP_OPPOW: return NUMPOW(v1, v2);
	case KTAP_OPUNM: return NUMUNM(v1);
	default: ktap_assert(0); return 0;
	}
}

int ktapc_str2d(const char *s, size_t len, ktap_number *result)
{
	char *endptr;

	if (strpbrk(s, "nN"))  /* reject 'inf' and 'nan' */
		return 0;
	else
		*result = (long)strtoul(s, &endptr, 0);

	if (endptr == s)
		return 0;  /* nothing recognized */
	while (isspace((unsigned char)(*endptr)))
		endptr++;
	return (endptr == s + len);  /* OK if no trailing characters */
}

/*
 * strglobmatch is copyed from perf(linux/tools/perf/util/string.c)
 */

/* Character class matching */
static bool __match_charclass(const char *pat, char c, const char **npat)
{
	bool complement = false, ret = true;

	if (*pat == '!') {
		complement = true;
		pat++;
	}
	if (*pat++ == c)	/* First character is special */
		goto end;

	while (*pat && *pat != ']') {	/* Matching */
		if (*pat == '-' && *(pat + 1) != ']') {	/* Range */
			if (*(pat - 1) <= c && c <= *(pat + 1))
				goto end;
			if (*(pat - 1) > *(pat + 1))
				goto error;
			pat += 2;
		} else if (*pat++ == c)
			goto end;
	}
	if (!*pat)
		goto error;
	ret = false;

end:
	while (*pat && *pat != ']')	/* Searching closing */
		pat++;
	if (!*pat)
		goto error;
	*npat = pat + 1;
	return complement ? !ret : ret;

error:
	return false;
}

/* Glob/lazy pattern matching */
static bool __match_glob(const char *str, const char *pat, bool ignore_space)
{
	while (*str && *pat && *pat != '*') {
		if (ignore_space) {
			/* Ignore spaces for lazy matching */
			if (isspace(*str)) {
				str++;
				continue;
			}
			if (isspace(*pat)) {
				pat++;
				continue;
			}
		}
		if (*pat == '?') {	/* Matches any single character */
			str++;
			pat++;
			continue;
		} else if (*pat == '[')	/* Character classes/Ranges */
			if (__match_charclass(pat + 1, *str, &pat)) {
				str++;
				continue;
			} else
				return false;
		else if (*pat == '\\') /* Escaped char match as normal char */
			pat++;
		if (*str++ != *pat++)
			return false;
	}
	/* Check wild card */
	if (*pat == '*') {
		while (*pat == '*')
			pat++;
		if (!*pat)	/* Tail wild card matches all */
			return true;
		while (*str)
			if (__match_glob(str++, pat, ignore_space))
				return true;
	}
	return !*str && !*pat;
}

/**
 * strglobmatch - glob expression pattern matching
 * @str: the target string to match
 * @pat: the pattern string to match
 *
 * This returns true if the @str matches @pat. @pat can includes wildcards
 * ('*','?') and character classes ([CHARS], complementation and ranges are
 * also supported). Also, this supports escape character ('\') to use special
 * characters as normal character.
 *
 * Note: if @pat syntax is broken, this always returns false.
 */
bool strglobmatch(const char *str, const char *pat)
{
	return __match_glob(str, pat, false);
}

#define handle_error(str) do { perror(str); exit(-1); } while(0)

#define KALLSYMS_PATH "/proc/kallsyms"
/*
 * read kernel symbol from /proc/kallsyms
 */
int kallsyms_parse(void *arg,
		   int(*process_symbol)(void *arg, const char *name,
		   char type, unsigned long start))
{
	int ret = 0;
	FILE *file;
	char *line = NULL;

	file = fopen(KALLSYMS_PATH, "r");
	if (file == NULL)
		handle_error("open " KALLSYMS_PATH " failed");

	while (!feof(file)) {
		char *symbol_addr, *symbol_name;
		char symbol_type;
		unsigned long start;
		int line_len;
		size_t n;

		line_len = getline(&line, &n, file);
		if (line_len < 0 || !line)
			break;

		line[--line_len] = '\0'; /* \n */

		symbol_addr = strtok(line, " \t");
		start = strtoul(symbol_addr, NULL, 16);

		symbol_type = *strtok(NULL, " \t");
		symbol_name = strtok(NULL, " \t");

		ret = process_symbol(arg, symbol_name, symbol_type, start);
		if (ret)
			break;
	}

	free(line);
	fclose(file);

	return ret;
}

struct ksym_addr_t {
	const char *name;
	unsigned long addr;
};

static int symbol_cmp(void *arg, const char *name, char type,
		      unsigned long start)
{
	struct ksym_addr_t *base = arg;

	if (strcmp(base->name, name) == 0) {
		base->addr = start;
		return 1;
	}

	return 0;
}

unsigned long find_kernel_symbol(const char *symbol)
{
	int ret;
	struct ksym_addr_t arg = {
		.name = symbol,
		.addr = 0
	};

	ret = kallsyms_parse(&arg, symbol_cmp);
	if (ret < 0 || arg.addr == 0) {
		fprintf(stderr, "cannot read kernel symbol \"%s\" in %s\n",
			symbol, KALLSYMS_PATH);
		exit(EXIT_FAILURE);
	}

	return arg.addr;
}


#define AVAILABLE_EVENTS_PATH "/sys/kernel/debug/tracing/available_events"

void list_available_events(const char *match)
{
	FILE *file;
	char *line = NULL;

	file = fopen(AVAILABLE_EVENTS_PATH, "r");
	if (file == NULL)
		handle_error("open " AVAILABLE_EVENTS_PATH " failed");

	while (!feof(file)) {
		int line_len;
		size_t n;

		line_len = getline(&line, &n, file);
		if (line_len < 0 || !line)
			break;

		if (!match || strglobmatch(line, match))
			printf("%s", line);
	}

	free(line);
	fclose(file);
}

void process_available_tracepoints(const char *sys, const char *event,
				   int (*process)(const char *sys,
						  const char *event))
{
	char *line = NULL;
	FILE *file;
	char str[128] = {0};

	/* add '\n' into tail */
	snprintf(str, 64, "%s:%s\n", sys, event);

	file = fopen(AVAILABLE_EVENTS_PATH, "r");
	if (file == NULL)
		handle_error("open " AVAILABLE_EVENTS_PATH " failed");

	while (!feof(file)) {
		int line_len;
		size_t n;

		line_len = getline(&line, &n, file);
		if (line_len < 0 || !line)
			break;

		if (strglobmatch(line, str)) {
			char match_sys[64] = {0};
			char match_event[64] = {0};
			char *sep;

			sep = strchr(line, ':');
			memcpy(match_sys, line, sep - line);
			memcpy(match_event, sep + 1,
					    line_len - (sep - line) - 2);

			if (process(match_sys, match_event))
				break;
		}
	}

	free(line);
	fclose(file);
}

