/**
 *  Copyright (C) 2015 Topology LP
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include "daemon.hpp"
#include "daemon_options.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

struct option_spec {
    const char* long_name;
    char short_name;              // 0 when the option has no short form
    bool takes_value;
    const char* value_name;       // shown in help; null when no value
    const char* description;
};

const option_spec OPTIONS[] = {
    { "help",           'h', false, nullptr,   "produce help message" },
    { "realm",          'r', true,  "arg",     "set the WAMP realm for this router" },
    { "websocket-port", 'w', true,  "arg=<port>",  "enable websocket transport on the given port" },
    { "rawsocket-port", 't', true,  "arg=<port>",  "enable rawsocket transport on the given port" },
    { "rawsocket-path", 'u', true,  "arg=<path>",  "enable rawsocket transport on the given path" },
    { "no-json",         0, false, nullptr,   "disable JSON serialization" },
    { "no-msgpack",      0, false, nullptr,   "disable msgpack serialization" },
    { "debug",         'd', false, nullptr,   "enable debugging" },
};

const option_spec* find_long(const std::string& name)
{
    for (const option_spec& spec : OPTIONS) {
        if (name == spec.long_name) {
            return &spec;
        }
    }
    return nullptr;
}

const option_spec* find_short(char name)
{
    for (const option_spec& spec : OPTIONS) {
        if (spec.short_name == name) {
            return &spec;
        }
    }
    return nullptr;
}

void print_help()
{
    std::cout << "Allowed options:" << std::endl;
    for (const option_spec& spec : OPTIONS) {
        std::string names = "  ";
        if (spec.short_name != 0) {
            names += std::string("-") + spec.short_name + " [ ";
            names += std::string("--") + spec.long_name + " ]";
        } else {
            names += std::string("--") + spec.long_name;
        }
        if (spec.takes_value) {
            names += std::string(" ") + spec.value_name;
        }
        names += std::string(names.size() < 36 ? 36 - names.size() : 1, ' ');
        std::cout << names << spec.description << std::endl;
    }
}

bool parse_port(const std::string& text, std::uint16_t& port)
{
    if (text.empty()) {
        return false;
    }
    char* end = nullptr;
    unsigned long value = std::strtoul(text.c_str(), &end, 10);
    if (end != text.c_str() + text.size() || value > 65535UL) {
        return false;
    }
    port = static_cast<std::uint16_t>(value);
    return true;
}

} // anonymous namespace

int main(int argc, char** argv)
{
    // Hand-rolled parser replacing boost::program_options (BOOST-REMOVAL.md
    // step 2e'). Recognized shapes: --name, --name=value, -n, -nvalue and
    // -n value; anything else (including bare positionals and values
    // missing or mistyped) reports "Unrecognized options." and exits 1,
    // exactly like the previous try/catch around po::parse_command_line.
    bool help = false;
    bool error = false;

    bonefish::daemon_options options;
    std::string realm;
    std::uint16_t websocket_port = 0;
    std::uint16_t rawsocket_port = 0;
    std::string rawsocket_path;
    bool debug_enabled = false;
    bool websocket_enabled = false;
    bool rawsocket_enabled = false;
    bool json_enabled = true;
    bool msgpack_enabled = true;

    for (int i = 1; i < argc && !error; ++i) {
        const std::string arg(argv[i]);
        const option_spec* spec = nullptr;
        std::string value;
        bool have_value = false;

        if (arg.size() > 2 && arg.compare(0, 2, "--") == 0) {
            std::string name = arg.substr(2);
            std::string::size_type eq = name.find('=');
            if (eq != std::string::npos) {
                value = name.substr(eq + 1);
                name = name.substr(0, eq);
                have_value = true;
            }
            spec = find_long(name);
            if (spec == nullptr) {
                error = true;
                break;
            }
        } else if (arg.size() > 1 && arg[0] == '-' && arg != "--") {
            spec = find_short(arg[1]);
            if (spec == nullptr) {
                error = true;
                break;
            }
            if (arg.size() > 2) {
                value = arg.substr(2);
                have_value = true;
            }
        } else {
            // Bare positionals: boost::parse_command_line silently accepted
            // (and ignored) unregistered positional arguments, so ignore
            // them here as well instead of erroring.
            continue;
        }

        if (spec->takes_value) {
            if (!have_value) {
                if (i + 1 == argc) {
                    error = true;
                    break;
                }
                value = argv[++i];
            }
        } else if (have_value || arg[0] != '-') {
            // value attached to a flag-style option: parse error in boost
            error = true;
            break;
        }

        if (spec->short_name == 'h' || std::string(spec->long_name) == "help") {
            help = true;
        } else if (std::string(spec->long_name) == "realm") {
            realm = value;
        } else if (std::string(spec->long_name) == "websocket-port") {
            if (!parse_port(value, websocket_port)) {
                error = true;
                break;
            }
            websocket_enabled = true;
        } else if (std::string(spec->long_name) == "rawsocket-port") {
            if (!parse_port(value, rawsocket_port)) {
                error = true;
                break;
            }
            rawsocket_enabled = true;
        } else if (std::string(spec->long_name) == "rawsocket-path") {
            rawsocket_path = value;
            rawsocket_enabled = true;
        } else if (std::string(spec->long_name) == "no-json") {
            json_enabled = false;
        } else if (std::string(spec->long_name) == "no-msgpack") {
            msgpack_enabled = false;
        } else if (std::string(spec->long_name) == "debug") {
            debug_enabled = true;
        }
    }

    if (error) {
        std::cout << "Unrecognized options.\n\n";
    }

    if (error || help) {
        print_help();
        return 1;
    }

    options.set_debug_enabled(debug_enabled);
    options.set_websocket_enabled(websocket_enabled);
    options.set_websocket_port(websocket_port);
    options.set_rawsocket_enabled(rawsocket_enabled);
    options.set_rawsocket_port(rawsocket_port);
    options.set_rawsocket_path(rawsocket_path);
    options.set_json_serialization_enabled(json_enabled);
    options.set_msgpack_serialization_enabled(msgpack_enabled);
    if (!realm.empty()) {
        options.set_realm(realm);
    }

    bonefish::daemon daemon(options);
    daemon.run();
    return 0;
}
