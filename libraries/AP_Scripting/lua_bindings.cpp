#include "AP_Scripting_config.h"

#if AP_SCRIPTING_ENABLED

#include <AP_Common/AP_Common.h>
#include <AP_HAL/HAL.h>
#include <AP_Networking/AP_Networking_Config.h>
#if AP_NETWORKING_ENABLED
#include <AP_HAL/utility/Socket.h>
#endif
#include <AP_Logger/AP_Logger.h>
#include <AP_Filesystem/AP_Filesystem.h>

#include "lua_bindings.h"

#include "lua_boxed_numerics.h"
#include <AP_Scripting/lua_generated_bindings.h>

#include <AP_Scheduler/AP_Scheduler.h>
#include <AP_Scripting/AP_Scripting.h>
#include <string.h>

#include "lua/src/lauxlib.h"

extern const AP_HAL::HAL& hal;

extern "C" {
#include "lua/src/lmem.h"
}

extern const AP_HAL::HAL& hal;

// millis
int lua_millis(lua_State *L) {
    binding_argcheck(L, 0);

    *new_uint32_t(L) = AP_HAL::millis();

    return 1;
}

// micros
int lua_micros(lua_State *L) {
    binding_argcheck(L, 0);

    *new_uint32_t(L) = AP_HAL::micros();

    return 1;
}

#if HAL_GCS_ENABLED
int lua_mavlink_init(lua_State *L) {

    // Allow : and . access
    const int arg_offset = (luaL_testudata(L, 1, "mavlink") != NULL) ? 1 : 0;

    binding_argcheck(L, 2+arg_offset);
    // get the depth of receive queue
    const uint32_t queue_size = get_uint32(L, 1+arg_offset, 0, 25);
    // get number of msgs to accept
    const uint32_t num_msgs = get_uint32(L, 2+arg_offset, 0, 25);

    struct AP_Scripting::mavlink &data = AP::scripting()->mavlink_data;
    bool failed = false;
    {
        WITH_SEMAPHORE(data.sem);
        if (data.rx_buffer == nullptr) {
            data.rx_buffer = NEW_NOTHROW ObjectBuffer<struct AP_Scripting::mavlink_msg>(queue_size);
        }
        if (data.accept_msg_ids == nullptr) {
            data.accept_msg_ids = NEW_NOTHROW uint32_t[num_msgs];
        }
        if ((data.rx_buffer == nullptr) || (data.accept_msg_ids == nullptr)) {
            delete data.rx_buffer;
            delete[] data.accept_msg_ids;
            data.rx_buffer = nullptr;
            data.accept_msg_ids = nullptr;
            data.accept_msg_ids_size = 0;
            failed = true;
        } else {
            data.accept_msg_ids_size = num_msgs;
            memset(data.accept_msg_ids, UINT32_MAX, sizeof(int) * num_msgs);
        }
    } // release semaphore here as luaL_error will NOT do that!

    if (failed) {
        return luaL_error(L, "out of memory");
    }

    return 0;
}

int lua_mavlink_receive_chan(lua_State *L) {

    // Allow : and . access
    const int arg_offset = (luaL_testudata(L, 1, "mavlink") != NULL) ? 1 : 0;

    binding_argcheck(L, arg_offset);

    struct AP_Scripting::mavlink_msg msg;
    ObjectBuffer<struct AP_Scripting::mavlink_msg> *rx_buffer = AP::scripting()->mavlink_data.rx_buffer;

    if (rx_buffer == nullptr) {
        return luaL_error(L, "RX not initialized");
    }

    if (rx_buffer->pop(msg)) {
        lua_pushlstring(L, (char *)&msg.msg, sizeof(msg.msg));
        lua_pushinteger(L, msg.chan);
        *new_uint32_t(L) = msg.timestamp_ms;
        return 3;
    } else {
        // no MAVLink to handle, just return no results
        return 0;
    }
}

int lua_mavlink_register_rx_msgid(lua_State *L) {

    // Allow : and . access
    const int arg_offset = (luaL_testudata(L, 1, "mavlink") != NULL) ? 1 : 0;

    binding_argcheck(L, 1+arg_offset);

    const uint32_t msgid = get_uint32(L, 1+arg_offset, 0, (1 << 24) - 1);

    struct AP_Scripting::mavlink &data = AP::scripting()->mavlink_data;

    // check that we aren't currently watching this ID
    for (uint8_t i = 0; i < data.accept_msg_ids_size; i++) {
        if (data.accept_msg_ids[i] == msgid) {
            lua_pushboolean(L, false);
            return 1;
        }
    }

    int i = 0;
    for (i = 0; i < data.accept_msg_ids_size; i++) {
        if (data.accept_msg_ids[i] == UINT32_MAX) {
            break;
        }
    }

    if (i >= data.accept_msg_ids_size) {
        return luaL_error(L, "no registrations free");
    }

    {
        WITH_SEMAPHORE(data.sem);
        data.accept_msg_ids[i] = msgid;
    }

    lua_pushboolean(L, true);
    return 1;
}

int lua_mavlink_send_chan(lua_State *L) {

    // Allow : and . access
    const int arg_offset = (luaL_testudata(L, 1, "mavlink") != NULL) ? 1 : 0;

    binding_argcheck(L, 3+arg_offset);
    
    const mavlink_channel_t chan = (mavlink_channel_t)get_uint32(L, 1+arg_offset, 0, MAVLINK_COMM_NUM_BUFFERS - 1);

    const uint32_t msgid = get_uint32(L, 2+arg_offset, 0, (1 << 24) - 1);

    const char *packet = luaL_checkstring(L, 3+arg_offset);

    // FIXME: The data that's in this mavlink_msg_entry_t should be provided from the script, which allows
    //        sending entirely new messages as outputs. At the moment we can only encode messages that
    //        are known at compile time. This is fine as a starting point as this is symmetrical to the
    //        decoding side of the scripting support
    const mavlink_msg_entry_t *entry = mavlink_get_msg_entry(msgid);
    if (entry == nullptr) {
        return luaL_error(L, "Unknown MAVLink message ID (%d)", msgid);
    }

    WITH_SEMAPHORE(comm_chan_lock(chan));
    if (comm_get_txspace(chan) >= (GCS_MAVLINK::packet_overhead_chan(chan) + entry->max_msg_len)) {
        _mav_finalize_message_chan_send(chan,
                                        entry->msgid,
                                        packet,
                                        entry->min_msg_len,
                                        entry->max_msg_len,
                                        entry->crc_extra);

        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }

    return 1;
}

int lua_mavlink_block_command(lua_State *L) {

    // Allow : and . access
    const int arg_offset = (luaL_testudata(L, 1, "mavlink") != NULL) ? 1 : 0;

    binding_argcheck(L, 1+arg_offset);

    const uint16_t id = get_uint16_t(L, 1+arg_offset);

    // Check if ID is already registered
    if (AP::scripting()->is_handling_command(id)) {
        lua_pushboolean(L, true);
        return 1;
    }

    // Add new list item
    AP_Scripting::command_block_list *new_item = NEW_NOTHROW AP_Scripting::command_block_list;
    if (new_item == nullptr) {
        lua_pushboolean(L, false);
        return 1;
    }
    new_item->id = id;

    {
        WITH_SEMAPHORE(AP::scripting()->mavlink_command_block_list_sem);
        new_item->next = AP::scripting()->mavlink_command_block_list;
        AP::scripting()->mavlink_command_block_list = new_item;
    }

    lua_pushboolean(L, true);
    return 1;
}
#endif // HAL_GCS_ENABLED

#if AP_MISSION_ENABLED
int lua_mission_receive(lua_State *L) {
    binding_argcheck(L, 0);

    ObjectBuffer<struct AP_Scripting::scripting_mission_cmd> *input = AP::scripting()->mission_data;

    if (input == nullptr) {
        // no mission items ever received
        return 0;
    }

    struct AP_Scripting::scripting_mission_cmd cmd;

    if (!input->pop(cmd)) {
        // no new item
        return 0;
    }

    *new_uint32_t(L) = cmd.time_ms;

    lua_pushinteger(L, cmd.p1);
    lua_pushnumber(L, cmd.content_p1);
    lua_pushnumber(L, cmd.content_p2);
    lua_pushnumber(L, cmd.content_p3);

    return 5;
}
#endif // AP_MISSION_ENABLED

#if HAL_LOGGING_ENABLED
int AP_Logger_Write(lua_State *L) {
    AP_Logger * AP_logger = AP_Logger::get_singleton();
    if (AP_logger == nullptr) {
        return luaL_argerror(L, 1, "logger not supported on this firmware");
    }

    // Allow : and . access
    const int arg_offset = (luaL_testudata(L, 1, "logger") != NULL) ? 1 : 0;

    // check we have at least 4 arguments passed in
    const int args = lua_gettop(L) - arg_offset;
    if (args < 4) {
        return luaL_argerror(L, args, "too few arguments");
    }

    const char * name = luaL_checkstring(L, 1 + arg_offset);
    const char * labels = luaL_checkstring(L, 2 + arg_offset);
    const char * fmt = luaL_checkstring(L, 3 + arg_offset);

    // cheack the name, labels and format are not too long
    if (strlen(name) >= LS_NAME_SIZE) {
        return luaL_error(L, "Name must be 4 or less chars long");
    }
    uint8_t length = strlen(labels);
    if (length >= (LS_LABELS_SIZE - 7)) { // need 7 chars to add 'TimeUS,'
        return luaL_error(L, "labels must be less than 58 chars long");
    }
    // Count the number of commas
    uint8_t commas = 1;
    for (uint8_t i=0; i<length; i++) {
        if (labels[i] == ',') {
            commas++;
        }
    }

    length = strlen(fmt);
    if (length >= (LS_FORMAT_SIZE - 1)) { // need 1 char to add timestamp
        return luaL_error(L, "format must be less than 15 chars long");
    }

    // check the number of arguments matches the number of values in the label
    if (length != commas) {
        return luaL_argerror(L, args, "label does not match format");
    }

    bool have_units = false;
    if (args - 5 == length) {
        // check if there are enough arguments for units and multiplyers
        have_units = true;
    } else if (args - 3 != length) {
        // check the number of arguments matches the length of the foramt string
        return luaL_argerror(L, args, "format does not match No. of arguments");
    }

    // prepend timestamp to format and labels
    char label_cat[LS_LABELS_SIZE];
    strcpy(label_cat,"TimeUS,");
    strcat(label_cat,labels);
    char fmt_cat[LS_FORMAT_SIZE];
    strcpy(fmt_cat,"Q");
    strcat(fmt_cat,fmt);

    // Need to declare these here so they don't go out of scope
    char units_cat[LS_FORMAT_SIZE];
    char multipliers_cat[LS_FORMAT_SIZE];

    uint8_t field_start = 4;
    struct AP_Logger::log_write_fmt *f;
    if (!have_units) {
        // ask for a mesage type
        f = AP_logger->msg_fmt_for_name(name, label_cat, nullptr, nullptr, fmt_cat, true, true);

    } else {
        // read in units and multiplers strings
        field_start += 2;
        const char * units = luaL_checkstring(L, 4 + arg_offset);
        const char * multipliers = luaL_checkstring(L, 5 + arg_offset);

        if (length != strlen(units)) {
            return luaL_error(L, "units must be same length as format");
        }
        if (length != strlen(multipliers)) {
            return luaL_error(L, "multipliers must be same length as format");
        }

        // prepend timestamp to units and multiplyers
        strcpy(units_cat,"s");
        strcat(units_cat,units);

        strcpy(multipliers_cat,"F");
        strcat(multipliers_cat,multipliers);

        // ask for a mesage type
        f = AP_logger->msg_fmt_for_name(name, label_cat, units_cat, multipliers_cat, fmt_cat, true, true);
    }

    if (f == nullptr) {
        // unable to map name to a messagetype; could be out of
        // msgtypes, could be out of slots, ...
        return luaL_argerror(L, args, "could not map message type");
    }

    // work out how long the block will be
    int16_t msg_len = AP_logger->Write_calc_msg_len(fmt_cat);
    if (msg_len == -1) {
        return luaL_argerror(L, args, "unknown format");
    }

    // note that luaM_malloc will never return null, it will fault instead
    char *buffer = (char*)luaM_malloc(L, msg_len);

    // add logging headers
    uint8_t offset = 0;
    buffer[offset++] = HEAD_BYTE1;
    buffer[offset++] = HEAD_BYTE2;
    buffer[offset++] = f->msg_type;

    // timestamp is always first value
    const uint64_t now = AP_HAL::micros64();
    memcpy(&buffer[offset], &now, sizeof(uint64_t));
    offset += sizeof(uint64_t);

    for (uint8_t i=field_start; i<=args; i++) {
        uint8_t charlen = 0;
        uint8_t index = have_units ? i-5 : i-3;
        uint8_t arg_index = i + arg_offset;
        switch(fmt_cat[index]) {
            // logger variable types not available to scripting
            // 'd': double
            // 'q': int64_t
            // 'a': int16_t[32]
            case 'b': { // int8_t
                int isnum;
                const lua_Integer tmp1 = lua_tointegerx(L, arg_index, &isnum);
                if (!isnum || (tmp1 < INT8_MIN) || (tmp1 > INT8_MAX)) {
                    luaM_free(L, buffer);
                    luaL_argerror(L, arg_index, "argument out of range");
                    // no return
                }
                int8_t tmp = static_cast<int8_t>(tmp1);
                memcpy(&buffer[offset], &tmp, sizeof(int8_t));
                offset += sizeof(int8_t);
                break;
            }
            case 'h': // int16_t
            case 'c': { // int16_t * 100
                int isnum;
                const lua_Integer tmp1 = lua_tointegerx(L, arg_index, &isnum);
                if (!isnum || (tmp1 < INT16_MIN) || (tmp1 > INT16_MAX)) {
                    luaM_free(L, buffer);
                    luaL_argerror(L, arg_index, "argument out of range");
                    // no return
                }
                int16_t tmp = static_cast<int16_t>(tmp1);
                memcpy(&buffer[offset], &tmp, sizeof(int16_t));
                offset += sizeof(int16_t);
                break;
            }
            case 'H': // uint16_t
            case 'C': { // uint16_t * 100
                int isnum;
                const lua_Integer tmp1 = lua_tointegerx(L, arg_index, &isnum);
                if (!isnum || (tmp1 < 0) || (tmp1 > UINT16_MAX)) {
                    luaM_free(L, buffer);
                    luaL_argerror(L, arg_index, "argument out of range");
                    // no return
                }
                uint16_t tmp = static_cast<uint16_t>(tmp1);
                memcpy(&buffer[offset], &tmp, sizeof(uint16_t));
                offset += sizeof(uint16_t);
                break;
            }
            case 'i': // int32_t
            case 'L': // int32_t (lat/long)
            case 'e': { // int32_t * 100
                int isnum;
                const lua_Integer tmp1 = lua_tointegerx(L, arg_index, &isnum);
                if (!isnum) {
                    luaM_free(L, buffer);
                    luaL_argerror(L, arg_index, "argument out of range");
                    // no return
                }
                const int32_t tmp = tmp1;
                memcpy(&buffer[offset], &tmp, sizeof(int32_t));
                offset += sizeof(int32_t);
                break;
            }
            case 'f': { // float
                int isnum;
                const lua_Number tmp1 = lua_tonumberx(L, arg_index, &isnum);
                if (!isnum) {
                    luaM_free(L, buffer);
                    luaL_argerror(L, arg_index, "argument out of range");
                    // no return
                }
                const float tmp = tmp1;
                memcpy(&buffer[offset], &tmp, sizeof(float));
                offset += sizeof(float);
                break;
            }
            case 'n': { // char[4]
                charlen = 4;
                break;
            }
            case 'M': // uint8_t (flight mode)
            case 'B': { // uint8_t
                int isnum;
                lua_Integer tmp1 = lua_tointegerx(L, arg_index, &isnum);
                if (!isnum || (tmp1 < 0) || (tmp1 > UINT8_MAX)) {
                    // Also allow boolean
                    if (!isnum && lua_isboolean(L, arg_index)) {
                        tmp1 = lua_toboolean(L, arg_index);

                    } else {
                        luaM_free(L, buffer);
                        luaL_argerror(L, arg_index, "argument out of range");
                        // no return
                    }
                }
                uint8_t tmp = static_cast<uint8_t>(tmp1);
                memcpy(&buffer[offset], &tmp, sizeof(uint8_t));
                offset += sizeof(uint8_t);
                break;
            }
            case 'I': // uint32_t
            case 'E': { // uint32_t * 100
                uint32_t tmp;
                void * ud = luaL_testudata(L, arg_index, "uint32_t");
                if (ud != nullptr) {
                    tmp = *static_cast<uint32_t *>(ud);
                } else {
                    int success;
                    const lua_Integer v_int = lua_tointegerx(L, arg_index, &success);
                    if (success) {
                        tmp = v_int;
                    } else {
                        const lua_Number v_float = lua_tonumberx(L, arg_index, &success);
                        if (!success || (v_float < 0) || (v_float > float(UINT32_MAX))) {
                            luaM_free(L, buffer);
                            luaL_argerror(L, arg_index, "argument out of range");
                            // no return
                        }
                        tmp = v_float;
                    }
                }
                memcpy(&buffer[offset], &tmp, sizeof(uint32_t));
                offset += sizeof(uint32_t);
                break;
            }
            case 'Q': { // uint64_t
                void * ud = luaL_testudata(L, arg_index, "uint64_t");
                if (ud == nullptr) {
                    luaM_free(L, buffer);
                    luaL_argerror(L, arg_index, "argument out of range");
                    // no return
                }
                uint64_t tmp = *static_cast<uint64_t *>(ud);
                memcpy(&buffer[offset], &tmp, sizeof(uint64_t));
                offset += sizeof(uint64_t);
                break;
            }
            case 'N': { // char[16]
                charlen = 16;
                break;
            }
            case 'Z': { // char[64]
                charlen = 64;
                break;
            }
            default: {
                luaM_free(L, buffer);
                luaL_error(L, "%c unsupported format",fmt_cat[index]);
                // no return
            }
        }
        if (charlen != 0) {
            size_t slen;
            const char *tmp = lua_tolstring(L, arg_index, &slen);
            if (tmp == nullptr) {
                luaM_free(L, buffer);
                luaL_argerror(L, arg_index, "argument out of range");
                // no return
            }
            if (slen > charlen) {
                luaM_free(L, buffer);
                luaL_error(L, "arg %d too long for %c format",arg_index,fmt_cat[index]);
                // no return
            }
            memcpy(&buffer[offset], tmp, slen);
            memset(&buffer[offset+slen], 0, charlen-slen);
            offset += charlen;
        }
    }

    AP_logger->Safe_Write_Emit_FMT(f);

    AP_logger->WriteBlock(buffer,msg_len);

    luaM_free(L, buffer);

    return 0;
}
#endif // HAL_LOGGING_ENABLED

int lua_get_i2c_device(lua_State *L) {

    // Allow : and . access
    const int arg_offset = (luaL_testudata(L, 1, "i2c") != NULL) ? 1 : 0;

    const int args = lua_gettop(L) - arg_offset;
    if (args < 2) {
        return luaL_argerror(L, args, "require i2c bus and address");
    }
    if (args > 4) {
        return luaL_argerror(L, args, "too many arguments");
    }

    const lua_Integer bus_in = get_integer(L, 1 + arg_offset, 0, 4);
    const uint8_t bus = static_cast<uint8_t>(bus_in);

    const lua_Integer address_in = get_integer(L, 2 + arg_offset, 0, 128);
    const uint8_t address = static_cast<uint8_t>(address_in);

    // optional arguments, use the same defaults as the hal get_device function
    uint32_t bus_clock = 400000;
    bool use_smbus = false;

    if (args > 2) {
        bus_clock = coerce_to_uint32_t(L, 3 + arg_offset);

        if (args > 3) {
            use_smbus = static_cast<bool>(lua_toboolean(L, 4 + arg_offset));
        }
    }

    auto *scripting = AP::scripting();

    static_assert(SCRIPTING_MAX_NUM_I2C_DEVICE >= 0, "There cannot be a negative number of I2C devices");
    if (scripting->num_i2c_devices >= SCRIPTING_MAX_NUM_I2C_DEVICE) {
        return luaL_argerror(L, 1, "no i2c devices available");
    }

    scripting->_i2c_dev[scripting->num_i2c_devices] = NEW_NOTHROW AP_HAL::OwnPtr<AP_HAL::I2CDevice>;
    if (scripting->_i2c_dev[scripting->num_i2c_devices] == nullptr) {
        return luaL_argerror(L, 1, "i2c device nullptr");
    }

    *scripting->_i2c_dev[scripting->num_i2c_devices] = std::move(hal.i2c_mgr->get_device(bus, address, bus_clock, use_smbus));

    if (scripting->_i2c_dev[scripting->num_i2c_devices] == nullptr || scripting->_i2c_dev[scripting->num_i2c_devices]->get() == nullptr) {
        return luaL_argerror(L, 1, "i2c device nullptr");
    }

    *new_AP_HAL__I2CDevice(L) = scripting->_i2c_dev[scripting->num_i2c_devices]->get();

    scripting->num_i2c_devices++;

    return 1;
}

int AP_HAL__I2CDevice_read_registers(lua_State *L) {
    const int args = lua_gettop(L);
    bool multi_register;
    if (args == 2) {
        multi_register = false;
    } else if (args == 3) {
        multi_register = true;
    } else {
        return luaL_argerror(L, args, "expected 1 or 2 arguments");
    }

    AP_HAL::I2CDevice * ud = *check_AP_HAL__I2CDevice(L, 1);

    const uint8_t first_reg = get_uint8_t(L, 2);

    uint8_t recv_length = 1;
    if (multi_register) {
        recv_length = get_uint8_t(L, 3);
    }

    uint8_t data[recv_length];

    ud->get_semaphore()->take_blocking();
    const bool success = static_cast<bool>(ud->read_registers(first_reg, data, recv_length));
    ud->get_semaphore()->give();

    if (success) {
        if (!multi_register) {
            lua_pushinteger(L, data[0]);
        } else {
            // push to table
            lua_newtable(L);
            for (uint8_t i=0; i < recv_length; i++) {
                lua_pushinteger(L, i+1);
                lua_pushinteger(L, data[i]);
                lua_settable(L, -3);
            }
        }
    }
    return success;
}

int AP_HAL__I2CDevice_transfer(lua_State *L) {
    binding_argcheck(L, 3);

    AP_HAL::I2CDevice * ud = *check_AP_HAL__I2CDevice(L, 1);

    // Parse string of bytes to send
    size_t send_len;
    const uint8_t* send_data = (const uint8_t*)(lua_tolstring(L, 2, &send_len));

    // Parse and setup rx buffer
    uint32_t rx_len = get_uint8_t(L, 3);
    uint8_t rx_data[rx_len];

    // Transfer
    ud->get_semaphore()->take_blocking();
    const bool success = ud->transfer(send_data, send_len, rx_data, rx_len);
    ud->get_semaphore()->give();

    if (!success) {
        return 0;
    }

    // Return a string
    lua_pushlstring(L, (const char *)rx_data, rx_len);
    return 1;
}

#if AP_SCRIPTING_CAN_SENSOR_ENABLED
int lua_get_CAN_device(lua_State *L) {

    // Allow : and . access
    const int arg_offset = (luaL_testudata(L, 1, "CAN") != NULL) ? 1 : 0;

    binding_argcheck(L, 1 + arg_offset);

    const uint32_t raw_buffer_len = get_uint32(L, 1 + arg_offset, 1, 25);
    const uint32_t buffer_len = static_cast<uint32_t>(raw_buffer_len);

    auto *scripting = AP::scripting();

    if (scripting->_CAN_dev == nullptr) {
        scripting->_CAN_dev = NEW_NOTHROW ScriptingCANSensor(AP_CAN::Protocol::Scripting);
        if (scripting->_CAN_dev == nullptr) {
            return luaL_argerror(L, 1, "CAN device nullptr");
        }
    }

    if (!scripting->_CAN_dev->initialized()) {
        // Driver not initialized, probably because there is no can driver set to scripting
        // Return nil
        return 0;
    }

    *new_ScriptingCANBuffer(L) = scripting->_CAN_dev->add_buffer(buffer_len);

    return 1;
}

int lua_get_CAN_device2(lua_State *L) {

    // Allow : and . access
    const int arg_offset = (luaL_testudata(L, 1, "CAN") != NULL) ? 1 : 0;

    binding_argcheck(L, 1 + arg_offset);

    const uint32_t raw_buffer_len = get_uint32(L, 1 + arg_offset, 1, 25);
    const uint32_t buffer_len = static_cast<uint32_t>(raw_buffer_len);

    auto *scripting = AP::scripting();

    if (scripting->_CAN_dev2 == nullptr) {
        scripting->_CAN_dev2 = NEW_NOTHROW ScriptingCANSensor(AP_CAN::Protocol::Scripting2);
        if (scripting->_CAN_dev2 == nullptr) {
            return luaL_argerror(L, 1, "CAN device nullptr");
        }
    }

    if (!scripting->_CAN_dev2->initialized()) {
        // Driver not initialized, probably because there is no can driver set to scripting 2
        // Return nil
        return 0;
    }

    *new_ScriptingCANBuffer(L) = scripting->_CAN_dev2->add_buffer(buffer_len);

    return 1;
}
#endif // AP_SCRIPTING_CAN_SENSOR_ENABLED

#if AP_SERIALMANAGER_ENABLED
int lua_serial_find_serial(lua_State *L) {
    // Allow : and . access
    const int arg_offset = (luaL_testudata(L, 1, "serial") != NULL) ? 1 : 0;

    binding_argcheck(L, 1 + arg_offset);

    uint8_t instance = get_uint8_t(L, 1 + arg_offset);

    AP_SerialManager *mgr = &AP::serialmanager();
    AP_HAL::UARTDriver *driver_stream = mgr->find_serial(
        AP_SerialManager::SerialProtocol_Scripting, instance);

    if (driver_stream == nullptr) { // not found
        return 0;
    }

    AP_Scripting_SerialAccess *port = new_AP_Scripting_SerialAccess(L);
    port->stream = driver_stream;
#if AP_SCRIPTING_SERIALDEVICE_ENABLED
    port->is_device_port = false;
#endif

    return 1;
}
#endif // AP_SERIALMANAGER_ENABLED

#if AP_SCRIPTING_SERIALDEVICE_ENABLED
int lua_serial_find_simulated_device(lua_State *L) {
    // Allow : and . access
    const int arg_offset = (luaL_testudata(L, 1, "serial") != NULL) ? 1 : 0;

    binding_argcheck(L, 2 + arg_offset);

    const int8_t protocol = (int8_t)get_uint32(L, 1 + arg_offset, 0, 127);
    uint32_t instance = get_uint16_t(L, 2 + arg_offset);

    auto *scripting = AP::scripting();
    AP_Scripting_SerialDevice::Port *device_stream = nullptr;

    for (auto &port : scripting->_serialdevice.ports) {
        if (port.state.protocol == protocol) {
            if (instance-- == 0) {
                device_stream = &port;
                break;
            }
        }
    }

    if (!scripting->_serialdevice.enable || device_stream == nullptr) {
        // serial devices as a whole are disabled, or port not found
        return 0;
    }

    AP_Scripting_SerialAccess *port = new_AP_Scripting_SerialAccess(L);
    port->stream = device_stream;
    port->is_device_port = true;

    return 1;
}
#endif // AP_SCRIPTING_SERIALDEVICE_ENABLED

int lua_serial_writestring(lua_State *L)
{
    binding_argcheck(L, 2);

    AP_Scripting_SerialAccess * port = check_AP_Scripting_SerialAccess(L, 1);

    // get the bytes the user wants to write, along with their length
    size_t req_bytes;
    const char *data = luaL_checklstring(L, 2, &req_bytes);

    // write up to that number of bytes
    const uint32_t written_bytes = port->write((const uint8_t*)data, req_bytes);

    // return the number of bytes that were actually written
    lua_pushinteger(L, written_bytes);

    return 1;
}

int lua_serial_readstring(lua_State *L) {
    binding_argcheck(L, 2);

    AP_Scripting_SerialAccess * port = check_AP_Scripting_SerialAccess(L, 1);

    // create a buffer sized to hold the number of bytes the user wants to read
    luaL_Buffer b;
    const uint16_t req_bytes = get_uint16_t(L, 2);
    uint8_t *data = (uint8_t *)luaL_buffinitsize(L, &b, req_bytes);

    // read up to that number of bytes
    const ssize_t read_bytes = port->read(data, req_bytes);
    if (read_bytes < 0) {
        return 0; // error, return nil
    }

    // push the buffer as a string, truncated to the number of bytes actually read
    luaL_pushresultsize(&b, read_bytes);

    return 1;
}

/*
  directory listing, return table of files in a directory
 */
int lua_dirlist(lua_State *L) {
    binding_argcheck(L, 1);

    struct dirent *entry;
    int i;
    const char *path = luaL_checkstring(L, 1);
    
    /* open directory */
    auto dir = AP::FS().opendir(path);
    if (dir == nullptr) {  /* error opening the directory? */
        lua_pushnil(L);  /* return nil and ... */
        lua_pushstring(L, strerror(errno));  /* error message */
        return 2;  /* number of results */
    }
    
    /* create result table */
    lua_newtable(L);
    i = 1;
    while ((entry = AP::FS().readdir(dir)) != nullptr) {
        lua_pushnumber(L, i++);  /* push key */
        lua_pushstring(L, entry->d_name);  /* push value */
        lua_settable(L, -3);
    }
    
    AP::FS().closedir(dir);
    return 1;  /* table is already on top */
}

/*
  remove a file
 */
int lua_removefile(lua_State *L) {
    binding_argcheck(L, 1);
    const char *filename = luaL_checkstring(L, 1);
    return luaL_fileresult(L, AP::FS().unlink(filename) == 0, filename);
}

// Manual binding to allow SRV_Channels table to see safety state
int SRV_Channels_get_safety_state(lua_State *L) {
    binding_argcheck(L, 1);
    const bool data = hal.util->safety_switch_state() !=  AP_HAL::Util::SAFETY_ARMED;
    lua_pushboolean(L, data);
    return 1;
}

int lua_get_PWMSource(lua_State *L) {
    binding_argcheck(L, 0);

    auto *scripting = AP::scripting();

    static_assert(SCRIPTING_MAX_NUM_PWM_SOURCE >= 0, "There cannot be a negative number of PWMSources");
    if (scripting->num_pwm_source >= SCRIPTING_MAX_NUM_PWM_SOURCE) {
        return luaL_argerror(L, 1, "no PWMSources available");
    }

    scripting->_pwm_source[scripting->num_pwm_source] = NEW_NOTHROW AP_HAL::PWMSource;
    if (scripting->_pwm_source[scripting->num_pwm_source] == nullptr) {
        return luaL_argerror(L, 1, "PWMSources device nullptr");
    }

    *new_AP_HAL__PWMSource(L) = scripting->_pwm_source[scripting->num_pwm_source];

    scripting->num_pwm_source++;

    return 1;
}

#if AP_NETWORKING_ENABLED
/*
  allocate a SocketAPM
 */
int lua_get_SocketAPM(lua_State *L) {
    binding_argcheck(L, 1);
    const uint8_t datagram = get_uint8_t(L, 1);
    auto *scripting = AP::scripting();

    auto *sock = NEW_NOTHROW SocketAPM(datagram);
    if (sock == nullptr) {
        return luaL_argerror(L, 1, "SocketAPM device nullptr");
    }
    for (uint8_t i=0; i<SCRIPTING_MAX_NUM_NET_SOCKET; i++) {
        if (scripting->_net_sockets[i] == nullptr) {
            scripting->_net_sockets[i] = sock;
            *new_SocketAPM(L) = scripting->_net_sockets[i];
            return 1;
        }
    }

    return luaL_argerror(L, 1, "no sockets available");
}

/*
  socket close
 */
int SocketAPM_close(lua_State *L) {
    binding_argcheck(L, 1);

    SocketAPM *ud = *check_SocketAPM(L, 1);

    auto *scripting = AP::scripting();

    // clear allocated socket
    for (uint8_t i=0; i<SCRIPTING_MAX_NUM_NET_SOCKET; i++) {
        if (scripting->_net_sockets[i] == ud) {
            ud->close();
            delete ud;
            scripting->_net_sockets[i] = nullptr;
            *check_SocketAPM(L, 1) = nullptr;
            break;
        }
    }

    return 0;
}

/*
  socket sendfile, for offloading file send to AP_Networking
 */
int SocketAPM_sendfile(lua_State *L) {
    binding_argcheck(L, 2);

    SocketAPM *ud = *check_SocketAPM(L, 1);

    auto *p = (luaL_Stream *)luaL_checkudata(L, 2, LUA_FILEHANDLE);
    int fd = p->f->fd;

    bool ret = fd != -1 && AP::network().sendfile(ud, fd);
    if (ret) {
        // the fd is no longer valid. The lua script must
        // still call close() to release the memory from the
        // socket
        p->f->fd = -1;
    }

    lua_pushboolean(L, ret);
    return 1;
}

/*
  receive from a socket to a lua string
 */
int SocketAPM_recv(lua_State *L) {
    binding_argcheck(L, 2);

    SocketAPM * ud = *check_SocketAPM(L, 1);

    const uint16_t count = get_uint16_t(L, 2);
    uint8_t *data = (uint8_t*)malloc(count);
    if (data == nullptr) {
        return 0;
    }

    const auto ret = ud->recv(data, count, 0);
    if (ret < 0) {
        free(data);
        return 0;
    }

    int retcount = 1;

    // push data to lua string
    lua_pushlstring(L, (const char *)data, ret);

    // also push the address and port if available
    uint32_t ip_addr;
    uint16_t port;
    if (ud->last_recv_address(ip_addr, port)) {
        *new_uint32_t(L) = ip_addr;
        lua_pushinteger(L, port);
        retcount += 2;
    }

    free(data);

    return retcount;
}

/*
  TCP socket accept() call
 */
int SocketAPM_accept(lua_State *L) {
    binding_argcheck(L, 1);

    SocketAPM * ud = *check_SocketAPM(L, 1);

    auto *scripting = AP::scripting();

    // find an empty slot
    for (uint8_t i=0; i<SCRIPTING_MAX_NUM_NET_SOCKET; i++) {
        if (scripting->_net_sockets[i] == nullptr) {
            scripting->_net_sockets[i] = ud->accept(0);
            if (scripting->_net_sockets[i] == nullptr) {
                return 0;
            }
            *new_SocketAPM(L) = scripting->_net_sockets[i];
            return 1;
        }
    }

    // out of socket slots, return nil, caller can retry
    return 0;
}

/*
  convert a uint32_t ipv4 address to a string
 */
int SocketAPM_ipv4_addr_to_string(lua_State *L) {
    binding_argcheck(L, 1);
    const uint32_t ip_addr = get_uint32(L, 1, 0, UINT32_MAX);
    char buf[IP4_STR_LEN];
    const char *ret = SocketAPM::inet_addr_to_str(ip_addr, buf, sizeof(buf));
    if (ret == nullptr) {
        return 0;
    }
    lua_pushlstring(L, (const char *)ret, strlen(ret));
    return 1;
}

/*
  convert a ipv4 string address to a uint32_t
 */
int SocketAPM_string_to_ipv4_addr(lua_State *L) {
    binding_argcheck(L, 1);
    const char *str = luaL_checkstring(L, 1);
    *new_uint32_t(L) = SocketAPM::inet_str_to_addr(str);
    return 1;
}

#endif // AP_NETWORKING_ENABLED


int lua_get_current_env_ref()
{
    auto *scripting = AP::scripting();
    return scripting->get_current_env_ref();
}

// This is used when loading modules with require, lua must only look in enabled directory's
const char* lua_get_modules_path()
{
#define LUA_PATH_ROMFS "@ROMFS/scripts/modules/?.lua;" "@ROMFS/scripts/modules/?/init.lua"
#define LUA_PATH_SCRIPTS LUA_LDIR"?.lua;"  LUA_LDIR"?/init.lua"

    uint16_t dir_disable = AP_Scripting::get_singleton()->get_disabled_dir();
    dir_disable &= uint16_t(AP_Scripting::SCR_DIR::SCRIPTS) | uint16_t(AP_Scripting::SCR_DIR::ROMFS);
    if (dir_disable == 0) {
        // Both directory's are enabled, return both, ROMFS takes priority
        return LUA_PATH_ROMFS ";" LUA_PATH_SCRIPTS;
    }

    if ((dir_disable & uint16_t(AP_Scripting::SCR_DIR::SCRIPTS)) == 0) {
        // Only scripts enabled
        return LUA_PATH_SCRIPTS;
    }

    if ((dir_disable & uint16_t(AP_Scripting::SCR_DIR::ROMFS)) == 0) {
        // Only ROMFS enabled
        return LUA_PATH_ROMFS;
    }

    // Nothing enabled?
    return "";
}

// Simple print to GCS or over CAN
int lua_print(lua_State *L) {
    // Only support a single argument
    binding_argcheck(L, 1);

    GCS_SEND_TEXT(MAV_SEVERITY_DEBUG, "%s", luaL_checkstring(L, 1));

    return 0;
}

#if AP_RANGEFINDER_ENABLED
int lua_range_finder_handle_script_msg(lua_State *L) {
    // Arg 1 => self (an instance of rangefinder_backend)
    // Arg 2 => a float distance or a RangeFinder_State user data
    binding_argcheck(L, 2);

    // check_AP_RangeFinder_Backend aborts if not found. No need to check for null
    AP_RangeFinder_Backend * ud = *check_AP_RangeFinder_Backend(L, 1);

    bool result = false;

    // Check to see if the first argument is the state structure.
    const void *state_arg = luaL_testudata(L, 2, "RangeFinder_State");
    if (state_arg != nullptr) {
        result = ud->handle_script_msg(*static_cast<const RangeFinder::RangeFinder_State *>(state_arg));

    } else {
        // Otherwise assume the argument is a number and set the measurement.
        result = ud->handle_script_msg(luaL_checknumber(L, 2));
    }

    lua_pushboolean(L, result);
    return 1;
}
#endif  // AP_RANGEFINDER_ENABLED

/*
  lua wants to abort, and doesn't have access to a panic function
 */
void lua_abort()
{
    INTERNAL_ERROR(AP_InternalError::error_t::flow_of_control);
#if AP_SIM_ENABLED
    AP_HAL::panic("lua_abort called");
#else
    if (!hal.util->get_soft_armed()) {
        AP_HAL::panic("lua_abort called");
    }
    // abort while flying, all we can do is loop
    while (true) {
        hal.scheduler->delay(1000);
    }
#endif
}

#if (HAL_GCS_ENABLED && !defined(HAL_BUILD_AP_PERIPH))
/*
  implement gcs:command_int() access to MAV_CMD_xxx commands
 */
int lua_GCS_command_int(lua_State *L)
{
    GCS *_gcs = check_GCS(L);
    binding_argcheck(L, 3);

    const uint16_t command = get_uint16_t(L, 2);
    if (!lua_istable(L, 3)) {
        // must have parameter table
        return 0;
    }

    mavlink_command_int_t pkt {};

    pkt.command = command;

    float *params = &pkt.param1;
    int32_t *xy = &pkt.x;

    // extract the first 4 parameters as floats
    for (uint8_t i=0; i<4; i++) {
        char pname[3] { 'p' , char('1' + i), 0 };
        lua_pushstring(L, pname);
        lua_gettable(L, 3);
        if (lua_isnumber(L, -1)) {
            params[i] = lua_tonumber(L, -1);
        }
        lua_pop(L, 1);
    }

    // extract the xy values
    for (uint8_t i=0; i<2; i++) {
        const char *names[] = { "x", "y" };
        lua_pushstring(L, names[i]);
        lua_gettable(L, 3);
        if (lua_isinteger(L, -1)) {
            xy[i] = lua_tointeger(L, -1);
        }
        lua_pop(L, 1);
    }

    // and z
    lua_pushstring(L, "z");
    lua_gettable(L, 3);
    if (lua_isnumber(L, -1)) {
        pkt.z = lua_tonumber(L, -1);
    }
    lua_pop(L, 1);

    // optional frame
    lua_pushstring(L, "frame");
    lua_gettable(L, 3);
    if (lua_isinteger(L, -1)) {
        pkt.frame = lua_tointeger(L, -1);
    }
    lua_pop(L, 1);
    
    // call the interface with scheduler lock
    WITH_SEMAPHORE(AP::scheduler().get_semaphore());

    auto result = _gcs->lua_command_int_packet(pkt);

    // Return the resulting MAV_RESULT
    lua_pushinteger(L, result);

    return 1;
}
#endif

#if HAL_ENABLE_DRONECAN_DRIVERS
/*
  get FlexDebug from a DroneCAN node
 */
int lua_DroneCAN_get_FlexDebug(lua_State *L)
{
    binding_argcheck(L, 4);

    const uint8_t bus = get_uint8_t(L, 1);
    const uint8_t node_id = get_uint8_t(L, 2);
    const uint16_t msg_id = get_uint16_t(L, 3);
    uint32_t tstamp_us = get_uint32(L, 4, 0, UINT32_MAX);

    const auto *dc = AP_DroneCAN::get_dronecan(bus);
    if (dc == nullptr) {
        return 0;
    }
    dronecan_protocol_FlexDebug msg;

    if (!dc->get_FlexDebug(node_id, msg_id, tstamp_us, msg)) {
        return 0;
    }

    *new_uint32_t(L) = tstamp_us;
    lua_pushlstring(L, (const char *)msg.u8.data, msg.u8.len);

    return 2;
}
#endif // HAL_ENABLE_DRONECAN_DRIVERS

#endif  // AP_SCRIPTING_ENABLED
