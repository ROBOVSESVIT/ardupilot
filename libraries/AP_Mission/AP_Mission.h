/// @file    AP_Mission.h
/// @brief   Handles the MAVLINK command mission stack.  Reads and writes mission to storage.

/*
 *   The AP_Mission library:
 *   - responsible for managing a list of commands made up of "nav", "do" and "conditional" commands
 *   - reads and writes the mission commands to storage.
 *   - provides easy access to current, previous and upcoming waypoints
 *   - calls main program's command execution and verify functions.
 *   - accounts for the DO_JUMP command
 *
 */
#pragma once

#include "AP_Mission_config.h"

#include <GCS_MAVLink/GCS_MAVLink.h>
#include <AP_Math/AP_Math.h>
#include <AP_Common/AP_Common.h>
#include <AP_Common/Location.h>
#include <AP_Param/AP_Param.h>
#include <StorageManager/StorageManager.h>
#include <AP_Common/float16.h>

// definitions
#define AP_MISSION_EEPROM_VERSION           0x65AE  // version number stored in first four bytes of eeprom.  increment this by one when eeprom format is changed
#define AP_MISSION_EEPROM_COMMAND_SIZE      15      // size in bytes of all mission commands

#ifndef AP_MISSION_MAX_NUM_DO_JUMP_COMMANDS
#if HAL_MEM_CLASS >= HAL_MEM_CLASS_500
#define AP_MISSION_MAX_NUM_DO_JUMP_COMMANDS 100     // allow up to 100 do-jump commands
#else
#define AP_MISSION_MAX_NUM_DO_JUMP_COMMANDS 15      // allow up to 15 do-jump commands
#endif
#endif

#define AP_MISSION_JUMP_REPEAT_FOREVER      -1      // when do-jump command's repeat count is -1 this means endless repeat

#define AP_MISSION_CMD_ID_NONE              0       // mavlink cmd id of zero means invalid or missing command
#define AP_MISSION_CMD_INDEX_NONE           65535   // command index of 65535 means invalid or missing command
#define AP_MISSION_JUMP_TIMES_MAX           32767   // maximum number of times a jump can be executed.  Used when jump tracking fails (i.e. when too many jumps in mission)

#define AP_MISSION_FIRST_REAL_COMMAND       1       // command #0 reserved to hold home position

#define AP_MISSION_RESTART_DEFAULT          0       // resume the mission from the last command run by default

#define AP_MISSION_OPTIONS_DEFAULT          0       // Do not clear the mission when rebooting

#define AP_MISSION_MAX_WP_HISTORY           7       // The maximum number of previous wp commands that will be stored from the active missions history
#define LAST_WP_PASSED (AP_MISSION_MAX_WP_HISTORY-2)

#if CONFIG_HAL_BOARD == HAL_BOARD_CHIBIOS
#define AP_MISSION_SDCARD_FILENAME "APM/mission.stg"
#else
#define AP_MISSION_SDCARD_FILENAME "mission.stg"
#endif

union PackedContent;

/// @class    AP_Mission
/// @brief    Object managing Mission
class AP_Mission
{

public:
    // jump command structure
    struct PACKED Jump_Command {
        uint16_t target;        // target command id
        int16_t num_times;      // num times to repeat.  -1 = repeat forever
    };

    // condition delay command structure
    struct PACKED Conditional_Delay_Command {
        float seconds;          // period of delay in seconds
    };

    // condition delay command structure
    struct PACKED Conditional_Distance_Command {
        float meters;           // distance from next waypoint in meters
    };

    // condition yaw command structure
    struct PACKED Yaw_Command {
        float angle_deg;        // target angle in degrees (0=north, 90=east)
        float turn_rate_dps;    // turn rate in degrees / second (0=use default)
        int8_t direction;       // -1 = ccw, +1 = cw
        uint8_t relative_angle; // 0 = absolute angle, 1 = relative angle
    };

    // change speed command structure
    struct PACKED Change_Speed_Command {
        uint8_t speed_type;     // 0=airspeed, 1=ground speed
        float target_ms;        // target speed in m/s, -1 means no change
        float throttle_pct;     // throttle as a percentage (i.e. 1 ~ 100), 0 means no change
    };

    // set relay command structure
    struct PACKED Set_Relay_Command {
        uint8_t num;            // relay number from 1 to 4
        uint8_t state;          // on = 3.3V or 5V (depending upon board), off = 0V.  only used for do-set-relay, not for do-repeat-relay
    };

    // repeat relay command structure
    struct PACKED Repeat_Relay_Command {
        uint8_t num;            // relay number from 1 to 4
        int16_t repeat_count;   // number of times to trigger the relay
        float cycle_time;       // cycle time in seconds (the time between peaks or the time the relay is on and off for each cycle?)
    };

    // set servo command structure
    struct PACKED Set_Servo_Command {
        uint8_t channel;        // servo channel
        uint16_t pwm;           // pwm value for servo
    };

    // repeat servo command structure
    struct PACKED Repeat_Servo_Command {
        uint8_t channel;        // servo channel
        uint16_t pwm;           // pwm value for servo
        int16_t repeat_count;   // number of times to move the servo (returns to trim in between)
        float cycle_time;       // cycle time in seconds (the time between peaks or the time the servo is at the specified pwm value for each cycle?)
    };

    // mount control command structure
    struct PACKED Mount_Control {
        float pitch;            // pitch angle in degrees
        float roll;             // roll angle in degrees
        float yaw;              // yaw angle (relative to vehicle heading) in degrees
    };

    // digicam control command structure
    struct PACKED Digicam_Configure {
        uint8_t shooting_mode;  // ProgramAuto = 1, AV = 2, TV = 3, Man=4, IntelligentAuto=5, SuperiorAuto=6
        uint16_t shutter_speed;
        uint8_t aperture;       // F stop number * 10
        uint16_t ISO;           // 80, 100, 200, etc
        uint8_t exposure_type;
        uint8_t cmd_id;
        float engine_cutoff_time;   // seconds
    };

    // digicam control command structure
    struct PACKED Digicam_Control {
        uint8_t session;        // 1 = on, 0 = off
        uint8_t zoom_pos;
        int8_t zoom_step;       // +1 = zoom in, -1 = zoom out
        uint8_t focus_lock;
        uint8_t shooting_cmd;
        uint8_t cmd_id;
    };

    // set cam trigger distance command structure
    struct PACKED Cam_Trigg_Distance {
        float meters;           // distance
        uint8_t trigger;        // triggers one image capture immediately
    };

    // gripper command structure
    struct PACKED Gripper_Command {
        uint8_t num;            // gripper number
        uint8_t action;         // action (0 = release, 1 = grab)
    };

    // AUX_FUNCTION command structure
    struct PACKED AuxFunction {
        uint16_t function;  // from RC_Channel::AUX_FUNC
        uint8_t switchpos;  // from RC_Channel::AuxSwitchPos
    };

    // high altitude balloon altitude wait
    struct PACKED Altitude_Wait {
        float altitude; // meters
        float descent_rate; // m/s
        uint8_t wiggle_time; // seconds
    };

    // nav guided command
    struct PACKED Guided_Limits_Command {
        // max time is held in p1 field
        float alt_min;          // min alt below which the command will be aborted.  0 for no lower alt limit
        float alt_max;          // max alt above which the command will be aborted.  0 for no upper alt limit
        float horiz_max;        // max horizontal distance the vehicle can move before the command will be aborted.  0 for no horizontal limit
    };

    // do VTOL transition
    struct PACKED Do_VTOL_Transition {
        uint8_t target_state;
    };

    // navigation delay command structure
    struct PACKED Navigation_Delay_Command {
        float seconds; // period of delay in seconds
        int8_t hour_utc; // absolute time's hour (utc)
        int8_t min_utc; // absolute time's min (utc)
        int8_t sec_utc; // absolute time's sec (utc)
    };

    // DO_ENGINE_CONTROL support
    struct PACKED Do_Engine_Control {
        bool start_control; // start or stop engine
        bool cold_start; // use cold start procedure
        uint16_t height_delay_cm; // height delay for start
        bool allow_disarmed_start; // allow starting the engine while disarmed
    };

    // NAV_SET_YAW_SPEED support
    struct PACKED Set_Yaw_Speed {
        float angle_deg;        // target angle in degrees (0=north, 90=east)
        float speed;            // speed in meters/second
        uint8_t relative_angle; // 0 = absolute angle, 1 = relative angle
    };

    // winch command structure
    struct PACKED Winch_Command {
        uint8_t num;            // winch number
        uint8_t action;         // action (0 = relax, 1 = length control, 2 = rate control)
        float release_length;   // cable distance to unwind in meters, negative numbers to wind in cable
        float release_rate;     // release rate in meters/second
    };

    // Scripting command structure
    struct PACKED scripting_Command {
        float p1;
        float p2;
        float p3;
    };

#if AP_SCRIPTING_ENABLED
    // Scripting NAV command old version of storage format
    struct PACKED nav_script_time_Command_tag0 {
        uint8_t command;
        uint8_t timeout_s;
        float arg1;
        float arg2;
    };

    // Scripting NAV command, new version of storage format
    struct PACKED nav_script_time_Command {
        uint8_t command;
        uint8_t timeout_s;
        Float16_t arg1;
        Float16_t arg2;
        // last 2 arguments need to be integers due to MISSION_ITEM_INT encoding
        int16_t arg3;
        int16_t arg4;
    };
#endif

    // Scripting NAV command (with verify)
    struct PACKED nav_attitude_time_Command {
        uint16_t time_sec;
        int16_t roll_deg;
        int8_t pitch_deg;
        int16_t yaw_deg;
        int16_t climb_rate;
    };

    // MAV_CMD_DO_GIMBAL_MANAGER_PITCHYAW support
    struct PACKED gimbal_manager_pitchyaw_Command {
        int8_t pitch_angle_deg;
        int16_t yaw_angle_deg;
        int8_t pitch_rate_degs;
        int8_t yaw_rate_degs;
        uint8_t flags;
        uint8_t gimbal_id;
    };

    // MAV_CMD_IMAGE_START_CAPTURE support
    struct PACKED image_start_capture_Command {
        uint8_t instance;
        float interval_s;
        uint16_t total_num_images;
        uint16_t start_seq_number;
    };

    // MAV_CMD_SET_CAMERA_ZOOM support
    struct PACKED set_camera_zoom_Command {
        uint8_t zoom_type;
        float zoom_value;
    };

    // MAV_CMD_SET_CAMERA_FOCUS support
    struct PACKED set_camera_focus_Command {
        uint8_t focus_type;
        float focus_value;
    };

    // MAV_CMD_SET_CAMERA_SOURCE support
    struct PACKED set_camera_source_Command {
        uint8_t instance;
        uint8_t primary_source;
        uint8_t secondary_source;
    };

    // MAV_CMD_VIDEO_START_CAPTURE support
    struct PACKED video_start_capture_Command {
        uint8_t video_stream_id;
    };

    // MAV_CMD_VIDEO_STOP_CAPTURE support
    struct PACKED video_stop_capture_Command {
        uint8_t video_stream_id;
    };

    union Content {
        // jump structure
        Jump_Command jump;

        // conditional delay
        Conditional_Delay_Command delay;

        // conditional distance
        Conditional_Distance_Command distance;

        // conditional yaw
        Yaw_Command yaw;

        // change speed
        Change_Speed_Command speed;

        // do-set-relay
        Set_Relay_Command relay;

        // do-repeat-relay
        Repeat_Relay_Command repeat_relay;

        // do-set-servo
        Set_Servo_Command servo;

        // do-repeate-servo
        Repeat_Servo_Command repeat_servo;

        // mount control
        Mount_Control mount_control;

        // camera configure
        Digicam_Configure digicam_configure;

        // camera control
        Digicam_Control digicam_control;

        // cam trigg distance
        Cam_Trigg_Distance cam_trigg_dist;

        // do-gripper
        Gripper_Command gripper;

        // arbitrary aux function
        AuxFunction auxfunction;

        // do-guided-limits
        Guided_Limits_Command guided_limits;

        // high altitude balloon altitude wait
        Altitude_Wait altitude_wait;

        // do vtol transition
        Do_VTOL_Transition do_vtol_transition;

        // DO_ENGINE_CONTROL
        Do_Engine_Control do_engine_control;

        // navigation delay
        Navigation_Delay_Command nav_delay;

        // NAV_SET_YAW_SPEED support
        Set_Yaw_Speed set_yaw_speed;

        // do-winch
        Winch_Command winch;

        // do scripting
        scripting_Command scripting;

#if AP_SCRIPTING_ENABLED
        // nav scripting
        nav_script_time_Command nav_script_time;
#endif

        // nav attitude time
        nav_attitude_time_Command nav_attitude_time;

        // MAV_CMD_DO_GIMBAL_MANAGER_PITCHYAW
        gimbal_manager_pitchyaw_Command gimbal_manager_pitchyaw;

        // MAV_CMD_IMAGE_START_CAPTURE support
        image_start_capture_Command image_start_capture;

        // MAV_CMD_SET_CAMERA_ZOOM support
        set_camera_zoom_Command set_camera_zoom;

        // MAV_CMD_SET_CAMERA_FOCUS support
        set_camera_focus_Command set_camera_focus;

        // MAV_CMD_SET_CAMEARA_SOURCE support
        set_camera_source_Command set_camera_source;

        // MAV_CMD_VIDEO_START_CAPTURE support
        video_start_capture_Command video_start_capture;

        // MAV_CMD_VIDEO_STOP_CAPTURE support
        video_stop_capture_Command video_stop_capture;

        // location
        Location location{};      // Waypoint location
    };

    // command structure
    struct Mission_Command {
        uint16_t index;             // this commands position in the command list
        uint16_t id;                // mavlink command id
        uint16_t p1;                // general purpose parameter 1
        Content content;

        // for items which store in location, we offer a few more bits
        // of storage:
        uint8_t type_specific_bits;  // bitmask of set/unset bits

        // return a human-readable interpretation of the ID stored in this command
        const char *type() const;

        // comparison operator (relies on all bytes in the structure even if they may not be used)
        bool operator ==(const Mission_Command &b) const { return (memcmp(this, &b, sizeof(Mission_Command)) == 0); }
        bool operator !=(const Mission_Command &b) const { return !operator==(b); }

        /*
          return the number of turns for a LOITER_TURNS command
          this has special handling for loiter turns from cmd.p1 and type_specific_bits
         */
        float get_loiter_turns(void) const {
            float turns = LOWBYTE(p1);
            if (type_specific_bits & (1U<<1)) {
                // special storage handling allows for fractional turns
                turns *= (1.0/256.0);
            }
            return turns;
        }
    };


    // main program function pointers
    FUNCTOR_TYPEDEF(mission_cmd_fn_t, bool, const Mission_Command&);
    FUNCTOR_TYPEDEF(mission_complete_fn_t, void);

    // constructor
    AP_Mission(mission_cmd_fn_t cmd_start_fn, mission_cmd_fn_t cmd_verify_fn, mission_complete_fn_t mission_complete_fn) :
        _cmd_start_fn(cmd_start_fn),
        _cmd_verify_fn(cmd_verify_fn),
        _mission_complete_fn(mission_complete_fn),
        _prev_nav_cmd_id(AP_MISSION_CMD_ID_NONE),
        _prev_nav_cmd_index(AP_MISSION_CMD_INDEX_NONE),
        _prev_nav_cmd_wp_index(AP_MISSION_CMD_INDEX_NONE)
    {
#if CONFIG_HAL_BOARD == HAL_BOARD_SITL
        if (_singleton != nullptr) {
            AP_HAL::panic("Mission must be singleton");
        }
#endif
        _singleton = this;

        // load parameter defaults
        AP_Param::setup_object_defaults(this, var_info);

        // clear commands
        _nav_cmd.index = AP_MISSION_CMD_INDEX_NONE;
        _do_cmd.index = AP_MISSION_CMD_INDEX_NONE;
    }

    // get singleton instance
    static AP_Mission *get_singleton()
    {
        return _singleton;
    }

    /* Do not allow copies */
    CLASS_NO_COPY(AP_Mission);

    // mission state enumeration
    enum mission_state {
        MISSION_STOPPED=0,
        MISSION_RUNNING=1,
        MISSION_COMPLETE=2
    };

    ///
    /// public mission methods
    ///

    /// init - initialises this library including checks the version in eeprom matches this library
    void init();

    /// status - returns the status of the mission (i.e. Mission_Started, Mission_Complete, Mission_Stopped
    mission_state state() const
    {
        return _flags.state;
    }

    /// num_commands - returns total number of commands in the mission
    ///                 this number includes offset 0, the home location
    uint16_t num_commands() const
    {
        return _cmd_total;
    }

    /// num_commands_max - returns maximum number of commands that can be stored
    uint16_t num_commands_max() const {
        return _commands_max;
    }

    /// start - resets current commands to point to the beginning of the mission
    ///     To-Do: should we validate the mission first and return true/false?
    void start();

    /// stop - stops mission execution.  subsequent calls to update() will have no effect until the mission is started or resumed
    void stop();

    /// resume - continues the mission execution from where we last left off
    ///     previous running commands will be re-initialised
    void resume();

    /// start_or_resume - if MIS_AUTORESTART=0 this will call resume(), otherwise it will call start()
    void start_or_resume();

    /// check mission starts with a takeoff command
    bool starts_with_takeoff_cmd();

    /// reset - reset mission to the first command
    void reset();

    /// clear - clears out mission
    bool clear();

    /// truncate - truncate any mission items beyond given index
    void truncate(uint16_t index);

    /// update - ensures the command queues are loaded with the next command and calls main programs command_init and command_verify functions to progress the mission
    ///     should be called at 10hz or higher
    void update();

    ///
    /// public command methods
    ///

    /// add_cmd - adds a command to the end of the command list and writes to storage
    ///     returns true if successfully added, false on failure
    ///     cmd.index is updated with it's new position in the mission
    bool add_cmd(Mission_Command& cmd);

    /// replace_cmd - replaces the command at position 'index' in the command list with the provided cmd
    ///     replacing the current active command will have no effect until the command is restarted
    ///     returns true if successfully replaced, false on failure
    bool replace_cmd(uint16_t index, const Mission_Command& cmd);

    /// is_nav_cmd - returns true if the command's id is a "navigation" command, false if "do" or "conditional" command
    static bool is_nav_cmd(const Mission_Command& cmd);

    /// get_current_nav_cmd - returns the current "navigation" command
    const Mission_Command& get_current_nav_cmd() const
    {
        return _nav_cmd;
    }

    /// get_current_nav_index - returns the current "navigation" command index
    /// Note that this will return 0 if there is no command. This is
    /// used in MAVLink reporting of the mission command
    uint16_t get_current_nav_index() const
    {
        return _nav_cmd.index==AP_MISSION_CMD_INDEX_NONE?0:_nav_cmd.index;
    }

    /// get_current_nav_id - return the id of the current nav command
    uint16_t get_current_nav_id() const
    {
        return _nav_cmd.id;
    }

    /// get_prev_nav_cmd_id - returns the previous "navigation" command id
    ///     if there was no previous nav command it returns AP_MISSION_CMD_ID_NONE
    ///     we do not return the entire command to save on RAM
    uint16_t get_prev_nav_cmd_id() const
    {
        return _prev_nav_cmd_id;
    }

    /// get_prev_nav_cmd_index - returns the previous "navigation" commands index (i.e. position in the mission command list)
    ///     if there was no previous nav command it returns AP_MISSION_CMD_INDEX_NONE
    ///     we do not return the entire command to save on RAM
    uint16_t get_prev_nav_cmd_index() const
    {
        return _prev_nav_cmd_index;
    }

    /// get_prev_nav_cmd_with_wp_index - returns the previous "navigation" commands index that contains a waypoint (i.e. position in the mission command list)
    ///     if there was no previous nav command it returns AP_MISSION_CMD_INDEX_NONE
    ///     we do not return the entire command to save on RAM
    uint16_t get_prev_nav_cmd_with_wp_index() const
    {
        return _prev_nav_cmd_wp_index;
    }

    /// get_next_nav_cmd - gets next "navigation" command found at or after start_index
    ///     returns true if found, false if not found (i.e. reached end of mission command list)
    ///     accounts for do_jump commands
    bool get_next_nav_cmd(uint16_t start_index, Mission_Command& cmd);

    /// get the ground course of the next navigation leg in centidegrees
    /// from 0 36000. Return default_angle if next navigation
    /// leg cannot be determined
    int32_t get_next_ground_course_cd(int32_t default_angle);

    /// get_current_do_cmd - returns active "do" command
    const Mission_Command& get_current_do_cmd() const
    {
        return _do_cmd;
    }

    /// get_current_do_cmd_id - returns id of the active "do" command
    uint16_t get_current_do_cmd_id() const
    {
        return _do_cmd.id;
    }

    // set_current_cmd - jumps to command specified by index
    bool set_current_cmd(uint16_t index);

    // restart current navigation command.  Used to handle external changes to mission
    // returns true on success, false if current nav command has been deleted
    bool restart_current_nav_cmd();

    /// load_cmd_from_storage - load command from storage
    ///     true is return if successful
    bool read_cmd_from_storage(uint16_t index, Mission_Command& cmd) const;

    /// write_cmd_to_storage - write a command to storage
    ///     cmd.index is used to calculate the storage location
    ///     true is returned if successful
    bool write_cmd_to_storage(uint16_t index, const Mission_Command& cmd);

    /// write_home_to_storage - writes the special purpose cmd 0 (home) to storage
    ///     home is taken directly from ahrs
    void write_home_to_storage();

    static MAV_MISSION_RESULT convert_MISSION_ITEM_to_MISSION_ITEM_INT(const mavlink_mission_item_t &mission_item,
            mavlink_mission_item_int_t &mission_item_int) WARN_IF_UNUSED;
    static MAV_MISSION_RESULT convert_MISSION_ITEM_INT_to_MISSION_ITEM(const mavlink_mission_item_int_t &mission_item_int,
            mavlink_mission_item_t &mission_item) WARN_IF_UNUSED;

    // mavlink_int_to_mission_cmd - converts mavlink message to an AP_Mission::Mission_Command object which can be stored to eeprom
    //  return MAV_MISSION_ACCEPTED on success, MAV_MISSION_RESULT error on failure
    static MAV_MISSION_RESULT mavlink_int_to_mission_cmd(const mavlink_mission_item_int_t& packet, AP_Mission::Mission_Command& cmd);

    // mission_cmd_to_mavlink_int - converts an AP_Mission::Mission_Command object to a mavlink message which can be sent to the GCS
    //  return true on success, false on failure
    static bool mission_cmd_to_mavlink_int(const AP_Mission::Mission_Command& cmd, mavlink_mission_item_int_t& packet);

    // return the last time the mission changed in milliseconds
    uint32_t last_change_time_ms(void) const
    {
        return _last_change_time_ms;
    }

    // find the nearest landing sequence starting point (DO_LAND_START) and
    // return its index.  Returns 0 if no appropriate DO_LAND_START point can
    // be found.
    uint16_t get_landing_sequence_start(const Location &current_loc);

    // find the nearest landing sequence starting point (DO_LAND_START) and
    // switch to that mission item.  Returns false if no DO_LAND_START
    // available.
    bool jump_to_landing_sequence(const Location &current_loc);

    // jumps the mission to the closest landing abort that is planned, returns false if unable to find a valid abort
    bool jump_to_abort_landing_sequence(const Location &current_loc);

    // Scripting helpers for the above functions to fill in the location
#if AP_SCRIPTING_ENABLED
    bool jump_to_landing_sequence(void);
    bool jump_to_abort_landing_sequence(void);
#endif

    // find the closest point on the mission after a DO_RETURN_PATH_START and before DO_LAND_START or landing
    bool jump_to_closest_mission_leg(const Location &current_loc);

    // check which is the shortest route to landing an RTL via a DO_LAND_START or continuing on the current mission plan
    bool is_best_land_sequence(const Location &current_loc);

    // set in_landing_sequence flag
    void set_in_landing_sequence_flag(bool flag)
    {
        _flags.in_landing_sequence = flag;
    }

    // get in_landing_sequence flag
    bool get_in_landing_sequence_flag() const {
        return _flags.in_landing_sequence;
    }

    // get in_return_path flag
    bool get_in_return_path_flag() const {
        return _flags.in_return_path;
    }

    // force mission to resume when start_or_resume() is called
    void set_force_resume(bool force_resume)
    {
        _force_resume = force_resume;
    }

    // returns true if configured to resume
    bool is_resume() const { return _restart == 0 || _force_resume; }

    // get a reference to the AP_Mission semaphore, allowing an external caller to lock the
    // storage while working with multiple waypoints
    HAL_Semaphore &get_semaphore(void)
    {
        return _rsem;
    }

    // returns true if the mission contains the requested items
    bool contains_item(MAV_CMD command) const;

    // returns true if the mission has a terrain relative mission item
    bool contains_terrain_alt_items(void);
    
    // returns true if the mission cmd has a location
    static bool cmd_has_location(const uint16_t command);

    // reset the mission history to prevent recalling previous mission histories when restarting missions.
    void reset_wp_history(void);

    /*
      Option::FailsafeToBestLanding -  continue mission
      logic after a land if the next waypoint is a takeoff. If this
      is false then after a landing is complete the vehicle should 
      disarm and mission logic should stop
     */
    enum class Option {
        CLEAR_ON_BOOT            = (1U<<0), // clear mission on vehicle boot
        FAILSAFE_TO_BEST_LANDING = (1U<<1), // on failsafe, find fastest path along mission home
        CONTINUE_AFTER_LAND      = (1U<<2), // continue running mission (do not disarm) after land if takeoff is next waypoint
    };
    bool option_is_set(Option option) const {
        return (_options.get() & (uint16_t)option) != 0;
    }

    bool continue_after_land_check_for_takeoff(void);
    bool continue_after_land(void) const {
        return option_is_set(Option::CONTINUE_AFTER_LAND);
    }

    // user settable parameters
    static const struct AP_Param::GroupInfo var_info[];

    // allow lua to get/set any WP items in any order in a mavlink-ish kinda way.
    bool get_item(uint16_t index, mavlink_mission_item_int_t& result) const ;
    bool set_item(uint16_t index, mavlink_mission_item_int_t& source) ;

    // Jump Tags. When a JUMP_TAG is run in the mission, either via DO_JUMP_TAG or
    // by just being the next item, the tag is remembered and the age is set to 1.
    // Only the most recent tag is remembered. It's age is how many NAV items have
    // progressed since the tag was seen. While executing the tag, the
    // age will be 1. The next NAV command after it will tick the age to 2, and so on.
    bool get_last_jump_tag(uint16_t &tag, uint16_t &age) const;

    // Set the mission index to the first JUMP_TAG with this tag.
    // Returns true on success, else false if no appropriate JUMP_TAG match can be found or if setting the index failed
    bool jump_to_tag(const uint16_t tag);

    // find the first JUMP_TAG with this tag and return its index.
    // Returns 0 if no appropriate JUMP_TAG match can be found.
    uint16_t get_index_of_jump_tag(const uint16_t tag) const;

    bool is_valid_index(const uint16_t index) const { return index < _cmd_total; }

#if AP_SDCARD_STORAGE_ENABLED
    bool failed_sdcard_storage(void) const {
        return _failed_sdcard_storage;
    }
#endif

#if HAL_LOGGING_ENABLED
    void set_log_start_mission_item_bit(uint32_t bit) { log_start_mission_item_bit = bit; }
#endif

private:
    static AP_Mission *_singleton;

    static StorageAccess _storage;

    static bool stored_in_location(uint16_t id);

    struct {
        uint16_t age;   // a value of 0 means we have never seen a tag. Once a tag is seen, age will increment every time the mission index changes.
        uint16_t tag;   // most recent tag that was successfully jumped to. Only valid if age > 0
    } _jump_tag;

    struct Mission_Flags {
        mission_state state;
        bool nav_cmd_loaded;         // true if a "navigation" command has been loaded into _nav_cmd
        bool do_cmd_loaded;          // true if a "do"/"conditional" command has been loaded into _do_cmd
        bool do_cmd_all_done;        // true if all "do"/"conditional" commands have been completed (stops unnecessary searching through eeprom for do commands)
        bool in_landing_sequence;   // true if the mission has jumped to a landing
        bool resuming_mission;      // true if the mission is resuming and set false once the aircraft attains the interrupted WP
        bool in_return_path;        // true if the mission has passed a DO_RETURN_PATH_START waypoint either in the course of the mission or via a `jump_to_closest_mission_leg` call
    } _flags;

    // mission WP resume history
    uint16_t _wp_index_history[AP_MISSION_MAX_WP_HISTORY]; // storing the nav_cmd index for the last 6 WPs

    ///
    /// private methods
    ///

    /// complete - mission is marked complete and clean-up performed including calling the mission_complete_fn
    void complete();

    bool verify_command(const Mission_Command& cmd);
    bool start_command(const Mission_Command& cmd);

    /// advance_current_nav_cmd - moves current nav command forward
    //      starting_index is used to set the index from which searching will begin, leave as 0 to search from the current navigation target
    ///     do command will also be loaded
    ///     accounts for do-jump commands
    //      returns true if command is advanced, false if failed (i.e. mission completed)
    bool advance_current_nav_cmd(uint16_t starting_index = 0);

    /// advance_current_do_cmd - moves current do command forward
    ///     accounts for do-jump commands
    ///     returns true if successfully advanced (can it ever be unsuccessful?)
    void advance_current_do_cmd();

    /// get_next_cmd - gets next command found at or after start_index
    ///     returns true if found, false if not found (i.e. mission complete)
    ///     accounts for do_jump commands
    ///     increment_jump_num_times_if_found should be set to true if advancing the active navigation command
    bool get_next_cmd(uint16_t start_index, Mission_Command& cmd, bool increment_jump_num_times_if_found, bool send_gcs_msg = true);

    /// get_next_do_cmd - gets next "do" or "conditional" command after start_index
    ///     returns true if found, false if not found
    ///     stops and returns false if it hits another navigation command before it finds the first do or conditional command
    ///     accounts for do_jump commands but never increments the jump's num_times_run (get_next_nav_cmd is responsible for this)
    bool get_next_do_cmd(uint16_t start_index, Mission_Command& cmd);

    ///
    /// jump handling methods
    ///
    // init_jump_tracking - initialise jump_tracking variables
    void init_jump_tracking();

    /// get_jump_times_run - returns number of times the jump command has been run
    ///     return is signed to be consistent with do-jump cmd's repeat count which can be -1 (to signify to repeat forever)
    int16_t get_jump_times_run(const Mission_Command& cmd);

    /// increment_jump_times_run - increments the recorded number of times the jump command has been run
    void increment_jump_times_run(Mission_Command& cmd, bool send_gcs_msg = true);

    /// check_eeprom_version - checks version of missions stored in eeprom matches this library
    /// command list will be cleared if they do not match
    void check_eeprom_version();

    // check if command is a landing type command.  Asside the obvious, MAV_CMD_DO_PARACHUTE is considered a type of landing
    bool is_landing_type_cmd(uint16_t id) const;

    // check if command is a takeoff type command.
    bool is_takeoff_type_cmd(uint16_t id) const;

    // approximate the distance travelled to get to a landing.  DO_JUMP commands are observed in look forward.
    bool distance_to_landing(uint16_t index, float &tot_distance,Location current_loc);

    // Approximate the distance traveled to return to the mission path. DO_JUMP commands are observed in look forward.
    // Stop searching once reaching a landing or do-land-start
    bool distance_to_mission_leg(uint16_t index, uint16_t &search_remaining, float &rejoin_distance, uint16_t &rejoin_index, const Location& current_loc);

    // calculate the location of a resume cmd wp
    bool calc_rewind_pos(Mission_Command& rewind_cmd);

    // update progress made in mission to store last position in the event of mission exit
    void update_exit_position(void);

    void on_mission_timestamp_change();

    /// sanity checks that the masked fields are not NaN's or infinite
    static MAV_MISSION_RESULT sanity_check_params(const mavlink_mission_item_int_t& packet);

    /// check if the next nav command is a takeoff, skipping delays
    bool is_takeoff_next(uint16_t start_index);

    // pointer to main program functions
    mission_cmd_fn_t        _cmd_start_fn;  // pointer to function which will be called when a new command is started
    mission_cmd_fn_t        _cmd_verify_fn; // pointer to function which will be called repeatedly to ensure a command is progressing
    mission_complete_fn_t   _mission_complete_fn;   // pointer to function which will be called when mission completes

    // parameters
    AP_Int16                _cmd_total;  // total number of commands in the mission
    AP_Int16                _options;    // bitmask options for missions, currently for mission clearing on reboot but can be expanded as required
    AP_Int8                 _restart;   // controls mission starting point when entering Auto mode (either restart from beginning of mission or resume from last command run)

    // internal variables
    bool                    _force_resume;  // when set true it forces mission to resume irrespective of MIS_RESTART param.
    uint16_t                _repeat_dist; // Distance to repeat on mission resume (m), can be set with MAV_CMD_DO_SET_RESUME_REPEAT_DIST
    struct Mission_Command  _nav_cmd;   // current "navigation" command.  It's position in the command list is held in _nav_cmd.index
    struct Mission_Command  _do_cmd;    // current "do" command.  It's position in the command list is held in _do_cmd.index
    struct Mission_Command  _resume_cmd;  // virtual wp command that is used to resume mission if the mission needs to be rewound on resume.
    uint16_t                _prev_nav_cmd_id;       // id of the previous "navigation" command. (WAYPOINT, LOITER_TO_ALT, ect etc)
    uint16_t                _prev_nav_cmd_index;    // index of the previous "navigation" command.  Rarely used which is why we don't store the whole command
    uint16_t                _prev_nav_cmd_wp_index; // index of the previous "navigation" command that contains a waypoint.  Rarely used which is why we don't store the whole command
    Location         _exit_position;  // the position in the mission that the mission was exited

    // jump related variables
    struct jump_tracking_struct {
        uint16_t index;                 // index of do-jump commands in mission
        int16_t num_times_run;          // number of times this jump command has been run
    } _jump_tracking[AP_MISSION_MAX_NUM_DO_JUMP_COMMANDS];

    // last time that mission changed
    uint32_t _last_change_time_ms;
    uint32_t _last_change_time_prev_ms;

    // maximum number of commands that will fit in storage
    uint16_t _commands_max;

#if AP_SDCARD_STORAGE_ENABLED
    bool _failed_sdcard_storage;
#endif

    // fast call to get command ID of a mission index
    uint16_t get_command_id(uint16_t index) const;

    // memoisation of contains-relative:
    bool _contains_terrain_alt_items;  // true if the mission has terrain-relative items
    uint32_t _last_contains_relative_calculated_ms;  // will be equal to _last_change_time_ms if _contains_terrain_alt_items is up-to-date
    bool calculate_contains_terrain_alt_items(void) const;

    // multi-thread support. This is static so it can be used from
    // const functions
    static HAL_Semaphore _rsem;

    // mission items common to all vehicles:
    bool start_command_do_aux_function(const AP_Mission::Mission_Command& cmd);
    bool start_command_do_gripper(const AP_Mission::Mission_Command& cmd);
    bool start_command_do_servorelayevents(const AP_Mission::Mission_Command& cmd);
    bool start_command_camera(const AP_Mission::Mission_Command& cmd);
    bool start_command_parachute(const AP_Mission::Mission_Command& cmd);
    bool command_do_set_repeat_dist(const AP_Mission::Mission_Command& cmd);

    bool start_command_do_sprayer(const AP_Mission::Mission_Command& cmd);
    bool start_command_do_scripting(const AP_Mission::Mission_Command& cmd);
    bool start_command_do_gimbal_manager_pitchyaw(const AP_Mission::Mission_Command& cmd);
    bool start_command_fence(const AP_Mission::Mission_Command& cmd);

    /*
      handle format conversion of storage format to allow us to update
      format to take advantage of new packing
     */
    void format_conversion(uint8_t tag_byte, const Mission_Command &cmd, PackedContent &packed_content) const;

#if HAL_LOGGING_ENABLED
    // if not -1, this bit in LOG_BITMASK specifies whether to log a message each time we start a command:
    uint32_t log_start_mission_item_bit = -1;
#endif
};

namespace AP
{
AP_Mission *mission();
};
