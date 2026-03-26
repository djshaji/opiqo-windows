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
#ifndef __SAMPLE_ANDROID_DEBUG_H__
#define __SAMPLE_ANDROID_DEBUG_H__

#define LOGV(...) printf(__VA_ARGS__); printf("\n");
#define LOGD(...) printf(__VA_ARGS__); printf("\n");
#define LOGI(...) printf(__VA_ARGS__); printf("\n");
#define LOGW(...) printf(__VA_ARGS__); printf("\n");
#define LOGE(...) printf(__VA_ARGS__); printf("\n");
#define LOGF(...) printf(__VA_ARGS__); printf("\n");

#define HERE printf("[here]: %s:%d\n", __FILE__, __LINE__);
#define IN printf(">> %s\n", __PRETTY_FUNCTION__);
#define OUT printf("<< %s\n", __PRETTY_FUNCTION__);

//#define HERE LOGE ("[here]: %s:%d", __FILE__, __LINE__);
#define ASSERT(cond, ...) if (!(cond)) {printf("Assertion failed: %s\n", #cond);}

#endif // __SAMPLE_ANDROID_DEBUG_H__
