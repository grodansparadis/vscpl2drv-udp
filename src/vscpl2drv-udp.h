// Copyright (c) 2000-2026 Ake Hedman and contributors, the VSCP Project
// SPDX-License-Identifier: LGPL-2.0-or-later
#pragma once
#include <vscp.h>

extern "C" {
long
VSCPOpen(const char *path, const char *guid);
int
VSCPClose(long handle);
int
VSCPWrite(long handle, const vscpEvent *event, unsigned long timeout);
int
VSCPRead(long handle, vscpEvent *event, unsigned long timeout);
unsigned long
VSCPGetVersion();
}
