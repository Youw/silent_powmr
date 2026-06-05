#pragma once
// Tuya device credentials. Put the REAL values in secrets.h (git-ignored) by
// copying secrets.example.h. These come from your product on the Tuya IoT
// platform (PID) and a device license (UUID + AuthKey).
#if defined(__has_include)
#  if __has_include("secrets.h")
#    include "secrets.h"
#  endif
#endif

#ifndef TUYA_PRODUCT_ID
#define TUYA_PRODUCT_ID "xxxxxxxxxxxxxxxx"
#endif
#ifndef TUYA_OPENSDK_UUID
#define TUYA_OPENSDK_UUID "uuidxxxxxxxxxxxxxxxx"
#endif
#ifndef TUYA_OPENSDK_AUTHKEY
#define TUYA_OPENSDK_AUTHKEY "keyxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
#endif
