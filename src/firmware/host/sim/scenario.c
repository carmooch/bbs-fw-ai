#define _POSIX_C_SOURCE 200809L

#include "scenario.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* input_names[INPUT_COUNT] =
{
	"cadence", "gear", "speed", "throttle", "brake", "level", "voltage", "rider_w", "grade"
};

static const char* output_names[OUTPUT_COUNT] = { "current", "motor", "fw_cadence", "target_speed" };
static const char* op_names[] = { "==", "!=", "<", "<=", ">", ">=" };

const char* input_name(input_t input) { return input_names[input]; }
const char* output_name(output_t output) { return output_names[output]; }
const char* op_name(op_t op) { return op_names[op]; }

static int lookup(const char* word, const char** names, int count)
{
	for (int i = 0; i < count; ++i)
	{
		if (strcmp(word, names[i]) == 0)
		{
			return i;
		}
	}
	return -1;
}

static bool parse_number(const char* word, double* out)
{
	char* end = NULL;
	*out = strtod(word, &end);
	return word[0] != '\0' && *end == '\0';
}

static bool parse_ms(const char* word, uint32_t* out)
{
	double v;
	if (!parse_number(word, &v) || v < 0)
	{
		return false;
	}
	*out = (uint32_t)v;
	return true;
}

#define FAIL(...) \
	do { \
		fprintf(stderr, "%s:%d: ", path, line_no); \
		fprintf(stderr, __VA_ARGS__); \
		fprintf(stderr, "\n"); \
		fclose(f); \
		return false; \
	} while (0)

#define CHECK_ROOM(count) \
	do { if ((count) >= SCENARIO_MAX_ITEMS) FAIL("too many statements of this kind"); } while (0)

static int compare_events(const void* a, const void* b)
{
	const event_t* ea = a;
	const event_t* eb = b;
	if (ea->at_ms != eb->at_ms)
	{
		return ea->at_ms < eb->at_ms ? -1 : 1;
	}
	// keep file order for events at the same time
	return ea->line - eb->line;
}

bool scenario_load(const char* path, scenario_t* s)
{
	memset(s, 0, sizeof(*s));
	snprintf(s->path, sizeof(s->path), "%s", path);

	FILE* f = fopen(path, "r");
	if (!f)
	{
		fprintf(stderr, "%s: cannot open\n", path);
		return false;
	}

	char buf[512];
	int line_no = 0;
	while (fgets(buf, sizeof(buf), f))
	{
		line_no++;

		char* hash = strchr(buf, '#');
		if (hash)
		{
			*hash = '\0';
		}

		// split into words
		char* words[64];
		int n = 0;
		for (char* tok = strtok(buf, " \t\r\n"); tok && n < 64; tok = strtok(NULL, " \t\r\n"))
		{
			words[n++] = tok;
		}

		if (n == 0)
		{
			continue;
		}

		if (strcmp(words[0], "title") == 0)
		{
			// the title is the rest of the line, re-joined
			s->title[0] = '\0';
			for (int i = 1; i < n; ++i)
			{
				if (i > 1)
				{
					strncat(s->title, " ", sizeof(s->title) - strlen(s->title) - 1);
				}
				strncat(s->title, words[i], sizeof(s->title) - strlen(s->title) - 1);
			}
		}
		else if (strcmp(words[0], "end") == 0)
		{
			if (n != 2 || !parse_ms(words[1], &s->end_ms))
				FAIL("expected: end <ms>");
		}
		else if (strcmp(words[0], "config") == 0)
		{
			CHECK_ROOM(s->num_overrides);
			config_override_t* o = &s->overrides[s->num_overrides++];
			double v, level;
			o->line = line_no;

			if (n == 5 && strcmp(words[1], "level") == 0)
			{
				if (!parse_number(words[2], &level) || level < 0 || level > 9)
					FAIL("assist level must be 0-9");
				if (!parse_number(words[4], &v))
					FAIL("expected a number, got '%s'", words[4]);
				o->level = (int)level;
				snprintf(o->field, sizeof(o->field), "%s", words[3]);
				o->value = (long)v;
			}
			else if (n == 3)
			{
				if (!parse_number(words[2], &v))
					FAIL("expected a number, got '%s'", words[2]);
				o->level = -1;
				snprintf(o->field, sizeof(o->field), "%s", words[1]);
				o->value = (long)v;
			}
			else
			{
				FAIL("expected: config <field> <value> | config level <n> <field> <value>");
			}
		}
		else if (strcmp(words[0], "bike") == 0)
		{
			CHECK_ROOM(s->num_bike_params);
			bike_param_t* p = &s->bike_params[s->num_bike_params++];
			if (n != 3 || !parse_number(words[2], &p->value))
				FAIL("expected: bike <param> <value>");
			snprintf(p->param, sizeof(p->param), "%s", words[1]);
			p->line = line_no;
		}
		else if (strcmp(words[0], "at") == 0)
		{
			CHECK_ROOM(s->num_events);
			event_t* e = &s->events[s->num_events++];
			e->line = line_no;

			if ((n != 4 && n != 6) || !parse_ms(words[1], &e->at_ms))
				FAIL("expected: at <ms> <input> <value> [over <ms>]");

			int input = lookup(words[2], input_names, INPUT_COUNT);
			if (input < 0)
				FAIL("unknown input '%s'", words[2]);
			e->input = (input_t)input;

			if (!parse_number(words[3], &e->value))
				FAIL("expected a number, got '%s'", words[3]);

			if (n == 6)
			{
				if (strcmp(words[4], "over") != 0 || !parse_ms(words[5], &e->over_ms))
					FAIL("expected: over <ms>");
			}
		}
		else if (strcmp(words[0], "expect") == 0)
		{
			CHECK_ROOM(s->num_expects);
			expect_t* x = &s->expects[s->num_expects++];
			x->line = line_no;

			if (n != 5 || !parse_ms(words[1], &x->at_ms))
				FAIL("expected: expect <ms> <output> <op> <value>");

			int output = lookup(words[2], output_names, OUTPUT_COUNT);
			if (output < 0)
				FAIL("unknown output '%s'", words[2]);
			x->output = (output_t)output;

			int op = lookup(words[3], op_names, 6);
			if (op < 0)
				FAIL("unknown operator '%s'", words[3]);
			x->op = (op_t)op;

			if (!parse_number(words[4], &x->value))
				FAIL("expected a number, got '%s'", words[4]);
		}
		else
		{
			FAIL("unknown statement '%s'", words[0]);
		}
	}

	fclose(f);

	if (s->end_ms == 0)
	{
		fprintf(stderr, "%s: missing 'end <ms>'\n", path);
		return false;
	}

	qsort(s->events, s->num_events, sizeof(event_t), compare_events);
	return true;
}
