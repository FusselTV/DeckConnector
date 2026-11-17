#pragma once

#include <stdbool.h>

/* Firmware versions as `git describe` names them: v1.2.3, v1.2.3-rc4, and builds after a tag such as
 * v1.2.3-rc4-7-gabc1234-dirty, which count as newer than that tag. */

/* <0, 0 or >0 like strcmp; a name that is no version sorts below every version. */
int db_version_cmp(const char *a, const char *b);

/* A release candidate or other prerelease (v1.2.3-rc4), also a build after one. */
bool db_version_pre(const char *v);
