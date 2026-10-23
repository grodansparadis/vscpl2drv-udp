// Copyright (c) 2000-2026 Ake Hedman and contributors, the VSCP Project
// SPDX-License-Identifier: GPL-2.0-or-later

// A deliberately incomplete library for exercising driver export validation.
#ifdef _WIN32
__declspec(dllexport)
#endif
unsigned long
VSCPGetVersion(void)
{
  return 0;
}
