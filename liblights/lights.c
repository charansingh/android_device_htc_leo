/*
 * Copyright (C) 2010 Danijel Posilovic aka dan1j3l
 * Copyright (C) 2008 The Android Open Source Project
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


// #define LOG_NDEBUG 0
#define LOG_TAG "lights"

#include <cutils/log.h>

#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>

#include <sys/ioctl.h>
#include <sys/types.h>
#include <hardware/lights.h>

#include <linux/input.h>
#include <time.h>
#include <sys/stat.h>

/******************************************************************************/


static pthread_once_t g_init = PTHREAD_ONCE_INIT;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static struct light_state_t g_notification;
static struct light_state_t g_battery;

static int g_backlight 				= 255;
static int g_buttons 				= 0;
static int g_attention 				= 0;
static int button_state 			= 0;
static int screen_suspended 		= 1;
static int last_battery_state		= 0;

static time_t time_to_off;

// working threads
static pthread_t t_timed_off 		= 0;
static pthread_t t_button_checker 	= 0;
static pthread_t t_battery_checker 	= 0;

static pthread_t t_blink_button_backlight = 0;

static int blink_button				= 1;
static int battery_thread_led		= 1;
static int was_blinking_notification = 0;
static int g_brightnessMode = 0;



// HTC LEO LEDS
char const*const GREEN_LED_FILE
        = "/sys/class/leds/green/brightness";
char const*const GREEN_BLINK_FILE
        = "/sys/class/leds/green/blink";

char const*const AMBER_LED_FILE
        = "/sys/class/leds/amber/brightness";
char const*const AMBER_BLINK_FILE
        = "/sys/class/leds/amber/blink";

char const*const LCD_FILE
        = "/sys/class/leds/lcd-backlight/brightness";

char const*const BUTTON_FILE
        = "/sys/class/leds/button-backlight/brightness";

char const*const BUTTON_STATE_DEV
        = "/dev/input/event3";

char const*const BATTERY_STATUS_FILE
        = "/sys/class/power_supply/battery/status";
		
char const*const LS_FILE
	= "/sys/devices/platform/htcleo-backlight/auto_bl";

/**
device methods
*/

void init_globals(void){
    // init the mutex
    pthread_mutex_init(&g_lock, NULL);
}

static int write_int(char const* path, int value){
    int fd;
    static int already_warned = 0;

    fd = open(path, O_RDWR);
    if (fd >= 0) {
        char buffer[20];
        int bytes = sprintf(buffer, "%d\n", value);
        int amt = write(fd, buffer, bytes);
        close(fd);
        return amt == -1 ? -errno : 0;
    } else {
        if (already_warned == 0) {
            LOGE("write_int failed to open %s\n", path);
            already_warned = 1;
        }
        return -errno;
    }
}

static int is_lit(struct light_state_t const* state){
    return state->color & 0x00ffffff;
}

static int rgb_to_brightness(struct light_state_t const* state){
    int color = state->color & 0x00ffffff;
    return ((77*((color>>16)&0x00ff))+ (150*((color>>8)&0x00ff)) + (29*(color&0x00ff))) >> 8;
}



// dan1j3l EXPERIMENTAL - ONLY FOR SENSE BUILDS !!!
// TODO: Add sense detection and start this thread only in sense builds !!!
// Battery checking service !
void *battery_state_checker(void *arg){
    int fd,size, rs;
    char state[20];
    struct timespec t;
    t.tv_nsec = 0;
    t.tv_sec = 5;
	
	struct timespec t2;
    t.tv_nsec = 0;
    t.tv_sec = 1;
        
    
    while (battery_thread_led){        
        
        memset(&state[0], 0, sizeof(state));
        
        fd = open(BATTERY_STATUS_FILE,O_RDONLY);
        read(fd, state,20);     
        close(fd);    
        
        rs=0;
        rs = sprintf(state,"%s",state);
        
        if ( !was_blinking_notification && last_battery_state != rs){
            last_battery_state=rs;
            
            if ( rs == 9){ // Charging
				write_int(GREEN_LED_FILE, 0);
                write_int(AMBER_BLINK_FILE, 0);
				nanosleep(&t2,NULL);
				write_int(AMBER_LED_FILE, 1);
            }else if(rs == 5){ // FULL
                write_int(AMBER_LED_FILE, 0);
                write_int(GREEN_BLINK_FILE, 0); 
				nanosleep(&t2,NULL);
				write_int(GREEN_LED_FILE, 1);
            }else{
                write_int(AMBER_LED_FILE, 0);
                write_int(GREEN_LED_FILE, 0);
            }
             
        }
                
        nanosleep(&t,NULL);
    }
	
	t_battery_checker = 0;
    
    return 0;
}
void start_battery_checker(){
	if (t_button_checker == 0) 
		pthread_create(&t_battery_checker, NULL, battery_state_checker, NULL);
}



// Functions for timed powering on buttons
void *button_state_checker(void *arg) {
    struct input_event ev[64];
	struct timespec t;
    int fd, size = sizeof (struct input_event);
	t.tv_nsec= 0;
	t.tv_sec = 1;
	
    fd = open(BUTTON_STATE_DEV,O_RDONLY);
    
    while (1){
                
        if (screen_suspended == 1 ){
			write_int(BUTTON_FILE,0);
            button_state = 0;
			break;
		}
        
        read (fd, ev, size * 64);
        
        if (ev[0].value == 1){
            write_int(BUTTON_FILE,1);
            button_state = 1;
            button_timed_off();
			nanosleep(&t,NULL);
        }
    }

	close(fd);
	t_button_checker = 0; // reset thread so we can recreate it later
	
    return 0;
}
int do_check_button_state() {

	if (t_button_checker == 0) // create thread only if does not exist
		pthread_create(&t_button_checker, NULL, button_state_checker, NULL);

	return 0;
}

void *blink_button_backlight(void *arg) {
	struct timespec t;
	t.tv_nsec= 0;
	t.tv_sec = 1;
	
	blink_button = 1;
    
    while (blink_button){
        write_int(BUTTON_FILE,1);
		nanosleep(&t,NULL);
		write_int(BUTTON_FILE,0);
		nanosleep(&t,NULL);
    }

	t_blink_button_backlight = 0; // reset thread so we can recreate it later
	
    return 0;
}
int do_blink_button_backlight() {

	if (t_blink_button_backlight == 0) // create thread only if does not exist
		pthread_create(&t_blink_button_backlight, NULL, blink_button_backlight, NULL);

	return 0;
}


// Functions for timed powering off buttons
void *timer_button_off(void *arg) {
    struct timespec t;
	time_t secs;

	t.tv_nsec = 0;
    t.tv_sec = 1;    
	secs = time(NULL);

	// wait untill is time for power off buttons
	while (secs < time_to_off){
		secs = time(NULL);
		nanosleep(&t, NULL);
	};

	// if screen is suspended and buttons already turned off we exit thread
    if (screen_suspended == 1 && button_state == 0) {
		t_timed_off = 0; // reset thread
		return 0;
	}

	// turn off button lights
    write_int(BUTTON_FILE,0);
    button_state=0;

	// we check button states only if screen is active, else we close both threads
	if (screen_suspended == 0) do_check_button_state();

	t_timed_off = 0; // reset thread
	return 0;
}
int button_timed_off() {
	// we set time for power off  (time now + 10 secs)
	time_to_off = time(NULL) + 10;
	
	if (t_timed_off == 0) // if thread does not exist, we crate it
		pthread_create(&t_timed_off, NULL, timer_button_off, NULL);

	return 0;
}


static int set_light_buttons(struct light_device_t* dev,struct light_state_t const* state){
	LOGD("set_light_buttons");
	int err = 0;
    /*int on = is_lit(state);
    pthread_mutex_lock(&g_lock);
	g_buttons = on;
	err = write_int(BUTTON_FILE, on?255:0);
    pthread_mutex_unlock(&g_lock);
*/
	return err;
}


static int set_light_backlight(struct light_device_t* dev,struct light_state_t const* state){
	LOGD("set_light_backlight");
    int err = 0;
    int brightness = rgb_to_brightness(state);
    pthread_mutex_lock(&g_lock);

    // On/Off Buttons
	
	if (g_brightnessMode != state->brightnessMode) {
        g_brightnessMode = state->brightnessMode;
        LOGD("Switched brightnessMode=%d brightness=%d\n",g_brightnessMode,
            brightness);
        write_int(LS_FILE, state->brightnessMode);
    }
    // if we switched to user mode, allow for setting the backlight immedeately
    if (g_brightnessMode == BRIGHTNESS_MODE_USER || brightness==0 || g_backlight==0){
        LOGD("Setting brightnessMode=%d brightness=%d\n", g_brightnessMode,
            brightness);
        err = write_int(LCD_FILE, brightness);
    }
	
	
	
    if (brightness == 0 && button_state == 1) {
//        LOGD("button off");
        if(g_backlight) err = write_int(BUTTON_FILE,0);
        button_state=0;
        screen_suspended = 1;
    }else if (brightness > 0 && button_state == 0){
//        LOGD("button on");
        if(!g_backlight) err = write_int(BUTTON_FILE,1);
        button_state=1;
        screen_suspended = 0;
        // after powering buttons off, it's time to shut them down after declared amount of time (for now 10 sec)
        button_timed_off();
    }
	g_backlight = brightness;
    pthread_mutex_unlock(&g_lock);
    return err;
}

// dan1j3l: maybe remove this funct
static int set_light_keyboard(struct light_device_t* dev,struct light_state_t const* state){
	LOGD("set_light_keyboard");
    // Nothing to do in leo
    return 0;
}

static void blinkLed(unsigned int color, int blink){
	struct timespec t;
	int red, green, blue;
	
    t.tv_nsec = 0;
    t.tv_sec = 1;
	
	
	red = (color >> 16) & 0xFF;
    green = (color >> 8) & 0xFF;
    blue = color & 0xFF;
	
	
    if (red) { // Amber on / blink
		//if(blink){
		//	battery_thread_led = 0; // turn off battery thread for a while
		//}
        write_int(GREEN_LED_FILE, 0);
        write_int(AMBER_LED_FILE, 1);
		nanosleep(&t, NULL);
        if(blink) write_int(AMBER_BLINK_FILE, blink); 
        LOGD("amber on, blink:%d",blink);
    } else if (green || blue || color == 0x1) { // Green on / blink
        write_int(AMBER_LED_FILE, 0);
        write_int(GREEN_LED_FILE, 1);
		nanosleep(&t, NULL);
        if(blink) write_int(GREEN_BLINK_FILE, blink);
        LOGD("green on, blink:%d",blink);
    } else { // Leds off
        write_int(GREEN_LED_FILE, 0);
        write_int(AMBER_LED_FILE, 0);
        write_int(AMBER_BLINK_FILE, 0);
        write_int(GREEN_BLINK_FILE, 0);
		//start_battery_checker();
        LOGD("leds off");
    }
	
	LOGD("light-colors red:%d green:%d blue:%d",red,green,blue);
}

// dan1j3l TODO: Refactor, and simplify this funct
static int set_speaker_light_locked(struct light_device_t* dev,struct light_state_t const* state){
    int blink;
    
	unsigned int colorRGB;

    LOGD("set_speaker_light_locked");

    // Blink or solid
    switch (state->flashMode) {
        case LIGHT_FLASH_TIMED:
            blink = 1;
            break;
        case LIGHT_FLASH_NONE:
            blink = 0;
            break;
        default:
            blink = 0;
            break;
    }

    // dan1j3l: TODO: simplify color detection
    colorRGB = state->color;

    // dan1j3l BUG: on some android releases leds needs to be reseted by putting blink & brightness to 0 before enabling them
	
	blinkLed(colorRGB, blink);
	
    return 0;
}

// dan1j3l TODO: Refactor this funct
static void handle_speaker_battery_locked(struct light_device_t* dev){

    LOGD("handle_speaker_battery_locked");

    if (is_lit(&g_battery)) {
        set_speaker_light_locked(dev, &g_battery);
    } else {
        set_speaker_light_locked(dev, &g_notification);
    }
}

static int set_light_battery(struct light_device_t* dev,struct light_state_t const* state){

    LOGD("set_light_battery");

    pthread_mutex_lock(&g_lock);
    g_battery = *state;

    handle_speaker_battery_locked(dev);

    pthread_mutex_unlock(&g_lock);
    return 0;
}

static int set_light_notifications(struct light_device_t* dev,struct light_state_t const* state){

    LOGD("set_light_notifications");
    pthread_mutex_lock(&g_lock);
    g_notification = *state;

    handle_speaker_battery_locked(dev);

    pthread_mutex_unlock(&g_lock);
    return 0;
}


// dan1j3l BUG: Button lights ?
static int set_light_attention(struct light_device_t* dev,struct light_state_t const* state){
    pthread_mutex_lock(&g_lock);

/*    if (state->flashMode == LIGHT_FLASH_HARDWARE) {
        g_attention = state->flashOnMS;
    } else if (state->flashMode == LIGHT_FLASH_NONE) {
        g_attention = 0;
    }
	
    int mode = g_attention;*/

	if(is_lit(state) && state->flashOnMS) {
		do_blink_button_backlight();
	} else {
		blink_button = 0;
	}
	LOGD("set_light_attention: %p(%d,%d,%d)\n", state->color, state->flashMode, state->flashOnMS, state->flashOffMS);

/*    if (mode == 7 && g_backlight) {
        mode = 0;
    }*/

    pthread_mutex_unlock(&g_lock);
    return 0;
}

/** New stuff */
// dan1j3l TODO: Verify each device and create led detection

static int set_light_bluetooth(struct light_device_t* dev,struct light_state_t const* state){
    LOGD("set_light_bluetooth");
    return 0;
}

static int set_light_wifi(struct light_device_t* dev,struct light_state_t const* state){
    LOGD("set_light_wifi");
    return 0;
}

static int set_light_dualled(struct light_device_t* dev,struct light_state_t const* state){

    pthread_mutex_lock(&g_lock);
	
	// we only care for blinking states atm
	if(is_lit(state) && state->flashOnMS) {
		blinkLed(state->color,1);
		was_blinking_notification = 1;
	} else if(was_blinking_notification && !is_lit(state)) {
		blinkLed(0,0);
		was_blinking_notification = 0;
	}
	LOGD("set_light_dualled: %p(%d,%d,%d)\n", state->color, state->flashMode, state->flashOnMS, state->flashOffMS);

    pthread_mutex_unlock(&g_lock);
    return 0;
	
	
    /*
    // Blink or solid
    switch (state->flashMode) {
        case LIGHT_FLASH_TIMED:
            blink = 1;
            break;
        case LIGHT_FLASH_HARDWARE:
            blink = 1;
            break;
        case LIGHT_FLASH_NONE:
            blink = 0;
            break;
        default:
            blink = 0;
            break;
    }

    
    LOGD("dl mode:%d, offMS:%d, OnMS:%d, brightness:%d",state->flashMode,state->flashOffMS,state->flashOnMS,state->brightnessMode);
        
    
    // dan1j3l: TODO: simplify color detection
    colorRGB = state->color;
    red = (colorRGB >> 16) & 0xFF;
    green = (colorRGB >> 8) & 0xFF;
    blue = colorRGB & 0xFF;

    // dan1j3l TEST ON DIFFERENT BUILDS !!!
    if (state->flashMode == LIGHT_FLASH_NONE && red == 0 && green == 0 && blue == 0){
        // turn amber on
        LOGD("amber on");
        write_int(AMBER_BLINK_FILE,0);
        write_int(AMBER_LED_FILE,1);
    }else{
        //turn amber off
        LOGD("amber off");
        write_int(AMBER_LED_FILE,0);
    }
    
    
    
    LOGD("dl fullcolor:%d",state->color);
    LOGD("dl-color red:%d green:%d blue:%d",red,green,blue);

    */
    
    pthread_mutex_unlock(&g_lock);
    return 0;
}

// dan1j3l ???
static int set_light_capsfunc(struct light_device_t* dev,struct light_state_t const* state){
    LOGD("set_light_capsfunc");
    return 0;
}

// dan1j3l - Nothing interesting for leo
static int set_light_jogball(struct light_device_t* dev,struct light_state_t const* state){
    LOGD("set_light_jogball");
    return 0;
}

/** ****************************************************/


/** Close the lights device */
static int close_lights(struct light_device_t *dev){
    if (dev) {
        free(dev);
    }
    return 0;
}


/******************************************************************************/

/**
 * module methods
 */

/** Open a new instance of a lights device using name */
static int open_lights(const struct hw_module_t* module, char const* name,struct hw_device_t** device){

    int (*set_light)(struct light_device_t* dev,struct light_state_t const* state);

    if (0 == strcmp(LIGHT_ID_BACKLIGHT, name)) {
        set_light = set_light_backlight;
        LOGD("init backlight, id:%s",name);
    }
    else if (0 == strcmp(LIGHT_ID_KEYBOARD, name)) {  //dan1j3l: Ignored on leo
        set_light = set_light_keyboard;
        LOGD("init keyboard, id:%s",name);
    }
    else if (0 == strcmp(LIGHT_ID_BUTTONS, name)) {
        set_light = set_light_buttons;
        LOGD("init buttons, id:%s",name);
    }
    else if (0 == strcmp(LIGHT_ID_BATTERY, name)) {
        set_light = set_light_battery;
        start_battery_checker(); // needed for sense builds
        LOGD("init battery, id:%s",name);
    }
    else if (0 == strcmp(LIGHT_ID_NOTIFICATIONS, name)) {
        set_light = set_light_notifications;
        LOGD("init notifications, id:%s",name);
    }
    else if (0 == strcmp(LIGHT_ID_ATTENTION, name)) {
        set_light = set_light_attention;
        LOGD("init attention, id:%s",name);
    }
    else if (0 == strcmp(LIGHT_ID_BLUETOOTH, name)) {
        set_light = set_light_bluetooth;
        LOGD("init bluetooth, id:%s",name);
    }
    else if (0 == strcmp(LIGHT_ID_WIFI, name)) {
        set_light = set_light_wifi;
        LOGD("init wifi, id:%s",name);
    }
    else if (0 == strcmp(LIGHT_ID_DUALLED, name)) {
        set_light = set_light_dualled;
        LOGD("init dualled, id:%s",name);
    }
    else if (0 == strcmp(LIGHT_ID_CAPSFUNC, name)) { // dan1j3l: what is capsfunc ?
        set_light = set_light_capsfunc;
        LOGD("init capsfunc, id:%s",name);
    }
    else if (0 == strcmp(LIGHT_ID_JOGBALL, name)) { // dan1j3l: is needed in leo ?
        set_light = set_light_jogball;
        LOGD("init jogball, id:%s",name);
    }
    else {
        LOGD("Undefined device, id:%s",name);
        return -EINVAL;
    }


    pthread_once(&g_init, init_globals);
        
    struct light_device_t *dev = malloc(sizeof(struct light_device_t));
    memset(dev, 0, sizeof(*dev));

    dev->common.tag = HARDWARE_DEVICE_TAG;
    dev->common.version = 0;
    dev->common.module = (struct hw_module_t*)module;
    dev->common.close = (int (*)(struct hw_device_t*))close_lights;
    dev->set_light = set_light;

    *device = (struct hw_device_t*)dev;
    return 0;
}


static struct hw_module_methods_t lights_module_methods = {
    .open =  open_lights,
};

/*
 * The lights Module
 */
const struct hw_module_t HAL_MODULE_INFO_SYM = {
    .tag = HARDWARE_MODULE_TAG,
    .version_major = 1,
    .version_minor = 0,
    .id = LIGHTS_HARDWARE_MODULE_ID,
    .name = "HTC Leo Lights module",
    .author = "dan1j3l",
    .methods = &lights_module_methods,
};
