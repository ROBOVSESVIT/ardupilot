/// @file	RC_Channel.h
/// @brief	RC_Channel manager, with EEPROM-backed storage of constants.
#pragma once

#include "RC_Channel_config.h"

#if AP_RC_CHANNEL_ENABLED

#include <AP_Common/AP_Common.h>
#include <AP_Param/AP_Param.h>
#include <AP_Math/AP_Math.h>
#include <AP_Common/Bitmask.h>

#define NUM_RC_CHANNELS 16

/// @class	RC_Channel
/// @brief	Object managing one RC channel
class RC_Channel {
public:
    friend class RC_Channels;
    // Constructor
    RC_Channel(void);

    enum class ControlType {
        ANGLE = 0,
        RANGE = 1,
    };

    // ch returns the radio channel be read, starting at 1.  so
    // typically Roll=1, Pitch=2, throttle=3, yaw=4.  If this returns
    // 0 then this is the dummy object which means that one of roll,
    // pitch, yaw or throttle has not been configured correctly.
    uint8_t ch() const { return ch_in + 1; }

    // setup the control preferences
    void        set_range(uint16_t high);
    uint16_t    get_range() const { return high_in; }
    void        set_angle(uint16_t angle);
    bool        get_reverse(void) const;
    void        set_default_dead_zone(int16_t dzone);
    uint16_t    get_dead_zone(void) const { return dead_zone; }

    // get the center stick position expressed as a control_in value
    int16_t     get_control_mid() const;

    // read input from hal.rcin - create a control_in value
    bool        update(void);

    // calculate an angle given dead_zone and trim. This is used by the quadplane code
    // for hover throttle
    int16_t     pwm_to_angle_dz_trim(uint16_t dead_zone, uint16_t trim) const;

    // return a normalised input for a channel, in range -1 to 1,
    // centered around the channel trim. Ignore deadzone.
    float       norm_input() const;

    // return a normalised input for a channel, in range -1 to 1,
    // centered around the channel trim. Take into account the deadzone
    float       norm_input_dz() const;

    // return a normalised input for a channel, in range -1 to 1,
    // ignores trim and deadzone
    float       norm_input_ignore_trim() const;

    // returns true if input is within deadzone of min
    bool        in_min_dz() const;

    uint8_t     percent_input() const;

    static const struct AP_Param::GroupInfo var_info[];

    // return true if input is within deadzone of trim
    bool       in_trim_dz() const;

    int16_t    get_radio_in() const { return radio_in;}
    void       set_radio_in(int16_t val) {radio_in = val;}

    int16_t    get_control_in() const { return control_in;}
    void       set_control_in(int16_t val) { control_in = val;}

    void       clear_override();
    void       set_override(const uint16_t v, const uint32_t timestamp_ms);
    bool       has_override() const;

    float    stick_mixing(const float servo_in);

    // get control input with zero deadzone
    int16_t    get_control_in_zero_dz(void) const;

    int16_t    get_radio_min() const {return radio_min.get();}

    int16_t    get_radio_max() const {return radio_max.get();}

    int16_t    get_radio_trim() const { return radio_trim.get();}

    void       set_and_save_trim() { radio_trim.set_and_save_ifchanged(radio_in);}

    // set and save trim if changed
    void       set_and_save_radio_trim(int16_t val) { radio_trim.set_and_save_ifchanged(val);}

    // check if any of the trim/min/max param are configured, this would indicate that the user has done a calibration at somepoint
    bool       configured() { return radio_min.configured() || radio_max.configured() || radio_trim.configured(); }

    ControlType get_type(void) const { return type_in; }

    AP_Int16    option; // e.g. activate EPM gripper / enable fence

    // auxiliary switch support
    void init_aux();
    bool read_aux();

    // Aux Switch enumeration
    enum class AUX_FUNC {
        DO_NOTHING =           0, // aux switch disabled
        FLIP =                 2, // flip
        SIMPLE_MODE =          3, // change to simple mode
        RTL =                  4, // change to RTL flight mode
        SAVE_TRIM =            5, // save current position as level
        SAVE_WP =              7, // save mission waypoint or RTL if in auto mode
        CAMERA_TRIGGER =       9, // trigger camera servo or relay
        RANGEFINDER =         10, // allow enabling or disabling rangefinder in flight which helps avoid surface tracking when you are far above the ground
        FENCE =               11, // allow enabling or disabling fence in flight
        RESETTOARMEDYAW =     12, // UNUSED
        SUPERSIMPLE_MODE =    13, // change to simple mode in middle, super simple at top
        ACRO_TRAINER =        14, // low = disabled, middle = leveled, high = leveled and limited
        SPRAYER =             15, // enable/disable the crop sprayer
        AUTO =                16, // change to auto flight mode
        AUTOTUNE_MODE =       17, // auto tune
        LAND =                18, // change to LAND flight mode
        GRIPPER =             19, // Operate cargo grippers low=off, middle=neutral, high=on
        PARACHUTE_ENABLE  =   21, // Parachute enable/disable
        PARACHUTE_RELEASE =   22, // Parachute release
        PARACHUTE_3POS =      23, // Parachute disable, enable, release with 3 position switch
        MISSION_RESET =       24, // Reset auto mission to start from first command
        ATTCON_FEEDFWD =      25, // enable/disable the roll and pitch rate feed forward
        ATTCON_ACCEL_LIM =    26, // enable/disable the roll, pitch and yaw accel limiting
        RETRACT_MOUNT1 =      27, // Retract Mount1
        RELAY =               28, // Relay pin on/off (only supports first relay)
        LANDING_GEAR =        29, // Landing gear controller
        LOST_VEHICLE_SOUND =  30, // Play lost vehicle sound
        MOTOR_ESTOP =         31, // Emergency Stop Switch
        MOTOR_INTERLOCK =     32, // Motor On/Off switch
        BRAKE =               33, // Brake flight mode
        RELAY2 =              34, // Relay2 pin on/off
        RELAY3 =              35, // Relay3 pin on/off
        RELAY4 =              36, // Relay4 pin on/off
        THROW =               37, // change to THROW flight mode
        AVOID_ADSB =          38, // enable AP_Avoidance library
        PRECISION_LOITER =    39, // enable precision loiter
        AVOID_PROXIMITY =     40, // enable object avoidance using proximity sensors (ie. horizontal lidar)
        ARMDISARM_UNUSED =    41, // UNUSED
        SMART_RTL =           42, // change to SmartRTL flight mode
        INVERTED  =           43, // enable inverted flight
        WINCH_ENABLE =        44, // winch enable/disable
        WINCH_CONTROL =       45, // winch control
        RC_OVERRIDE_ENABLE =  46, // enable RC Override
        USER_FUNC1 =          47, // user function #1
        USER_FUNC2 =          48, // user function #2
        USER_FUNC3 =          49, // user function #3
        LEARN_CRUISE =        50, // learn cruise throttle (Rover)
        MANUAL       =        51, // manual mode
        ACRO         =        52, // acro mode
        STEERING     =        53, // steering mode
        HOLD         =        54, // hold mode
        GUIDED       =        55, // guided mode
        LOITER       =        56, // loiter mode
        FOLLOW       =        57, // follow mode
        CLEAR_WP     =        58, // clear waypoints
        SIMPLE       =        59, // simple mode
        ZIGZAG       =        60, // zigzag mode
        ZIGZAG_SaveWP =       61, // zigzag save waypoint
        COMPASS_LEARN =       62, // learn compass offsets
        SAILBOAT_TACK =       63, // rover sailboat tack
        REVERSE_THROTTLE =    64, // reverse throttle input
        GPS_DISABLE  =        65, // disable GPS for testing
        RELAY5 =              66, // Relay5 pin on/off
        RELAY6 =              67, // Relay6 pin on/off
        STABILIZE =           68, // stabilize mode
        POSHOLD   =           69, // poshold mode
        ALTHOLD   =           70, // althold mode
        FLOWHOLD  =           71, // flowhold mode
        CIRCLE    =           72, // circle mode
        DRIFT     =           73, // drift mode
        SAILBOAT_MOTOR_3POS = 74, // Sailboat motoring 3pos
        SURFACE_TRACKING =    75, // Surface tracking upwards or downwards
        STANDBY  =            76, // Standby mode
        TAKEOFF   =           77, // takeoff
        RUNCAM_CONTROL =      78, // control RunCam device
        RUNCAM_OSD_CONTROL =  79, // control RunCam OSD
        VISODOM_ALIGN =       80, // align visual odometry camera's attitude to AHRS
        DISARM =              81, // disarm vehicle
        Q_ASSIST =            82, // disable, enable and force Q assist
        ZIGZAG_Auto =         83, // zigzag auto switch
        AIRMODE =             84, // enable / disable airmode for copter
        GENERATOR   =         85, // generator control
        TER_DISABLE =         86, // disable terrain following in CRUISE/FBWB modes
        CROW_SELECT =         87, // select CROW mode for diff spoilers;high disables,mid forces progressive
        SOARING =             88, // three-position switch to set soaring mode
        LANDING_FLARE =       89, // force flare, throttle forced idle, pitch to LAND_PITCH_DEG, tilts up
        EKF_SOURCE_SET =      90, // change EKF data source set between primary, secondary and tertiary
        ARSPD_CALIBRATE=      91, // calibrate airspeed ratio 
        FBWA =                92, // Fly-By-Wire-A
        RELOCATE_MISSION =    93, // used in separate branch MISSION_RELATIVE
        VTX_POWER =           94, // VTX power level
        FBWA_TAILDRAGGER =    95, // enables FBWA taildragger takeoff mode. Once this feature is enabled it will stay enabled until the aircraft goes above TKOFF_TDRAG_SPD1 airspeed, changes mode, or the pitch goes above the initial pitch when this is engaged or goes below 0 pitch. When enabled the elevator will be forced to TKOFF_TDRAG_ELEV. This option allows for easier takeoffs on taildraggers in FBWA mode, and also makes it easier to test auto-takeoff steering handling in FBWA.
        MODE_SWITCH_RESET =   96, // trigger re-reading of mode switch
        WIND_VANE_DIR_OFSSET= 97, // flag for windvane direction offset input, used with windvane type 2
        TRAINING            = 98, // mode training
        AUTO_RTL =            99, // AUTO RTL via DO_LAND_START

        // entries from 100-150  are expected to be developer
        // options used for testing
        KILL_IMU1 =          100, // disable first IMU (for IMU failure testing)
        KILL_IMU2 =          101, // disable second IMU (for IMU failure testing)
        CAM_MODE_TOGGLE =    102, // Momentary switch to cycle camera modes
        EKF_LANE_SWITCH =    103, // trigger lane switch attempt
        EKF_YAW_RESET =      104, // trigger yaw reset attempt
        GPS_DISABLE_YAW =    105, // disable GPS yaw for testing
        DISABLE_AIRSPEED_USE = 106, // equivalent to AIRSPEED_USE 0
        FW_AUTOTUNE =          107, // fixed wing auto tune
        QRTL =               108, // QRTL mode
        CUSTOM_CONTROLLER =  109,  // use Custom Controller
        KILL_IMU3 =          110, // disable third IMU (for IMU failure testing)
        LOWEHEISER_STARTER = 111,  // allows for manually running starter
        AHRS_TYPE =          112, // change AHRS_EKF_TYPE
        RETRACT_MOUNT2 =     113, // Retract Mount2

        // if you add something here, make sure to update the documentation of the parameter in RC_Channel.cpp!
        // also, if you add an option >255, you will need to fix duplicate_options_exist

        // options 150-199 continue user rc switch options
        CRUISE =             150,  // CRUISE mode
        TURTLE =             151,  // Turtle mode - flip over after crash
        SIMPLE_HEADING_RESET = 152, // reset simple mode reference heading to current
        ARMDISARM =          153, // arm or disarm vehicle
        ARMDISARM_AIRMODE =  154, // arm or disarm vehicle enabling airmode
        TRIM_TO_CURRENT_SERVO_RC = 155, // trim to current servo and RC
        TORQEEDO_CLEAR_ERR = 156, // clear torqeedo error
        EMERGENCY_LANDING_EN = 157, //Force long FS action to FBWA for landing out of range
        OPTFLOW_CAL =        158, // optical flow calibration
        FORCEFLYING =        159, // enable or disable land detection for GPS based manual modes preventing land detection and maintainting set_throttle_mix_max
        WEATHER_VANE_ENABLE = 160, // enable/disable weathervaning
        TURBINE_START =      161, // initialize turbine start sequence
        FFT_NOTCH_TUNE =     162, // FFT notch tuning function
        MOUNT_LOCK =         163, // Mount yaw lock vs follow
        LOG_PAUSE =          164, // Pauses logging if under logging rate control
        ARM_EMERGENCY_STOP = 165, // ARM on high, MOTOR_ESTOP on low
        CAMERA_REC_VIDEO =   166, // start recording on high, stop recording on low
        CAMERA_ZOOM =        167, // camera zoom high = zoom in, middle = hold, low = zoom out
        CAMERA_MANUAL_FOCUS = 168,// camera manual focus.  high = long shot, middle = stop focus, low = close shot
        CAMERA_AUTO_FOCUS =  169, // camera auto focus
        QSTABILIZE =         170, // QuadPlane QStabilize mode
        MAG_CAL =            171, // Calibrate compasses (disarmed only)
        BATTERY_MPPT_ENABLE = 172,// Battery MPPT Power enable. high = ON, mid = auto (controlled by mppt/batt driver), low = OFF. This effects all MPPTs.
        PLANE_AUTO_LANDING_ABORT = 173, // Abort Glide-slope or VTOL landing during payload place or do_land type mission items
        CAMERA_IMAGE_TRACKING = 174, // camera image tracking
        CAMERA_LENS =        175, // camera lens selection
        VFWD_THR_OVERRIDE =  176, // force enabled VTOL forward throttle method
        MOUNT_LRF_ENABLE =   177,  // mount LRF enable/disable
        FLIGHTMODE_PAUSE =   178,  // e.g. pause movement towards waypoint
        ICE_START_STOP =     179, // AP_ICEngine start stop
        AUTOTUNE_TEST_GAINS = 180, // auto tune tuning switch to test or revert gains
        QUICKTUNE =          181,  //quicktune 3 position switch
                                  // saved for 4.7-dev feature in-flight AHRS autotrim
                                  //saved for 4.7-dev feature Fixed Wing AUTOLAND Mode
        SYSTEMID =           184,  // system ID as an aux switch

        // inputs from 200 will eventually used to replace RCMAP
        ROLL =               201, // roll input
        PITCH =              202, // pitch input
        THROTTLE =           203, // throttle pilot input
        YAW =                204, // yaw pilot input
        MAINSAIL =           207, // mainsail input
        FLAP =               208, // flap input
        FWD_THR =            209, // VTOL manual forward throttle
        AIRBRAKE =           210, // manual airbrake control
        WALKING_HEIGHT =     211, // walking robot height input
        MOUNT1_ROLL =        212, // mount1 roll input
        MOUNT1_PITCH =       213, // mount1 pitch input
        MOUNT1_YAW =         214, // mount1 yaw input
        MOUNT2_ROLL =        215, // mount2 roll input
        MOUNT2_PITCH =       216, // mount3 pitch input
        MOUNT2_YAW =         217, // mount4 yaw input
        LOWEHEISER_THROTTLE= 218,  // allows for throttle on slider
        TRANSMITTER_TUNING = 219, // use a transmitter knob or slider for in-flight tuning

        // inputs 248-249 are reserved for the Skybrush fork at
        // https://github.com/skybrush-io/ardupilot

        // inputs for the use of onboard lua scripting
        SCRIPTING_1 =        300,
        SCRIPTING_2 =        301,
        SCRIPTING_3 =        302,
        SCRIPTING_4 =        303,
        SCRIPTING_5 =        304,
        SCRIPTING_6 =        305,
        SCRIPTING_7 =        306,
        SCRIPTING_8 =        307,

        // this must be higher than any aux function above
        AUX_FUNCTION_MAX =   308,
    };

    // auxiliary switch handling (n.b.: we store this as 2-bits!):
    enum class AuxSwitchPos : uint8_t {
        LOW,       // indicates auxiliary switch is in the low position (pwm <1200)
        MIDDLE,    // indicates auxiliary switch is in the middle position (pwm >1200, <1800)
        HIGH       // indicates auxiliary switch is in the high position (pwm >1800)
    };

    enum class AuxFuncTriggerSource : uint8_t {
        INIT,
        RC,
        BUTTON,
        MAVLINK,
        MISSION,
        SCRIPTING,
    };

    AuxSwitchPos get_aux_switch_pos() const;

    // aux position for stick gestures used by RunCam menus etc
    AuxSwitchPos get_stick_gesture_pos() const;

    // wrapper function around do_aux_function which allows us to log
    bool run_aux_function(AUX_FUNC ch_option, AuxSwitchPos pos, AuxFuncTriggerSource source);

#if AP_RC_CHANNEL_AUX_FUNCTION_STRINGS_ENABLED
    const char *string_for_aux_function(AUX_FUNC function) const;
    const char *string_for_aux_pos(AuxSwitchPos pos) const;
#endif
    // pwm value under which we consider that Radio value is invalid
    static const uint16_t RC_MIN_LIMIT_PWM = 800;
    // pwm value above which we consider that Radio value is invalid
    static const uint16_t RC_MAX_LIMIT_PWM = 2200;

    // pwm value above which we condider that Radio min value is invalid
    static const uint16_t RC_CALIB_MIN_LIMIT_PWM = 1300;
    // pwm value under which we condider that Radio max value is invalid
    static const uint16_t RC_CALIB_MAX_LIMIT_PWM = 1700;

    // pwm value above which the switch/button will be invoked:
    static const uint16_t AUX_SWITCH_PWM_TRIGGER_HIGH = 1800;
    // pwm value below which the switch/button will be disabled:
    static const uint16_t AUX_SWITCH_PWM_TRIGGER_LOW = 1200;

    // pwm value above which the option will be invoked:
    static const uint16_t AUX_PWM_TRIGGER_HIGH = 1700;
    // pwm value below which the option will be disabled:
    static const uint16_t AUX_PWM_TRIGGER_LOW = 1300;

protected:

    virtual void init_aux_function(AUX_FUNC ch_option, AuxSwitchPos);

    // virtual function to be overridden my subclasses
    virtual bool do_aux_function(AUX_FUNC ch_option, AuxSwitchPos);

    void do_aux_function_armdisarm(const AuxSwitchPos ch_flag);
    void do_aux_function_avoid_adsb(const AuxSwitchPos ch_flag);
    void do_aux_function_avoid_proximity(const AuxSwitchPos ch_flag);
    void do_aux_function_camera_trigger(const AuxSwitchPos ch_flag);
    bool do_aux_function_record_video(const AuxSwitchPos ch_flag);
    bool do_aux_function_camera_zoom(const AuxSwitchPos ch_flag);
    bool do_aux_function_camera_manual_focus(const AuxSwitchPos ch_flag);
    bool do_aux_function_camera_auto_focus(const AuxSwitchPos ch_flag);
    bool do_aux_function_camera_image_tracking(const AuxSwitchPos ch_flag);
    bool do_aux_function_camera_lens(const AuxSwitchPos ch_flag);
    void do_aux_function_runcam_control(const AuxSwitchPos ch_flag);
    void do_aux_function_runcam_osd_control(const AuxSwitchPos ch_flag);
    void do_aux_function_fence(const AuxSwitchPos ch_flag);
    void do_aux_function_clear_wp(const AuxSwitchPos ch_flag);
    void do_aux_function_gripper(const AuxSwitchPos ch_flag);
    void do_aux_function_lost_vehicle_sound(const AuxSwitchPos ch_flag);
    void do_aux_function_mission_reset(const AuxSwitchPos ch_flag);
    void do_aux_function_rc_override_enable(const AuxSwitchPos ch_flag);
    void do_aux_function_relay(uint8_t relay, bool val);
    void do_aux_function_sprayer(const AuxSwitchPos ch_flag);
    void do_aux_function_generator(const AuxSwitchPos ch_flag);
    void do_aux_function_fft_notch_tune(const AuxSwitchPos ch_flag);
    void do_aux_function_retract_mount(const AuxSwitchPos ch_flag, const uint8_t instance);

    typedef int8_t modeswitch_pos_t;
    virtual void mode_switch_changed(modeswitch_pos_t new_pos) {
        // no action by default (e.g. Tracker, Sub, who do their own thing)
    };


private:

    // pwm is stored here
    int16_t     radio_in;

    // value generated from PWM normalised to configured scale
    int16_t    control_in;

    AP_Int16    radio_min;
    AP_Int16    radio_trim;
    AP_Int16    radio_max;

    AP_Int8     reversed;
    AP_Int16    dead_zone;

    ControlType type_in;
    int16_t     high_in;

    // the input channel this corresponds to
    uint8_t     ch_in;

    // overrides
    uint16_t override_value;
    uint32_t last_override_time;

    int16_t pwm_to_angle() const;
    int16_t pwm_to_angle_dz(uint16_t dead_zone) const;

    int16_t pwm_to_range() const;
    int16_t pwm_to_range_dz(uint16_t dead_zone) const;

    bool read_3pos_switch(AuxSwitchPos &ret) const WARN_IF_UNUSED;
    bool read_6pos_switch(int8_t& position) WARN_IF_UNUSED;

    // Structure used to detect and debounce switch changes
    struct {
        int8_t debounce_position = -1;
        int8_t current_position = -1;
        uint32_t last_edge_time_ms;
        bool initialised;
    } switch_state;

    void reset_mode_switch();
    void read_mode_switch();
    bool debounce_completed(int8_t position);
    // returns true if the first time we successfully read the
    // channel's three-position-switch position we should record that
    // position as the current position *without* executing the
    // associated auxiliary function.  e.g. do not attempt to arm a
    // vehicle when the user turns on their transmitter with the arm
    // switch high!
    bool init_position_on_first_radio_read(AUX_FUNC func) const;

#if AP_RC_CHANNEL_AUX_FUNCTION_STRINGS_ENABLED
    // Structure to lookup switch change announcements
    struct LookupTable{
       AUX_FUNC option;
       const char *announcement;
    };

    static const LookupTable lookuptable[];
#endif
};


/*
  class	RC_Channels. Hold the full set of RC_Channel objects
*/
class RC_Channels {
public:
    friend class SRV_Channels;
    friend class RC_Channel;
    // constructor
    RC_Channels(void);

    void init(void);

    // get singleton instance
    static RC_Channels *get_singleton() {
        return _singleton;
    }

    static const struct AP_Param::GroupInfo var_info[];

    // compatability functions for Plane:
    static uint16_t get_radio_in(const uint8_t chan) {
        RC_Channel *c = _singleton->channel(chan);
        if (c == nullptr) {
            return 0;
        }
        return c->get_radio_in();
    }
    static RC_Channel *rc_channel(const uint8_t chan) {
        return _singleton->channel(chan);
    }
    //end compatability functions for Plane

    // this function is implemented in the child class in the vehicle
    // code
    virtual RC_Channel *channel(uint8_t chan) = 0;
    // helper used by scripting to convert the above function from 0 to 1 indexeing
    // range is checked correctly by the underlying channel function
    RC_Channel *lua_rc_channel(const uint8_t chan) {
        return channel(chan -1);
    }

    uint8_t get_radio_in(uint16_t *chans, const uint8_t num_channels); // reads a block of chanel radio_in values starting from channel 0
                                                                       // returns the number of valid channels

    static uint8_t get_valid_channel_count(void);                      // returns the number of valid channels in the last read
    static int16_t get_receiver_rssi(void);                            // returns [0, 255] for receiver RSSI (0 is no link) if present, otherwise -1
    static int16_t get_receiver_link_quality(void);                         // returns 0-100 % of last 100 packets received at receiver are valid
    bool read_input(void);                                             // returns true if new input has been read in
    static void clear_overrides(void);                                 // clears any active overrides
    static bool receiver_bind(const int dsmMode);                      // puts the receiver in bind mode if present, returns true if success
    static void set_override(const uint8_t chan, const int16_t value, const uint32_t timestamp_ms = 0); // set a channels override value
    static bool has_active_overrides(void);                            // returns true if there are overrides applied that are valid

    // returns a mask indicating which channels have overrides.  Bit 0
    // is RC channel 1.  Beware this is not a cheap call.
    uint16_t get_override_mask() const;

    class RC_Channel *find_channel_for_option(const RC_Channel::AUX_FUNC option);
    bool duplicate_options_exist();
    RC_Channel::AuxSwitchPos get_channel_pos(const uint8_t rcmapchan) const;
    void convert_options(const RC_Channel::AUX_FUNC old_option, const RC_Channel::AUX_FUNC new_option);

    void init_aux_all();
    void read_aux_all();

    // mode switch handling
    void reset_mode_switch();
    virtual void read_mode_switch();

    virtual bool in_rc_failsafe() const { return true; };
    virtual bool has_valid_input() const { return false; };

    virtual RC_Channel *get_arming_channel(void) const { return nullptr; };

    bool gcs_overrides_enabled() const { return _gcs_overrides_enabled; }
    void set_gcs_overrides_enabled(bool enable) {
        _gcs_overrides_enabled = enable;
        if (!_gcs_overrides_enabled) {
            clear_overrides();
        }
    }

    enum class Option {
        IGNORE_RECEIVER         = (1U << 0), // RC receiver modules
        IGNORE_OVERRIDES        = (1U << 1), // MAVLink overrides
        IGNORE_FAILSAFE         = (1U << 2), // ignore RC failsafe bits
        FPORT_PAD               = (1U << 3), // pad fport telem output
        LOG_RAW_DATA            = (1U << 4), // log rc input bytes
        ARMING_CHECK_THROTTLE   = (1U << 5), // run an arming check for neutral throttle
        ARMING_SKIP_CHECK_RPY   = (1U << 6), // skip the an arming checks for the roll/pitch/yaw channels
        ALLOW_SWITCH_REV        = (1U << 7), // honor the reversed flag on switches
        CRSF_CUSTOM_TELEMETRY   = (1U << 8), // use passthrough data for crsf telemetry
        SUPPRESS_CRSF_MESSAGE   = (1U << 9), // suppress CRSF mode/rate message for ELRS systems
        MULTI_RECEIVER_SUPPORT  = (1U << 10), // allow multiple receivers
        USE_CRSF_LQ_AS_RSSI     = (1U << 11), // returns CRSF link quality as RSSI value, instead of RSSI
        CRSF_FM_DISARM_STAR     = (1U << 12), // when disarmed, add a star at the end of the flight mode in CRSF telemetry
        ELRS_420KBAUD           = (1U << 13), // use 420kbaud for ELRS protocol
    };

    bool option_is_enabled(Option option) const {
        return _options & uint32_t(option);
    }

    virtual bool arming_check_throttle() const {
        return option_is_enabled(Option::ARMING_CHECK_THROTTLE);
    }

    // returns true if overrides should time out.  If true is returned
    // then returned_timeout_ms will contain the timeout in
    // milliseconds, with 0 meaning overrides are disabled.
    bool get_override_timeout_ms(uint32_t &returned_timeout_ms) const {
        const float value = _override_timeout.get();
        if (is_positive(value)) {
            returned_timeout_ms = uint32_t(value * 1e3f);
            return true;
        }
        if (is_zero(value)) {
            returned_timeout_ms = 0;
            return true;
        }
        // overrides will not time out
        return false;
    }

    // get mask of enabled protocols
    uint32_t enabled_protocols() const;

    // returns true if we have had a direct detach RC receiver, does not include overrides
    bool has_had_rc_receiver() const { return _has_had_rc_receiver; }

    // returns true if we have had an override on any channel
    bool has_had_rc_override() const { return _has_had_override; }

    /*
      get the RC input PWM value given a channel number.  Note that
      channel numbers start at 1, as this API is designed for use in
      LUA
    */
    bool get_pwm(uint8_t channel, uint16_t &pwm) const;

    uint32_t last_input_ms() const { return last_update_ms; };

    // method for other parts of the system (e.g. Button and mavlink)
    // to trigger auxiliary functions
    bool run_aux_function(RC_Channel::AUX_FUNC ch_option, RC_Channel::AuxSwitchPos pos, RC_Channel::AuxFuncTriggerSource source) {
        return rc_channel(0)->run_aux_function(ch_option, pos, source);
    }

    // check if flight mode channel is assigned RC option
    // return true if assigned
    bool flight_mode_channel_conflicts_with_rc_option() const;

    // flight_mode_channel_number must be overridden in vehicle specific code
    virtual int8_t flight_mode_channel_number() const = 0;

    // set and get calibrating flag, stops arming if true
    void calibrating(bool b) { gcs_is_calibrating = b; }
    bool calibrating() { return gcs_is_calibrating; }

#if AP_SCRIPTING_ENABLED
    // get last aux cached value for scripting. Returns false if never set, otherwise 0,1,2
    bool get_aux_cached(RC_Channel::AUX_FUNC aux_fn, uint8_t &pos);
#endif

    // returns true if we've ever seen RC input, via overrides or via
    // AP_RCProtocol
    bool has_ever_seen_rc_input() const {
        return _has_ever_seen_rc_input;
    }

    // get failsafe timeout in milliseconds
    uint32_t get_fs_timeout_ms() const { return MAX(_fs_timeout * 1000, 100); }

    // methods which return RC input channels used for various axes.
    RC_Channel &get_roll_channel();
    RC_Channel &get_pitch_channel();
    RC_Channel &get_yaw_channel();
    RC_Channel &get_throttle_channel();

protected:

    void new_override_received() {
        has_new_overrides = true;
        _has_had_override = true;
    }

private:
    static RC_Channels *_singleton;
    // this static arrangement is to avoid static pointers in AP_Param tables
    static RC_Channel *channels;

    uint32_t last_update_ms;
    bool has_new_overrides;
    bool _has_had_rc_receiver; // true if we have had a direct detach RC receiver, does not include overrides
    bool _has_had_override; // true if we have had an override on any channel

    AP_Float _override_timeout;
    AP_Int32  _options;
    AP_Int32  _protocols;
    AP_Float _fs_timeout;

    // set to true if we see overrides or other RC input
    bool _has_ever_seen_rc_input;

    RC_Channel *flight_mode_channel() const;

    // Allow override by default at start
    bool _gcs_overrides_enabled = true;

    // true if GCS is performing a RC calibration
    bool gcs_is_calibrating;

#if AP_SCRIPTING_ENABLED
    // bitmask of last aux function value, 2 bits per function
    // value 0 means never set, otherwise level+1
    HAL_Semaphore aux_cache_sem;
    Bitmask<unsigned(RC_Channel::AUX_FUNC::AUX_FUNCTION_MAX)*2> aux_cached;

    void set_aux_cached(RC_Channel::AUX_FUNC aux_fn, RC_Channel::AuxSwitchPos pos);
#endif

    RC_Channel &get_rcmap_channel_nonnull(uint8_t rcmap_number);
    RC_Channel dummy_rcchannel;
};

RC_Channels &rc();

#endif  // AP_RC_CHANNEL_ENABLED
