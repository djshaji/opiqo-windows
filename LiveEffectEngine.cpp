/**
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

#include <cassert>
#include "logging_macros.h"

#include "LiveEffectEngine.h"

LiveEffectEngine::LiveEffectEngine() {
    gain = static_cast<float *>(malloc(sizeof(float)));
    *gain = 1.f ;
    queueManager.init(4096) ;
    fileWriter = new FileWriter(48000, mOutputChannelCount);
    queueManager.add_function(fileWriter->encode);
    initLV2 ();
}

void LiveEffectEngine::initLV2 () {
    world = lilv_world_new();
    audio_class_ = lilv_new_uri(world, LV2_CORE__AudioPort);
    control_class_ = lilv_new_uri(world, LV2_CORE__ControlPort);
    atom_class_ = lilv_new_uri(world, LV2_ATOM__AtomPort);
    input_class_ = lilv_new_uri(world, LV2_CORE__InputPort);
    toggle_class_ = lilv_new_uri(world, LV2_CORE__toggled);
    enum_class_ = lilv_new_uri(world, LV2_CORE__enumeration);
    patch_writable = lilv_new_uri(world, LV2_PATCH__writable);
    rdfs_label     = lilv_new_uri(world, LILV_NS_RDFS "label");
    rdfs_range     = lilv_new_uri(world, LILV_NS_RDFS "range");
    mod_filetypes  = lilv_new_uri(world, "http://moddevices.com/ns/mod#fileTypes");
}

bool LiveEffectEngine::setEffectOn(bool isOn) {
    bool success = true;

    return success;
}

void LiveEffectEngine::setValue (int p, int index, float value) {
    std::lock_guard<std::mutex> lock(pluginMutex);

    LV2Plugin * plugin = nullptr ;
    switch (p) {
        case 1:
            plugin = plugin1;
            break;
        case 2:
            plugin = plugin2;
            break;
        case 3:
            plugin = plugin3;
            break;
        case 4:
            plugin = plugin4;
            break;
        default:
            LOGE("Unknown plugin index %d", p);
            return;
    }

    if (plugin == nullptr) {
        LOGE("Plugin %d is null", p);
        return;
    }

    LOGD("[setValue] Setting plugin %d port %d to value %f", p, index, value);
    plugin->ports_.at (index).control = value;
}

void LiveEffectEngine::addPlugin (int position, std::string uri) {
    bypass = true ;

    const char* uriChars = uri.c_str();
    if (!uriChars) {
        LOGE("addPlugin: failed to get UTF chars for plugin uri");
        bypass = false ;
        return -1;
    }

    {
        std::lock_guard<std::mutex> lock(pluginMutex);

        LV2Plugin * plugin = nullptr ;
        switch (position) {
            case 1:
                plugin = plugin1;
                plugin1 = nullptr;
                break;
            case 2:
                plugin = plugin2;
                plugin2 = nullptr;
                break;
            case 3:
                plugin = plugin3;
                plugin3 = nullptr;
                break;
            case 4:
                plugin = plugin4;
                plugin4 = nullptr;
                break;
            default:
                LOGE("Unknown plugin index %d", position);
                bypass = false ;
                return -1;
        }

        if (plugin != nullptr) {
            plugin ->closePlugin();
            delete plugin;
        }

        plugin = new LV2Plugin(world, uriChars, sampleRate, blockSize);
        LOGD("[plugin %s] Created plugin %s at position %d", lilv_node_as_string(
            lilv_plugin_get_library_uri(plugin->plugin_)) , uriChars, position);

        if ( !plugin->initialize()) {
            LOGE("[load plugin] Failed to initialize plugin %s", uriChars);
            char * libname = const_cast<char *>(basename(
                    lilv_node_as_string(lilv_plugin_get_library_uri(plugin->plugin_))));
            LOGE("[load plugin] Failed to initialize plugin %s from library %s", uriChars, libname);
            void * lib = dlopen (libname, RTLD_NOW | RTLD_LOCAL);
            if (lib == nullptr) {
                LOGE("[load plugin] Failed to load library %s: %s", libname, dlerror());
            } else {
                void (*init_func)(void) = (void (*)(void)) dlsym(lib, "lv2_descriptor");
                if (init_func == nullptr) {
                    LOGE("[load plugin] Failed to find lv2_descriptor in library %s: %s", libname, dlerror());
                } else {
                    LOGD("[debug] Successfully found lv2_descriptor in library %s", libname);
                }

                LOGW("[debug] Successfully loaded library %s manually", libname);
                dlclose(lib);
            }

            bypass = false ;
            return -1;
        } else {
            plugin->start();
            LOGD("Successfully added plugin %s at position %d", uriChars, position);
//            LOGD ("[plugininfo] %s", pluginInfo[uri].dump(4).c_str());
            int portsTotal = lilv_plugin_get_num_ports(plugin->plugin_);
//            LOGD("[debug] Plugin %s has [%d/%d] ports", uriChars, plugin->ports_.size(), portsTotal);
        }

        switch (position) {
            case 1:
                plugin1 = plugin;
                break;
            case 2:
                plugin2 = plugin;
                break;
            case 3:
                plugin3 = plugin;
                break;
            case 4:
                plugin4 = plugin;
                break;
            default:
                LOGE("Unknown plugin index %d", position);
                bypass = false ;
                return -1;
        }
    }


    bypass = false ;
    return 0 ;
}

std::string LiveEffectEngine::initPlugins (std::string dir) {
    std::string path;
    if (!dir.empty()) {
        path = dir;
    } else {
        LOGE("[test] path is empty");
        return "";
    }

    LOGD ("[test] LV2 path set to %s", path.c_str());

    LilvNode* lv2_path = lilv_new_string(world, path.c_str());
    lilv_world_set_option(world, LILV_OPTION_LV2_PATH, lv2_path);
    lilv_node_free(lv2_path);

    lilv_world_load_all(world);

    plugins = lilv_world_get_all_plugins(world);
    pluginInfo = {} ;

    LILV_FOREACH (plugins, i, plugins) {
        const LilvPlugin* p = lilv_plugins_get(plugins, i);
        if (isNoLoadPlugin(lilv_node_as_string(lilv_plugin_get_uri(p)))) {
            LOGD("[initPlugins] Skipping no-load plugin %s", lilv_node_as_uri(lilv_plugin_get_uri(p)));
            continue;
        }

        pluginCount++;
//        LOGD("[plugin] %s\n", lilv_node_as_uri(lilv_plugin_get_uri(p)));
        json pluginInfo = {
                {"name", lilv_node_as_string(lilv_plugin_get_name(p))},
                {"uri", lilv_node_as_string(lilv_plugin_get_uri(p))},
                {"author", lilv_node_as_string(lilv_plugin_get_author_name(p))},
                {"ports", lilv_plugin_get_num_ports(p)}};

        pluginInfo["port"] = {};
        for (int index = 0 ; index < lilv_plugin_get_num_ports(p); index++) {
            const LilvPort* port = lilv_plugin_get_port_by_index(p, index);
//            LOGD ("[%s] Port %d: %s\n", lilv_node_as_string(lilv_plugin_get_name(p)),
//                  index, lilv_node_as_string(lilv_port_get_symbol(p, port)));
            pluginInfo ["port"][index] = {};
            pluginInfo ["port"][index]["index"] = index ;
            pluginInfo ["port"][index]["name"] = lilv_node_as_string(lilv_port_get_name(p, port));
            if (lilv_port_is_a(p, port, audio_class_)) {
//                LOGD("[%s] Port %d is an audio port\n", lilv_node_as_string(lilv_plugin_get_name(p)), index);
                pluginInfo["port"][index]["type"] = "audio";
            }
            else if (lilv_port_is_a(p, port, control_class_) &&
                    lilv_port_is_a(p, port, input_class_)) {
                pluginInfo["port"][index]["type"] = "control";

                if (lilv_port_has_property(p, port, toggle_class_)) {
                    pluginInfo["port"][index]["type"] = "toggled";
                }

                if (lilv_port_has_property(p, port, enum_class_)) {
                    pluginInfo["port"][index]["type"] = "dropdown";

                    // Query Scale Points (Labels for the enum)
                    LilvScalePoints* points = lilv_port_get_scale_points(p, port);
                    pluginInfo["port"][index]["options"] = json::array();

                    LILV_FOREACH(scale_points, s, points) {
                        const LilvScalePoint* point = lilv_scale_points_get(points, s);
                        json sp;
                        sp["label"] = lilv_node_as_string(lilv_scale_point_get_label(point));
                        sp["value"] = lilv_node_as_float(lilv_scale_point_get_value(point));
                        pluginInfo["port"][index]["options"].push_back(sp);
                    }
                }
//                LOGD("[%s] Port %d is a control port\n", lilv_node_as_string(lilv_plugin_get_name(p)), index);
                LilvNode * def = lilv_new_float(world, 0.0f);
                LilvNode * min = lilv_new_float(world, 0.0f);
                LilvNode * max = lilv_new_float(world, 0.0f);
                lilv_port_get_range(p, port, reinterpret_cast<LilvNode **>(&def),
                                    reinterpret_cast<LilvNode **>(&min),
                                    reinterpret_cast<LilvNode **>(&max));
                pluginInfo["port"][index]["min"] = lilv_node_as_float(min) ;
                pluginInfo["port"][index]["max"] = lilv_node_as_string(max);
                pluginInfo["port"][index]["default"] = lilv_node_as_string(def);

            }
            else if (lilv_port_is_a(p, port, atom_class_)) {
                pluginInfo["port"][index]["type"] = "atom";
//                LOGD ("[%s] Port %d is an atom port\n", lilv_node_as_string(lilv_plugin_get_name(p)), index);
            } else {
                LOGW ("[%s] Port %d (%s) is of unknown type\n", lilv_node_as_string(lilv_plugin_get_name(p)), index, lilv_node_as_string(lilv_port_get_symbol(p, port)));
            }

//            LOGI("[plugin ok] Port %d: %s\n", index, lilv_node_as_string(lilv_port_get_symbol(p, port)));
        }

        // probe atom path writable ports
        printf("\n== patch:writable parameters (lv2:Parameter) ==\n");
        const LilvNodes* writables = lilv_plugin_get_value(p, patch_writable);
        json writableParams = {};
        bool hasWritableParams = false ;
        LILV_FOREACH(nodes, i, writables) {
            hasWritableParams = true ;
            json info = {};
            const LilvNode* param = lilv_nodes_get(writables, i); // e.g. rata:Neural_Model

            LilvNodes* labels = lilv_world_find_nodes(world, param, rdfs_label, NULL);
            LilvNodes* ranges = lilv_world_find_nodes(world, param, rdfs_range, NULL);
            LilvNodes* types  = lilv_world_find_nodes(world, param, mod_filetypes, NULL);

            LOGD ("Writable parameter: %s", lilv_node_as_string(param));
            LILV_FOREACH(nodes, j, labels) {
                const LilvNode* label = lilv_nodes_get(labels, j);
                LOGD ("  label: %s", lilv_node_as_string(label));
                info ["label"] = lilv_node_as_string(label);
            }

            LILV_FOREACH(nodes, j, ranges) {
                const LilvNode* range = lilv_nodes_get(ranges, j);
                LOGD ("  range: %s", lilv_node_as_string(range));
                info ["range"] = lilv_node_as_string(range);
            }

            LILV_FOREACH(nodes, j, types) {
                const LilvNode* type = lilv_nodes_get(types, j);
                LOGD ("  type: %s", lilv_node_as_string(type));
                info ["type"] = lilv_node_as_string(type);
            }

            lilv_nodes_free(labels);
            lilv_nodes_free(ranges);
            lilv_nodes_free(types);
            writableParams [lilv_node_as_string(param)] = info ;
        }

        pluginInfo [pluginInfo["uri"]] = pluginInfo;
        if (hasWritableParams) {
            pluginInfo [pluginInfo["uri"]]["writableParams"] = writableParams;
        }
    }

//    LOGD("[plugininfo] %s", pluginInfo.dump(4).c_str());
    LOGD ("[initPlugins] Found %d plugins", pluginCount);
    return to_string (pluginInfo) ;
}

void LiveEffectEngine::deletePlugin (int plugin) {
     bypass = true ;
    {
        std::lock_guard<std::mutex> lock(pluginMutex);

        switch (plugin) {
            case 1:
                if (plugin1) {
                    plugin1->closePlugin();
                    delete plugin1;
                    plugin1 = nullptr;
                }
                break;
            case 2:
                if (plugin2) {
                    plugin2->closePlugin();
                    delete plugin2;
                    plugin2 = nullptr;
                }
                break ;
            case 3:
                if (plugin3) {
                    plugin3->closePlugin();
                    delete plugin3;
                    plugin3 = nullptr;
                }
                break ;
            case 4:
                if (plugin4) {
                    plugin4->closePlugin();
                    delete plugin4;
                    plugin4 = nullptr;
                }
                break;
            default:
                LOGE("Unknown plugin index %d", plugin);
        }
    }

     bypass = false ;
}

void LiveEffectEngine::setPluginEnabled(int plugin, bool is_enabled) {
    {
        std::lock_guard<std::mutex> lock(pluginMutex);

        switch (plugin) {
            case 1:
                if (plugin1) {
                    plugin1->enabled = is_enabled;
                }
                break;
            case 2:
                if (plugin2) {
                    plugin2->enabled = is_enabled;
                }
                break ;
            case 3:
                if (plugin3) {
                    plugin3->enabled = is_enabled;
                }
                break ;
            case 4:
                if (plugin4) {
                    plugin4->enabled = is_enabled;
                }
                break;
            default:
                LOGE("Unknown plugin index %d", plugin);
        }
    }
}

json LiveEffectEngine::getPreset(int plugin) {
    LV2Plugin * p = nullptr ;
    switch (plugin) {
        case 1:
            p = plugin1;
            break;
        case 2:
            p = plugin2;
            break ;
        case 3:
            p = plugin3;
            break ;
        case 4:
            p = plugin4;
            break;
        default:
            LOGE("Unknown plugin index %d", plugin);
            return "{}";
    }

    if (p == nullptr) {
        LOGE("Plugin %d is null", plugin);
        return "{}";
    }

    json preset = {};
    for (const auto& port : p->ports_) {
        preset[port.symbol] = port.control;
    }

    return preset;
}

std::string LiveEffectEngine::getPresetList () {
    json preset = {};
    preset ["app"] = "opiqo-desktop";
    preset ["gain"] = *gain ;
    preset ["plugin1"] = getPreset(1);
    preset ["plugin2"] = getPreset(2);
    preset ["plugin3"] = getPreset(3);
    preset ["plugin4"] = getPreset(4);

    return preset.dump();
}

void LiveEffectEngine::setFilePath (int position, std::string uri, std::string path) {
    LV2Plugin * p = nullptr ;
    switch (position) {
        case 1:
            p = plugin1;
            break;
        case 2:
            p = plugin2;
            break ;
        case 3:
            p = plugin3;
            break ;
        case 4:
            p = plugin4;
            break ;
        default:
            LOGE("Unknown plugin index %d", position);
            return;
    }

    int port = -1 ;
    for (const auto& prt : p->ports_) {
        if (prt.is_atom && prt.is_input) {
            port = prt.index;
            break;
        }
    }

    if (port == -1) {
        LOGE("Plugin %d does not have an input atom port", position);
        return;
    } else {
        LOGD ("Plugin %d atom input port found at index %d", position, port);
    }

    p->send_path_parameter(uri, path);
    std::vector<uint8_t> msg;
    if (p->readAtomMessage("notify", msg)) {
        std::string path, property;
        if (p->extractPathFromAtomMessage(msg.data(), msg.size(), path, &property)) {
            LOGD("Got file path '%s' for property '%s'", path.c_str(), property.c_str());
        } else {
            LOGE("Failed to extract file path from atom message for plugin %d", plugin);
        }
    } else {
        LOGE ("Failed to read atom message from plugin %d after setting file path", plugin);
    }

}

std::string LiveEffectEngine::getWritableParams (int plugin) {
    LV2Plugin * p = nullptr ;
    switch (plugin) {
        case 1:
            p = plugin1;
            break;
        case 2:
            p = plugin2;
            break ;
        case 3:
            p = plugin3;
            break ;
        case 4:
            p = plugin4;
            break ;
        default:
            LOGE("Unknown plugin index %d", plugin);
            return "{}";
    }

    if (p == nullptr) {
        LOGE("Plugin %d is null", plugin);
        return "{}";
    }

    return so_string (p->writables);
}

bool LiveEffectEngine::startRecording(int fd, int file_type, int quality) {
    return fileWriter->open(fd,
                                    static_cast<FileType>(file_type), quality);
}

void LiveEffectEngine::stopRecording() {
    fileWriter->close();
}

int LiveEffectEngine::process (float* input, float* output, int frames) {
    queueManager.process(input, output, frames);
    
    if (bypass) {
        memcpy(output, input, sizeof(float) * frames * mOutputChannelCount);
        return 0;
    }

    std::lock_guard<std::mutex> lock(pluginMutex);

    // Process plugins in order
    for (int i = 1; i <= 4; i++) {
        LV2Plugin* plugin = nullptr;
        switch (i) {
            case 1: plugin = plugin1; break;
            case 2: plugin = plugin2; break;
            case 3: plugin = plugin3; break;
            case 4: plugin = plugin4; break;
        }

        if (plugin && plugin->enabled) {
            plugin->process(input, output, frames);
            // For the next plugin in the chain, the output of the current plugin becomes the input
            input = output;
        }
    }

    return 0;
}

json LiveEffectEngine::getAvailablePlugins() {
    return pluginInfo;
}