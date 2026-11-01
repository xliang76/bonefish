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

// Wire-format guardrails for the WAMP message layer:
//   - message type enum codes and string names
//   - message factory coverage
//   - msgpack + JSON serializer round-trips with exact field order
//   - unmarshal validation errors
// These expectations lock the baseline behavior and must stay green on the
// Boost-free ported tree.

#include "bonefish_test.hpp"

#include <bonefish/messages/wamp_abort_message.hpp>
#include <bonefish/messages/wamp_call_message.hpp>
#include <bonefish/messages/wamp_error_message.hpp>
#include <bonefish/messages/wamp_event_message.hpp>
#include <bonefish/messages/wamp_goodbye_message.hpp>
#include <bonefish/messages/wamp_message_defaults.hpp>
#include <bonefish/messages/wamp_message_factory.hpp>
#include <bonefish/messages/wamp_message_type.hpp>
#include <bonefish/serialization/json_serializer.hpp>
#include <bonefish/serialization/msgpack_serializer.hpp>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <memory>
#include <msgpack.hpp>
#include <stdexcept>
#include <string>
#include <vector>

using namespace bonefish;

namespace {

struct type_code_case {
    wamp_message_type type;
    int code;
    const char* str;
};

// All 25 protocol message types: wire code + lowercase trace name.
const type_code_case type_table[] = {
    { wamp_message_type::HELLO,         1,  "hello" },
    { wamp_message_type::WELCOME,       2,  "welcome" },
    { wamp_message_type::ABORT,         3,  "abort" },
    { wamp_message_type::CHALLENGE,     4,  "challenge" },
    { wamp_message_type::AUTHENTICATE,  5,  "authenticate" },
    { wamp_message_type::GOODBYE,       6,  "goodbye" },
    { wamp_message_type::HEARTBEAT,     7,  "heartbeat" },
    { wamp_message_type::ERROR,         8,  "error" },
    { wamp_message_type::PUBLISH,       16, "publish" },
    { wamp_message_type::PUBLISHED,     17, "published" },
    { wamp_message_type::SUBSCRIBE,     32, "subscribe" },
    { wamp_message_type::SUBSCRIBED,    33, "subscribed" },
    { wamp_message_type::UNSUBSCRIBE,   34, "unsubscribe" },
    { wamp_message_type::UNSUBSCRIBED,  35, "unsubscribed" },
    { wamp_message_type::EVENT,         36, "event" },
    { wamp_message_type::CALL,          48, "call" },
    { wamp_message_type::CANCEL,        49, "cancel" },
    { wamp_message_type::RESULT,        50, "result" },
    { wamp_message_type::REGISTER,      64, "register" },
    { wamp_message_type::REGISTERED,    65, "registered" },
    { wamp_message_type::UNREGISTER,    66, "unregister" },
    { wamp_message_type::UNREGISTERED,  67, "unregistered" },
    { wamp_message_type::INVOCATION,    68, "invocation" },
    { wamp_message_type::INTERRUPT,     69, "interrupt" },
    { wamp_message_type::YIELD,         70, "yield" },
};

// The unpacked objects reference the zone owned by `handle`, so callers must
// keep `handle` alive at least as long as the returned vector (ASan caught
// the dangling-zone read when the handle was dropped at return).
std::vector<msgpack::object> unpack_array(const expandable_buffer& buf,
        msgpack::object_handle& handle)
{
    handle = msgpack::unpack(buf.data(), buf.size());
    return handle.get().as<std::vector<msgpack::object> >();
}

} // anonymous namespace

BF_TEST(message_type_codes_and_strings)
{
    for (const auto& c : type_table) {
        BF_ASSERT_EQ(static_cast<int>(c.type), c.code);
        BF_ASSERT_EQ(std::string(message_type_to_string(c.type)),
                std::string(c.str));
    }
}

BF_TEST(message_type_to_string_unknown_throws)
{
    bool threw = false;
    try {
        message_type_to_string(static_cast<wamp_message_type>(999));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    BF_ASSERT_TRUE(threw);
}

BF_TEST(message_factory_coverage)
{
    // Types the router does not implement must stay unconstructible.
    const wamp_message_type unimplemented[] = {
        wamp_message_type::CHALLENGE, wamp_message_type::AUTHENTICATE,
        wamp_message_type::HEARTBEAT, wamp_message_type::CANCEL,
        wamp_message_type::INTERRUPT
    };
    for (auto type : unimplemented) {
        wamp_message* m = wamp_message_factory::create_message(type);
        BF_ASSERT_TRUE(m == nullptr);
    }
    for (const auto& c : type_table) {
        if (std::find(std::begin(unimplemented), std::end(unimplemented),
                    c.type) != std::end(unimplemented)) {
            continue;
        }
        std::unique_ptr<wamp_message> m(wamp_message_factory::create_message(c.type));
        BF_ASSERT_TRUE(m != nullptr);
        if (m) {
            BF_ASSERT_EQ(m->get_type(), c.type);
        }
    }
}

BF_TEST(call_msgpack_roundtrip_and_wire_layout)
{
    std::vector<int> args_vec;
    args_vec.push_back(2);
    args_vec.push_back(3);
    msgpack::sbuffer args_buf;
    msgpack::pack(args_buf, args_vec);
    msgpack::object_handle args = msgpack::unpack(args_buf.data(), args_buf.size());

    wamp_call_message message;
    message.set_request_id(wamp_request_id(1234));
    message.set_options(msgpack_empty_map());
    message.set_procedure("com.example.add2");
    message.set_arguments(args.get());

    msgpack_serializer serializer;
    expandable_buffer buf = serializer.serialize(message);

    // Exact wire array: [48, 1234, {}, "com.example.add2", [2, 3]]
    msgpack::object_handle unpacked;
    std::vector<msgpack::object> fields = unpack_array(buf, unpacked);
    BF_ASSERT_EQ(fields.size(), static_cast<size_t>(5));
    BF_ASSERT_EQ(fields[0].as<int>(), 48);
    BF_ASSERT_EQ(fields[1].as<uint64_t>(), static_cast<uint64_t>(1234));
    BF_ASSERT_EQ(fields[2].type, msgpack::type::MAP);
    BF_ASSERT_EQ(fields[2].via.map.size, static_cast<uint32_t>(0));
    BF_ASSERT_EQ(fields[3].as<std::string>(), std::string("com.example.add2"));
    std::vector<int> back = fields[4].as<std::vector<int> >();
    BF_ASSERT_EQ(back.size(), static_cast<size_t>(2));
    BF_ASSERT_EQ(back[0], 2);
    BF_ASSERT_EQ(back[1], 3);

    // Round-trip through the router's own deserialize path.
    std::unique_ptr<wamp_message> raw(serializer.deserialize(buf.data(), buf.size()));
    BF_ASSERT_TRUE(raw != nullptr);
    BF_ASSERT_EQ(raw->get_type(), wamp_message_type::CALL);
    wamp_call_message* call = dynamic_cast<wamp_call_message*>(raw.get());
    BF_ASSERT_TRUE(call != nullptr);
    if (call) {
        BF_ASSERT_EQ(call->get_request_id(), wamp_request_id(1234));
        BF_ASSERT_EQ(call->get_procedure(), std::string("com.example.add2"));
        std::vector<int> rt = call->get_arguments().as<std::vector<int> >();
        BF_ASSERT_EQ(rt.size(), static_cast<size_t>(2));
        BF_ASSERT_EQ(rt[0], 2);
        BF_ASSERT_EQ(rt[1], 3);
    }
}

BF_TEST(call_json_roundtrip)
{
    std::vector<int> args_vec;
    args_vec.push_back(2);
    args_vec.push_back(3);
    msgpack::sbuffer args_buf;
    msgpack::pack(args_buf, args_vec);
    msgpack::object_handle args = msgpack::unpack(args_buf.data(), args_buf.size());

    wamp_call_message message;
    message.set_request_id(wamp_request_id(4711));
    message.set_options(msgpack_empty_map());
    message.set_procedure("com.example.echo");
    message.set_arguments(args.get());

    json_serializer serializer;
    expandable_buffer buf = serializer.serialize(message);
    std::unique_ptr<wamp_message> raw(
            serializer.deserialize(buf.data(), buf.size()));
    BF_ASSERT_TRUE(raw != nullptr);
    BF_ASSERT_EQ(raw->get_type(), wamp_message_type::CALL);
    wamp_call_message* call = dynamic_cast<wamp_call_message*>(raw.get());
    BF_ASSERT_TRUE(call != nullptr);
    if (call) {
        BF_ASSERT_EQ(call->get_request_id(), wamp_request_id(4711));
        BF_ASSERT_EQ(call->get_procedure(), std::string("com.example.echo"));
        std::vector<int> rt = call->get_arguments().as<std::vector<int> >();
        BF_ASSERT_EQ(rt.size(), static_cast<size_t>(2));
        BF_ASSERT_EQ(rt[1], 3);
    }
}

BF_TEST(event_wire_field_order)
{
    std::vector<std::string> payload;
    payload.push_back("hello");
    msgpack::sbuffer args_buf;
    msgpack::pack(args_buf, payload);
    msgpack::object_handle args = msgpack::unpack(args_buf.data(), args_buf.size());

    wamp_event_message message;
    message.set_subscription_id(wamp_subscription_id(33));
    message.set_publication_id(wamp_publication_id(77));
    message.set_details(msgpack_empty_map());
    message.set_arguments(args.get());

    msgpack_serializer serializer;
    expandable_buffer buf = serializer.serialize(message);
    msgpack::object_handle unpacked;
    std::vector<msgpack::object> fields = unpack_array(buf, unpacked);
    // [36, Subscription, Publication, Details, Arguments]
    BF_ASSERT_EQ(fields.size(), static_cast<size_t>(5));
    BF_ASSERT_EQ(fields[0].as<int>(), 36);
    BF_ASSERT_EQ(fields[1].as<uint64_t>(), static_cast<uint64_t>(33));
    BF_ASSERT_EQ(fields[2].as<uint64_t>(), static_cast<uint64_t>(77));
    BF_ASSERT_EQ(fields[3].type, msgpack::type::MAP);
    BF_ASSERT_EQ(fields[4].as<std::vector<std::string> >()[0],
            std::string("hello"));
}

BF_TEST(goodbye_abort_wire_layout)
{
    msgpack_serializer serializer;

    wamp_goodbye_message goodbye;
    goodbye.set_details(msgpack_empty_map());
    goodbye.set_reason("wamp.close.normal");
    expandable_buffer gb = serializer.serialize(goodbye);
    msgpack::object_handle gunpack;
    std::vector<msgpack::object> gf = unpack_array(gb, gunpack);
    BF_ASSERT_EQ(gf.size(), static_cast<size_t>(3));
    BF_ASSERT_EQ(gf[0].as<int>(), 6);
    BF_ASSERT_EQ(gf[2].as<std::string>(), std::string("wamp.close.normal"));

    wamp_abort_message abort;
    abort.set_details(msgpack_empty_map());
    abort.set_reason("wamp.error.invalid_roles");
    expandable_buffer ab = serializer.serialize(abort);
    msgpack::object_handle unpack2;
    std::vector<msgpack::object> af = unpack_array(ab, unpack2);
    BF_ASSERT_EQ(af.size(), static_cast<size_t>(3));
    BF_ASSERT_EQ(af[0].as<int>(), 3);
    BF_ASSERT_EQ(af[2].as<std::string>(), std::string("wamp.error.invalid_roles"));
}

BF_TEST(error_wire_layout)
{
    wamp_error_message message;
    message.set_request_type(wamp_message_type::CALL);
    message.set_request_id(wamp_request_id(9));
    message.set_details(msgpack_empty_map());
    message.set_error("wamp.error.no_such_procedure");

    msgpack_serializer serializer;
    expandable_buffer buf = serializer.serialize(message);
    msgpack::object_handle unpacked;
    std::vector<msgpack::object> fields = unpack_array(buf, unpacked);
    // [8, CALL, Request, Details, Error]
    BF_ASSERT_EQ(fields.size(), static_cast<size_t>(5));
    BF_ASSERT_EQ(fields[0].as<int>(), 8);
    BF_ASSERT_EQ(fields[1].as<int>(), 48);
    BF_ASSERT_EQ(fields[2].as<uint64_t>(), static_cast<uint64_t>(9));
    BF_ASSERT_EQ(fields[4].as<std::string>(),
            std::string("wamp.error.no_such_procedure"));
}

BF_TEST(call_unmarshal_validation)
{
    msgpack::zone zone;
    std::vector<msgpack::object> too_few(3, msgpack::object(
                wamp_message_type::CALL));
    wamp_call_message message;
    bool threw = false;
    try {
        message.unmarshal(too_few, msgpack::zone());
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    BF_ASSERT_TRUE(threw);

    // Right field count, wrong message type in field 0.
    msgpack::object hello_obj(wamp_message_type::HELLO);
    std::vector<msgpack::object> wrong_type;
    wrong_type.push_back(hello_obj);
    wrong_type.push_back(msgpack::object(static_cast<uint64_t>(1)));
    wrong_type.push_back(msgpack_empty_map());
    wrong_type.push_back(msgpack::object(std::string("com.example.x"), zone));
    threw = false;
    try {
        message.unmarshal(wrong_type, msgpack::zone());
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    BF_ASSERT_TRUE(threw);

    // Setter validation: arguments must be NIL or ARRAY.
    threw = false;
    try {
        msgpack::object not_an_array(static_cast<uint64_t>(7));
        message.set_arguments(not_an_array);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    BF_ASSERT_TRUE(threw);
}

BF_RUN_ALL("messages")
