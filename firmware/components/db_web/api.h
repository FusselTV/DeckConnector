#pragma once

#include <stddef.h>

/*
 * What the HTTP handlers and the web socket share. Each takes a JSON body, writes the JSON reply and returns
 * the HTTP status. Not reentrant: the web server runs one handler at a time.
 */
int api_config(const char *body, char *reply, size_t cap);
int api_input(const char *body, char *reply, size_t cap);
