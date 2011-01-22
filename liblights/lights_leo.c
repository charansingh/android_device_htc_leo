/*
 * Copyright (C) 2009 The Android Open Source Project
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

#define LOG_TAG "lights_leo"

#include <cutils/log.h>
#include <cutils/properties.h>

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

#include "events.h"

#define LIGHT_ATTENTION	1
#define LIGHT_NOTIFY 	2

//#define  ENABLE_LCDSAVE    
//#define  ENABLE_BATTERY_POOL

#define  ENABLE_RADIO_POOL

#define  LED_DEBUG  1

#if LED_DEBUG
#  define  D(...)   LOGD(__VA_ARGS__)
#else
#  define  D(...)   ((void)0)
#endif

/******************************************************************************/
static struct light_state_t *g_notify;
static struct light_state_t *g_attention;
static pthread_once_t g_init = PTHREAD_ONCE_INIT;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

#ifdef ENABLE_LCDSAVE
static int g_current_backlight = 0;
#endif

static int g_ts = 0;
static int g_backlight = 255;
static int g_buttons = 0;

struct led_prop {
    const char *filename;
    int fd;
    int value;
};

struct led {
    struct led_prop brightness;
    struct led_prop blink;
};

enum {
    BUTTONS_LED,
    GREEN_LED,
    AMBER_LED,
    LCD_BACKLIGHT,
    NUM_LEDS,
};

struct led leds[NUM_LEDS] = {
    [BUTTONS_LED] = {
        .brightness = { "/sys/class/leds/button-backlight/brightness", 0, 0},
	.blink = {NULL, 0, 0},
    },
    [GREEN_LED] = {
        .brightness = { "/sys/class/leds/green/brightness", 0, 0},
        .blink = { "/sys/class/leds/green/blink", 0, 0},
    },
    [AMBER_LED] = {
        .brightness = { "/sys/class/leds/amber/brightness", 0, 0},
        .blink = { "/sys/class/leds/amber/blink", 0, 0},
    },
    [LCD_BACKLIGHT] = {
        .brightness = { "/sys/class/leds/lcd-backlight/brightness", 0, 0},
	.blink = {NULL, 0, 0},
    },
};

/**
 * device methods
 */

static int init_prop(struct led_prop *prop)
{
    int fd;

    prop->fd = -1;
    if (!prop->filename)
        return 0;
    fd = open(prop->filename, O_RDWR);
    if (fd < 0) {
        LOGE("init_prop: %s cannot be opened (%s)\n", prop->filename,
             strerror(errno));
        return -errno;
    }

    prop->fd = fd;
    return 0;
}

static void close_prop(struct led_prop *prop)
{
    int fd;

    if (prop->fd > 0)
        close(prop->fd);
    return;
}

void init_globals(void)
{
    int i;
    pthread_mutex_init(&g_lock, NULL);

    for (i = 0; i < NUM_LEDS; ++i) {
        init_prop(&leds[i].brightness);
        if (leds[i].blink.filename) {
		init_prop(&leds[i].blink);
	}
    }
    g_attention = malloc(sizeof(struct light_state_t));
    memset(g_attention, 0, sizeof(*g_attention));
    g_notify = malloc(sizeof(struct light_state_t));
    memset(g_notify, 0, sizeof(*g_notify));
}

static int
write_int(struct led_prop *prop, int value)
{
    char buffer[20];
    int bytes;
    int amt;

    if (prop->fd < 0)
        return 0;
    if (prop->value != value) { 
    	//LOGV("%s %s: 0x%x\n", __func__, prop->filename, value);
    	bytes = snprintf(buffer, sizeof(buffer), "%d\n", value);
    	while (bytes > 0) {
        	amt = write(prop->fd, buffer, bytes);
        	if (amt < 0) {
        	    if (errno == EINTR)
         	       continue;
         	   return -errno;
        	}
        	bytes -= amt;
    	}
    	prop->value = value;
    }
    return 0;
}

static int
is_lit(struct light_state_t const* state)
{
    return state->color & 0x00ffffff;
}

static int
rgb_to_brightness(struct light_state_t const* state)
{
    int color = state->color & 0x00ffffff;
    return ((77*((color>>16)&0x00ff))
            + (150*((color>>8)&0x00ff)) + (29*(color&0x00ff))) >> 8;
}

//=====================================================================================
#ifdef ENABLE_BATTERY_POLL
static pthread_t battery_check_t 	= 0;
static int last_battery_state = 0;

void *battery_state_thread(void *arg){
    int fd,size, rs;
    char state[20];
    struct timespec t;
    t.tv_nsec = 0;
    t.tv_sec = 5;
        
    fd = open("/sys/class/power_supply/battery/status",O_RDONLY | O_NDELAY);
    if(fd < 0) {
       	LOGE("Couldn't open /sys/class/power_supply/battery/status\n");
        return 0;
    }

    for (;;) {            
        memset(&state[0], 0, sizeof(state));

        read(fd, state,20);     
        close(fd);    
        
        rs=0;
        rs = sprintf(state,"%s",state);
        
        if ( last_battery_state != rs){
            last_battery_state=rs;
            
            if ( rs == 9){ // Charging
    		write_int(&leds[GREEN_LED].brightness, 0);
    		write_int(&leds[AMBER_LED].brightness, 1);
    		write_int(&leds[AMBER_LED].blink, 0);
            }else if(rs == 5){ // FULL
    		write_int(&leds[GREEN_LED].brightness, 1);
    		write_int(&leds[AMBER_LED].brightness, 0);
    		write_int(&leds[GREEN_LED].blink, 0);
            }
             
        }
                
        nanosleep(&t,NULL);
    }

    close(fd);
    return 0;
}

void start_battery_thread(){
    if (battery_check_t == 0) //ensure only 1 thread 
    pthread_create(&battery_check_t, NULL, battery_state_thread, NULL);
    return;
}
#endif
//=====================================================================================
#ifdef ENABLE_RADIO_POOL
static int last_radio_state = 0;
#endif

static pthread_t events_ct = 0;
static time_t    keys_tm;
#ifdef ENABLE_LCDSAVE
static time_t    abs_tm;
static time_t    last_activity_tm;

static int user_activity_idle() {
  int fd;
  int ret =0;
  char tmp[20];

  fd= open("/sys/android_power/auto_off_timeout",O_RDONLY);
  if(fd < 0) {
       	LOGE("Couldn't open /sys/android_power/auto_off_timeout\n");
        return 0;
  }

  memset(&tmp[0], 0, sizeof(tmp));
  read(fd, tmp,20); 
    
  D("@@ %s->%s\n", __func__, tmp);
 
  close(fd);
  return (ret);
}
#endif

static int
switch_led_button(int on) {
  int err = 0;
  if (g_buttons!=on) {	
  	//D("@@ %s->%s\n", __func__, g_buttons?"ON":"OFF");
  	err = write_int(&leds[BUTTONS_LED].brightness, on);
  	keys_tm = time(NULL) + (8*on); // switch off button keypad after 8 seconds
  	g_buttons = on; 
  }
  return err;
}

static int
set_led_backlight(int level) {
  int err = 0;	
  //D("%s: [%d %d %d]\n", __func__, level, g_backlight, g_current_backlight);
  if (g_backlight != level ){
     err = write_int(&leds[LCD_BACKLIGHT].brightness, level);
     g_backlight = level;
  }
#ifdef ENABLE_LCDSAVE
  if (level>=g_current_backlight){
        g_current_backlight=level;
	abs_tm = time(NULL) + (10);
  }
#endif
  return err;
}

void *events_cthread(void *arg) {
#ifdef ENABLE_RADIO_POOL
    int radio_state = 0;
    char sim_state[PROPERTY_VALUE_MAX];
#endif
    struct input_event ev;
    struct timespec t;
    time_t a_tm, k_tm;
    //int fd;
    t.tv_nsec= 10000*1000;
    t.tv_sec = 0;

    ev_init();

    for (;;) {    
          /* radio events tracking */
#ifdef ENABLE_RADIO_POOL
          radio_state = 0;      
          if (property_get("gsm.sim.state", sim_state, NULL) && (strcmp(sim_state, "READY")==0))  {  
              radio_state = 1;  
          } 
          //radio state changed
          if ( last_radio_state != radio_state){   
	    D("@@ %s: |%s| %d->%d\n", __func__, sim_state, last_radio_state, radio_state );
            //green blink if radio is on
    	    write_int(&leds[AMBER_LED].brightness, radio_state?0:1);                    
    	    write_int(&leds[GREEN_LED].brightness, radio_state?1:0);
    	    write_int(&leds[GREEN_LED].blink, radio_state?1:0);
            last_radio_state=radio_state;   
          }    
#endif
          /* battery events tracking */
 
          /* button events tracking */
    	  ev_get(&ev, 1); 
          if (ev.type==EV_KEY) {
	       if (ev.value == 1){
                 switch (ev.code) {
                 case BTN_TOUCH:
#ifdef ENABLE_LCDSAVE
			if (g_backlight > 0) && (abs_tm==0)                                                                                                                  
		       	    set_led_backlight(g_current_backlight);			
			}
#endif
		      break;
                 case KEY_SEND:
	         case KEY_MENU:
	         case KEY_HOME:
	         case KEY_BACK:
	         case KEY_END:
	         case KEY_POWER:
#ifdef ENABLE_LCDSAVE
		     if (abs_tm==0) {set_led_backlight(g_current_backlight);}
#endif
            	     switch_led_button(1);
                     sleep(1);
                     break;
#ifdef ENABLE_LCDSAVE
                 case KEY_VOLUMEUP:
                 case KEY_VOLUMEDOWN:
		     if (abs_tm==0) { set_led_backlight(g_current_backlight);}
 		     break;
#endif
                 default:
	             /*LOGD("keys: code %d, value %d\n", ev.code, ev.value);*/
		     sleep(1);
                     break;
                }
              }
	 }
//=========================
#ifdef ENABLE_LCDSAVE
	if (g_backlight > 0) {
	   a_tm = time(NULL); 
	   //D("%ld %ld\n", a_tm, abs_tm);  
	   if ((abs_tm>0) && (a_tm >= abs_tm) && (user_activity_idle())) {
	   	//D("LCD_BACKLIGHT->down brightness\n"); 
	   	set_led_backlight(50);
	   	//write_int(&leds[LCD_BACKLIGHT].brightness, 50);
	  	 abs_tm = 0;
           }
	}
#endif
        k_tm = time(NULL);  
        //D("[%ld][%ld][%d][%d %d %d][%d]\n", tm, keys_tm, ev.type, ev.code, ev.value, g_buttons, g_backlight);	
	if (g_buttons==1) {
          if (k_tm == keys_tm || g_backlight == 0){
            switch_led_button(0);
            sleep(1);
          }
	}
//=========================
      nanosleep(&t,NULL); //avoid 100% CPU usage by system_server
    }

    ev_exit();
    events_ct = 0; 
    return 0;
}

void start_events_thread() {
   if (events_ct == 0) //ensure only 1 thread 
	pthread_create(&events_ct, NULL, events_cthread, NULL);
   return;
}
//=================================================================================================
static int
set_light_backlight(struct light_device_t* dev,
        struct light_state_t const* state) {
    int err = 0;
    int brightness = rgb_to_brightness(state);
    LOGV("%s brightness=%d color=0x%08x",
            __func__,brightness, state->color);
    pthread_mutex_lock(&g_lock);
    set_led_backlight(brightness);
    pthread_mutex_unlock(&g_lock);
    return err;
}

static int
set_light_buttons(struct light_device_t* dev,
        struct light_state_t const* state) {
    int err = 0;
    int on = is_lit(state);
    pthread_mutex_lock(&g_lock);
    LOGV("%s mode=%d color=0x%08x",
            __func__,state->flashMode, state->color);
    switch_led_button(on);
    pthread_mutex_unlock(&g_lock);
    return err;
}

static int
set_speaker_light_locked(struct light_device_t* dev,
        struct light_state_t const* state) {
    int len;
    unsigned int colorRGB;
    
    colorRGB = state->color & 0xFFFFFF;

    D("@@ %s colorRGB=%08X, state->flashMode:%d\n", __func__, colorRGB, state->flashMode);

    /*if (colorRGB ==0) return 0;*/

    int red = (colorRGB >> 16)&0xFF;
    int green = (colorRGB >> 8)&0xFF;
    int blue  = (colorRGB) & 0xFF;
    int g_blink = 0;

    switch (state->flashMode) {
        case LIGHT_FLASH_HARDWARE:
            g_blink = 3;
            break;
        case LIGHT_FLASH_TIMED:
            g_blink = 1;
            break;
        case LIGHT_FLASH_NONE:
            g_blink = 0;
            break;
        default:
            LOGE("set_led_state colorRGB=%08X, unknown mode %d\n",
                  colorRGB, state->flashMode);
	    break;
    }

    if (red) {
          D("@@ %s AMBER, blink: %d\n", __func__, g_blink);
	  write_int(&leds[GREEN_LED].brightness, 0);
	  write_int(&leds[AMBER_LED].brightness, 1);
	  write_int(&leds[AMBER_LED].blink, g_blink);	
    } else if (green) {
	  D("@@ %s GREEN, blink: %d\n", __func__, g_blink);	
    	  write_int(&leds[AMBER_LED].brightness, 0);
          write_int(&leds[GREEN_LED].brightness, 1);
	  write_int(&leds[GREEN_LED].blink, g_blink);	
    } else {
    	write_int(&leds[GREEN_LED].brightness, 0);
    	write_int(&leds[GREEN_LED].blink, 0);
    	write_int(&leds[AMBER_LED].brightness, 0);
    	write_int(&leds[AMBER_LED].blink, 0);
    }

    return 0;
}

static int
set_light_battery(struct light_device_t* dev,
        struct light_state_t const* state) {
    pthread_mutex_lock(&g_lock);
    LOGV("%s mode=%d color=0x%08x",
            __func__,state->flashMode, state->color);
    set_speaker_light_locked(dev, state);
    pthread_mutex_unlock(&g_lock);
    return 0;
}

static int
set_light_notifications(struct light_device_t* dev,
        struct light_state_t const* state) {
    pthread_mutex_lock(&g_lock);
    LOGV("%s mode=%d color=0x%08x",
            __func__,state->flashMode, state->color);
    pthread_mutex_unlock(&g_lock);
    return 0;
}

static int
set_light_attention(struct light_device_t* dev,
        struct light_state_t const* state) {
    LOGV("%s color=0x%08x mode=0x%08x submode=0x%08x",
            __func__, state->color, state->flashMode, state->flashOnMS);
    pthread_mutex_lock(&g_lock);
/*
/lights_leo(  252): set_light_attention color=0x00ffffff mode=0x00000002 submode=0x00000003
/lights_leo(  252): set_light_attention color=0x00000000 mode=0x00000002 submode=0x00000000
/lights_leo(  252): set_light_attention color=0x00ffffff mode=0x00000002 submode=0x00000007
/lights_leo(  252): set_light_attention color=0x00ffffff mode=0x00000000 submode=0x00000000
*/
#if 0
    if (state->flashMode==2 && state->flashOnMS==7){
    	write_int(&leds[GREEN_LED].brightness, 0);
    	write_int(&leds[AMBER_LED].brightness, 1);
    	write_int(&leds[AMBER_LED].blink, 2);	
    } else {
    	write_int(&leds[AMBER_LED].brightness, 0);
    	write_int(&leds[GREEN_LED].brightness, 1);
    	write_int(&leds[GREEN_LED].blink, 4);	
    }
#endif
    pthread_mutex_unlock(&g_lock);
    return 0;
}

static int
set_light_flashlight(struct light_device_t* dev,
        struct light_state_t const* state) {
    LOGV("%s color=0x%08x mode=0x%08x submode=0x%08x",
            __func__, state->color, state->flashMode, state->flashOnMS);

    return 0;
}

static int
set_light_function(struct light_device_t* dev,
        struct light_state_t const* state) {
    LOGV("%s color=0x%08x mode=0x%08x submode=0x%08x",
            __func__, state->color, state->flashMode, state->flashOnMS);

    return 0;
}

/** Close the lights device */
static int
close_lights(struct light_device_t *dev)
{
    int i;

    for (i = 0; i < NUM_LEDS; ++i) {
        close_prop(&leds[i].brightness);
        close_prop(&leds[i].blink);
    }

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
static int open_lights(const struct hw_module_t* module, char const* name,
        struct hw_device_t** device)
{
    int (*set_light)(struct light_device_t* dev,
            struct light_state_t const* state);

    LOGV("%s name=%s", __func__, name);

    if (0 == strcmp(LIGHT_ID_BACKLIGHT, name)) {
        set_light = set_light_backlight;
    }
    else if (0 == strcmp(LIGHT_ID_BUTTONS, name)) {
        set_light = set_light_buttons;	
        start_events_thread();
    }
    else if (0 == strcmp(LIGHT_ID_BATTERY, name)) {
        set_light = set_light_battery;
#ifdef ENABLE_BATTERY_POLL
        start_battery_thread();
#endif
    }
    else if (0 == strcmp(LIGHT_ID_NOTIFICATIONS, name)) {
        set_light = set_light_notifications;
    }
    else if (0 == strcmp(LIGHT_ID_ATTENTION, name)) {
        set_light = set_light_attention;
    }
    else if (0 == strcmp(LIGHT_ID_FLASHLIGHT, name)) {
        set_light = set_light_flashlight;
    }
    else if (0 == strcmp(LIGHT_ID_FUNC, name)) {
        set_light = set_light_function;
    }
    else {
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
    .name = "htcleo lights Module",
    .author = "Google, Inc.",
    .methods = &lights_module_methods,
};
