/*
 * Copyright 2018 The Android Open Source Project
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
 */

#ifndef OBOE_LIVEEFFECTENGINE_H
#define OBOE_LIVEEFFECTENGINE_H

#include <string>
#include <thread>
#include <mutex>
#include <lilv/lilv.h>
#include "json.hpp"
#include "LockFreeQueue.h"
#include "FileWriter.h"
#include "LV2Plugin.hpp"

using json = nlohmann::json;

class LiveEffectEngine {
public:
    LiveEffectEngine();
    void initLV2();

    LockFreeQueueManager queueManager;
    FileWriter * fileWriter ;

    std::string cacheDir ;
    std::mutex pluginMutex;  // Protects plugin1, plugin2, plugin3, plugin4 access
    LV2Plugin * plugin1 = nullptr;
    LV2Plugin * plugin2 = nullptr;
    LV2Plugin * plugin3 = nullptr;
    LV2Plugin * plugin4 = nullptr;
    LilvInstance *instance = nullptr;
    LilvWorld * world = nullptr;

    LilvNode *audio_class_, *control_class_, *atom_class_, *input_class_, * toggle_class_,
        *patch_writable, *rsz_minimumSize_, *rdfs_label, *rdfs_range, *mod_filetypes, *enum_class_ ;

    const LilvPlugins * plugins = nullptr;
    json pluginInfo;
    int32_t sampleRate = 48000 ;
    float * gain = nullptr ;
    int pluginCount = 0 ;
    bool bypass = false ;
    int blockSize = 4096 ;

    setValue (int p, int index, float value);
    void addPlugin (int position, std::string uri);
    void deletePlugin (int plugin);
    std::string getPresetList ();
    std::string getPreset (int plugin);
    void setFilePath (int position, std::string uri, std::string path) ;
    bool startRecording(int fd, int file_type, int quality);
    void stopRecording();
    int process (float* input, float* output, int frames);

private:
    bool              mIsEffectOn = false;
    int32_t           mSampleRate = 48000;
    const int32_t     mInputChannelCount = 2;
    const int32_t     mOutputChannelCount = 2;
};

#endif  // OBOE_LIVEEFFECTENGINE_H
