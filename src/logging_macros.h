/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <stdio.h>

#ifndef __SAMPLE_ANDROID_DEBUG_H__
#define __SAMPLE_ANDROID_DEBUG_H__

#ifdef _WIN32
// Route all log output through the Windows rate-limited sink.
// win_logging.h is in src/win32/ which is on the include path for the Windows build.
#include "win32/win_logging.h"
#define LOGV(...) opiqo_win_log(__FILE__, __LINE__, "V", __VA_ARGS__)
#define LOGD(...) opiqo_win_log(__FILE__, __LINE__, "D", __VA_ARGS__)
#define LOGI(...) opiqo_win_log(__FILE__, __LINE__, "I", __VA_ARGS__)
#define LOGW(...) opiqo_win_log(__FILE__, __LINE__, "W", __VA_ARGS__)
#define LOGE(...) opiqo_win_log(__FILE__, __LINE__, "E", __VA_ARGS__)
#define LOGF(...) opiqo_win_log(__FILE__, __LINE__, "F", __VA_ARGS__)
#else
#define LOGV(...) printf(__VA_ARGS__); printf("\n");
#define LOGD(...) printf(__VA_ARGS__); printf("\n");
#define LOGI(...) printf(__VA_ARGS__); printf("\n");
#define LOGW(...) printf(__VA_ARGS__); printf("\n");
#define LOGE(...) printf(__VA_ARGS__); printf("\n");
#define LOGF(...) printf(__VA_ARGS__); printf("\n");
#endif

#define HERE printf("[here]: %s:%d\n", __FILE__, __LINE__);
#undef IN
#define IN printf(">> %s\n", __PRETTY_FUNCTION__);
#undef OUT
#define OUT printf("<< %s\n", __PRETTY_FUNCTION__);

//#define HERE LOGE ("[here]: %s:%d", __FILE__, __LINE__);
#define ASSERT(cond, ...) if (!(cond)) {printf("Assertion failed: %s\n", #cond);}

#endif // __SAMPLE_ANDROID_DEBUG_H__
