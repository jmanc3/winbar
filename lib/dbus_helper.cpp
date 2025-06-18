//
// Created by jmanc3 on 6/16/25.
//

#include "dbus_helper.h"
#include <cstdint>
#include <iostream>


// Forward declarations
void append_any(DBusMessageIter *iter, const std::any &val);

// Helper to parse basic types
template<typename T>
std::any parse_basic(DBusMessageIter *iter) {
    T val;
    dbus_message_iter_get_basic(iter, &val);
    if constexpr (std::is_same_v<T, dbus_bool_t>) {
        return bool(val);
    } else if constexpr (std::is_same_v<T, const char *>) {
        return std::string(val);
    } else {
        return val;
    }
}

std::any parse_iter(DBusMessageIter *iter) {
    int type = dbus_message_iter_get_arg_type(iter);
    switch (type) {
        case DBUS_TYPE_INT32:
            return parse_basic<int32_t>(iter);
        case DBUS_TYPE_UINT32:
            return parse_basic<uint32_t>(iter);
        case DBUS_TYPE_INT64:
            return parse_basic<int64_t>(iter);
        case DBUS_TYPE_UINT64:
            return parse_basic<uint64_t>(iter);
        case DBUS_TYPE_BYTE:
            return parse_basic<uint8_t>(iter);
        case DBUS_TYPE_BOOLEAN:
            return parse_basic<dbus_bool_t>(iter);
        case DBUS_TYPE_DOUBLE:
            return parse_basic<double>(iter);
        case DBUS_TYPE_STRING:
        case DBUS_TYPE_OBJECT_PATH:
        case DBUS_TYPE_SIGNATURE:
            return parse_basic<const char *>(iter);
        
        case DBUS_TYPE_ARRAY: {
            DBusMessageIter sub;
            dbus_message_iter_recurse(iter, &sub);
            
            int elem_type = dbus_message_iter_get_element_type(iter);
            
            if (elem_type == DBUS_TYPE_DICT_ENTRY) {
                DbusDict dict;
                while (dbus_message_iter_get_arg_type(&sub) != DBUS_TYPE_INVALID) {
                    DBusMessageIter entry;
                    dbus_message_iter_recurse(&sub, &entry);
                    
                    std::any key = parse_iter(&entry);
                    dbus_message_iter_next(&entry);
                    std::any val = parse_iter(&entry);
                    
                    dict.map.emplace_back(std::move(key), std::move(val));
                    
                    dbus_message_iter_next(&sub);
                }
                return dict;
            } else {
                DbusArray array;
                while (dbus_message_iter_get_arg_type(&sub) != DBUS_TYPE_INVALID) {
                    array.elements.push_back(parse_iter(&sub));
                    dbus_message_iter_next(&sub);
                }
                return array;
            }
        }
        
        case DBUS_TYPE_VARIANT: {
            DBusMessageIter sub;
            dbus_message_iter_recurse(iter, &sub);
            return parse_iter(&sub);
        }
        
        case DBUS_TYPE_STRUCT: {
            DbusStruct s;
            DBusMessageIter sub;
            dbus_message_iter_recurse(iter, &sub);
            while (dbus_message_iter_get_arg_type(&sub) != DBUS_TYPE_INVALID) {
                s.members.push_back(parse_iter(&sub));
                dbus_message_iter_next(&sub);
            }
            return s;
        }
        
        default:
            return std::any();  // empty any
    }
}

std::any parse_iter_message(DBusMessage* msg) {
    DBusMessageIter args;
    dbus_message_iter_init(msg, &args);
    dbus_message_iter_next(&args);
    return parse_iter(&args);
}

/**
 *
 * return
 *
 * int32_t,
 * uint32_t,
 * int64_t,
 * uint64_t,
 * uint8_t,
 * bool,
 * double,
 * std::string, (DBUS_TYPE_STRING, DBUS_TYPE_OBJECT_PATH, DBUS_TYPE_SIGNATURE)
 * DbusArray,
 * DbusDict,
 * DbusStruct
 *
 * example usage:
 *
 * if for instance you know the msg type is
 *
 * ARRAY of DICT_ENTRY<OBJPATH,ARRAY of DICT_ENTRY<STRING,ARRAY of DICT_ENTRY<STRING,VARIANT>>>
 *
 * you can pull out what you need as follows, for example
 *
    std::any val = parse_message(msg);
    
    if (auto dict = std::any_cast<DbusDict>(&val)) {
        for (const auto &item: dict->map) {
            if (auto object_path = std::any_cast<std::string>(&item.first)) {
                printf("object_path: %s\n", object_path->c_str());
            }
            
            // try casting item.second to a DBusArray and keep going
        }
    }

 *
 */
// NOTE: we don't return string type info, just std::string in the case of object path and signature
std::any parse_message(DBusMessage *msg) {
    DBusMessageIter iter;
    if (dbus_message_iter_init(msg, &iter)) {
        return parse_iter(&iter);
    }
    return std::any();  // empty any
}


// Function to write DBusMessage to /tmp/out
bool write_dbus_message(DBusMessage *msg) {
    if (!msg) return false;
    
    uint8_t *buffer = nullptr;
    int size = 0;
    
    if (!dbus_message_marshal(msg, reinterpret_cast<char **>(&buffer), &size)) {
        fprintf(stderr, "Failed to marshal DBusMessage\n");
        return false;
    }
    
    FILE *file = fopen("/tmp/out", "wb");
    if (!file) {
        fprintf(stderr, "Failed to open /tmp/out for writing\n");
        dbus_free(buffer);
        return false;
    }
    
    size_t written = fwrite(buffer, 1, size, file);
    fclose(file);
    dbus_free(buffer);
    
    if (written != (size_t) size) {
        fprintf(stderr, "Failed to write complete message\n");
        return false;
    }
    
    printf("Wrote %d bytes to /tmp/out\n", size);
    return true;
}

Msg method_call(DBusConnection *con, std::string bus, std::string path, std::string iface, std::string method) {
    DBusMessage *msg = dbus_message_new_method_call(bus.c_str(),
                                                    path.c_str(),
                                                    iface.c_str(),
                                                    method.c_str());
    
    DBusError error;
    dbus_error_init(&error);
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(con, msg, 50, &error);
    if (dbus_error_is_set(&error)) {
        std::cerr << "Failed: " << error.message << std::endl;
        dbus_error_free(&error);
        dbus_message_unref(msg);
        return {};
    }
    dbus_message_unref(msg);
    return {reply};
}

void append_basic(DBusMessageIter *iter, const std::any &val) {
    if (val.type() == typeid(int32_t)) {
        int32_t v = std::any_cast<int32_t>(val);
        dbus_message_iter_append_basic(iter, DBUS_TYPE_INT32, &v);
    } else if (val.type() == typeid(uint32_t)) {
        uint32_t v = std::any_cast<uint32_t>(val);
        dbus_message_iter_append_basic(iter, DBUS_TYPE_UINT32, &v);
    } else if (val.type() == typeid(int64_t)) {
        int64_t v = std::any_cast<int64_t>(val);
        dbus_message_iter_append_basic(iter, DBUS_TYPE_INT64, &v);
    } else if (val.type() == typeid(uint64_t)) {
        uint64_t v = std::any_cast<uint64_t>(val);
        dbus_message_iter_append_basic(iter, DBUS_TYPE_UINT64, &v);
    } else if (val.type() == typeid(uint8_t)) {
        uint8_t v = std::any_cast<uint8_t>(val);
        dbus_message_iter_append_basic(iter, DBUS_TYPE_BYTE, &v);
    } else if (val.type() == typeid(bool)) {
        dbus_bool_t v = std::any_cast<bool>(val);
        dbus_message_iter_append_basic(iter, DBUS_TYPE_BOOLEAN, &v);
    } else if (val.type() == typeid(double)) {
        double v = std::any_cast<double>(val);
        dbus_message_iter_append_basic(iter, DBUS_TYPE_DOUBLE, &v);
    } else if (val.type() == typeid(DBusStrString)) {
        std::string v = std::any_cast<DBusStrString>(val).value;
        dbus_message_iter_append_basic(iter, DBUS_TYPE_STRING, &v);
    } else if (val.type() == typeid(DBusStrObjectPath)) {
        std::string v = std::any_cast<DBusStrObjectPath>(val).value;
        dbus_message_iter_append_basic(iter, DBUS_TYPE_OBJECT_PATH, &v);
    } else if (val.type() == typeid(DBusStrSignature)) {
        std::string v = std::any_cast<DBusStrSignature>(val).value;
        dbus_message_iter_append_basic(iter, DBUS_TYPE_SIGNATURE, &v);
    } else {
        // Could not append; you may choose to throw or log
    }
}

void append_array(DBusMessageIter *iter, const DbusArray &array) {
    DBusMessageIter sub;
    dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, NULL, &sub);
    for (const auto &elem: array.elements) {
        append_any(&sub, elem);
    }
    dbus_message_iter_close_container(iter, &sub);
}

void append_dict(DBusMessageIter *iter, const DbusDict &dict) {
    DBusMessageIter sub;
    dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, dict.sig.c_str(), &sub);
    for (const auto &pair: dict.map) {
        DBusMessageIter entry;
        dbus_message_iter_open_container(&sub, DBUS_TYPE_DICT_ENTRY, "{sv}", &entry);
        append_any(&entry, pair.first);
        append_any(&entry, pair.second);
        dbus_message_iter_close_container(&sub, &entry);
    }
    dbus_message_iter_close_container(iter, &sub);
}

void append_struct(DBusMessageIter *iter, const DbusStruct &s) {
    DBusMessageIter sub;
    dbus_message_iter_open_container(iter, DBUS_TYPE_STRUCT, NULL, &sub);
    for (const auto &m: s.members) {
        append_any(&sub, m);
    }
    dbus_message_iter_close_container(iter, &sub);
}

void append_variant(DBusMessageIter *iter, const std::any &val) {
    DBusMessageIter sub;
    dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "v", &sub);
    append_any(&sub, val);
    dbus_message_iter_close_container(iter, &sub);
}

void append_any(DBusMessageIter *iter, const std::any &val) {
    if (val.type() == typeid(int32_t) ||
        val.type() == typeid(uint32_t) ||
        val.type() == typeid(int64_t) ||
        val.type() == typeid(uint64_t) ||
        val.type() == typeid(uint8_t) ||
        val.type() == typeid(bool) ||
        val.type() == typeid(double) ||
        val.type() == typeid(DBusStrString) ||
        val.type() == typeid(DBusStrObjectPath) ||
        val.type() == typeid(DBusStrSignature)) {
        append_basic(iter, val);
    } else if (val.type() == typeid(DbusArray)) {
        append_array(iter, std::any_cast<const DbusArray &>(val));
    } else if (val.type() == typeid(DbusDict)) {
        append_dict(iter, std::any_cast<const DbusDict &>(val));
    } else if (val.type() == typeid(DbusStruct)) {
        append_struct(iter, std::any_cast<const DbusStruct &>(val));
    } else if (val.type() == typeid(DBusVariantString)) {
        DBusMessageIter sub;
        dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "s", &sub);
        const char *str = std::any_cast<const DBusVariantString &>(val).value.c_str();
        dbus_message_iter_append_basic(&sub, DBUS_TYPE_STRING, &str);
        dbus_message_iter_close_container(iter, &sub);
    } else if (val.type() == typeid(DBusVariantBool)) {
        DBusMessageIter sub;
        dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "b", &sub);
        dbus_bool_t v = std::any_cast<const DBusVariantBool &>(val).value;
        dbus_message_iter_append_basic(&sub, DBUS_TYPE_BOOLEAN, &v);
        dbus_message_iter_close_container(iter, &sub);
    } else if (val.type() == typeid(DBusVariantByteArray)) {
        DBusMessageIter sub;
        dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "ay", &sub);
        DBusMessageIter arr;
        dbus_message_iter_open_container(&sub, DBUS_TYPE_ARRAY, "y", &arr);
        const auto &vec = std::any_cast<const DBusVariantByteArray &>(val).value;
        for (auto b: vec) {
            dbus_message_iter_append_basic(&arr, DBUS_TYPE_BYTE, &b);
        }
        dbus_message_iter_close_container(&sub, &arr);
        dbus_message_iter_close_container(iter, &sub);
    } else {
        // Could not append; fallback? throw? log?
    }
}

DBusMessage *
build_message(const char *dest, const char *path, const char *iface, const char *method, const std::any &payload) {
    DBusMessage *msg = dbus_message_new_method_call(dest, path, iface, method);
    if (!msg) return nullptr;
    
    if (payload.has_value()) {
        DBusMessageIter iter;
        dbus_message_iter_init_append(msg, &iter);
        append_any(&iter, payload);
    }
    
    return msg;
}

DBusMessage* build_message(const char *dest, const char *path, const char *iface, const char *method, const std::vector<std::any> &payloads) {
    DBusMessage *msg = dbus_message_new_method_call(dest, path, iface, method);
    if (!msg) return nullptr;
    
    DBusMessageIter iter;
    dbus_message_iter_init_append(msg, &iter);
    
    for (const auto &payload : payloads) {
        append_any(&iter, payload);
    }
    
    return msg;
}

