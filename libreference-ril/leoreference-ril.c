/* //device/system/reference-ril/reference-ril.c
**
** Copyright 2006, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#include <telephony/ril.h>
#include <dlfcn.h>

#include <utils/Log.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <alloca.h>

#include "misc.h"
#include <getopt.h>
#include <sys/socket.h>
#include <cutils/sockets.h>
#include <termios.h>
#include <utils/Log.h>

#define LOG_TAG "RILW"

#define RIL_DEBUG  1

#if RIL_DEBUG
#  define  D(...)   LOGD(__VA_ARGS__)
#else
#  define  D(...)   ((void)0)
#endif

#define RIL_onRequestComplete(t, e, response, responselen) s_rilenv->OnRequestComplete(t,e, response, responselen)

#define PPP_TTY_PATH "/dev/ppp0"

#define AT_ERROR_GENERIC -1
#define AT_ERROR_COMMAND_PENDING -2
#define AT_ERROR_CHANNEL_CLOSED -3
#define AT_ERROR_TIMEOUT -4
#define AT_ERROR_INVALID_THREAD -5 /* AT commands may not be issued from
                                       reader thread (or unsolicited response
                                       callback */
#define AT_ERROR_INVALID_RESPONSE -6 /* eg an at_send_command_singleline that
                                        did not get back an intermediate
                                        response */

void (*libhtc_ril_onRequest)(int request, void *data, size_t datalen, RIL_Token t);

static const char * s_device_path = NULL;
static const struct RIL_Env *s_rilenv;
static void *ril_handler=NULL;

static int s_fd = -1;    /* fd of the AT channel */

static const char * getVersion(void) {
    return "android leo-reference-ril 1.0";
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++=
void  AT_DUMP(const char*  prefix, const char*  buff, int  len) {
    if (len < 0)
        len = strlen(buff);
    D("%.*s", len, buff);
}
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++=
static int at_writeline (const char *s) {
    size_t cur = 0;
    size_t len = strlen(s);
    ssize_t written;

    if (s_fd < 0 ) {
        return AT_ERROR_CHANNEL_CLOSED;
    }

    AT_DUMP( ">> ", s, strlen(s) );

    /* the main string */
    while (cur < len) {
        do {
            written = write (s_fd, s + cur, len - cur);
        } while (written < 0 && errno == EINTR);

        if (written < 0) {
            return AT_ERROR_GENERIC;
        }

        cur += written;
    }

    /* the \r  */

    do {
        written = write (s_fd, "\r" , 1);
    } while ((written < 0 && errno == EINTR) || (written == 0));

    if (written < 0) {
        return AT_ERROR_GENERIC;
    }

    //D("AT> %s", "sent");

    return 0;
}
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++=
static int at_command(char *cmd, int to)
{
    fd_set rfds;
    struct timeval timeout;

    char buf[1024];
    int sel, len, i, err;

    err=at_writeline(cmd);
    if (err != 0 ) {
       goto error;
    }

    //read always failed, not sure why ?
    for (i = 0; i < 100; i++) {
                FD_ZERO(&rfds);
                FD_SET(s_fd, &rfds);
                timeout.tv_sec = 0;
                timeout.tv_usec = to;
                if ((sel = select(s_fd + 1, &rfds, NULL, NULL, &timeout)) > 0) {
                        if (FD_ISSET(s_fd, &rfds)) {
                                memset(buf, 0, sizeof(buf));
                                len = read(s_fd, buf, sizeof(buf));
                                if (len> 0) {
                                	D("%d: %s", len, buf);
                                	if (strstr(buf, "\r\nOK") != NULL){
						D("  > OK");
                                        	break;
                                	}
                                	if (strstr(buf, "\r\nERROR") != NULL){
						D("  > ERROR");
                                        	break;
                                	}
                                	if (strstr(buf, "\r\nCONNECT") != NULL){
						D("  > CONNECT");
                                        	break;
                                	}
                                }
                        }

                } else {
		  //LOGE("select %s: %s (%d)", s_device_path, strerror(errno), errno);
                }
    }

   return 0;

error:
   return -1;
}
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++=
static int open_modem() {
        struct termios ios;
        int fd;
	LOGI("Opening tty device %s", s_device_path);
//
//       fd = open(s_device_path, O_RDWR | O_NONBLOCK);
	fd = open(s_device_path, O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd < 0) {
                LOGE("Can't open %s: %s (%d)", s_device_path, strerror(errno), errno);
                return -1;
        }

        tcflush(fd, TCIOFLUSH);
#if 0
        /* Switch tty to RAW mode */
  	tcgetattr( fd, &ios );
        /* cfmakeraw(&ios); */
  	ios.c_lflag = 0;  /* disable ECHO, ICANON, etc... */
        tcsetattr(fd, TCSANOW, &ios);
#endif

        /* Switch tty to RAW mode */
        cfmakeraw(&ios);
        tcsetattr(fd, TCSANOW, &ios);

        s_fd = fd;

        return fd;
}

static int close_modem() {
       if (s_fd >= 0) {
	     close(s_fd);
       }
       LOGI("Closing tty device %s", s_device_path);
       s_fd = -1;
       return 0;
}
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++=
#if 0
int open_modem () {
  int fd;
  int ret;
  struct termios  ios;

  fd = open (s_device_path, O_RDWR);
  if(fd == -1)  {
    LOGE("Error opening %s", s_device_path);
    goto error;
  }

  /* disable echo on serial ports */
  tcgetattr( fd, &ios );
  ios.c_lflag = 0;  /* disable ECHO, ICANON, etc... */
  tcsetattr( fd, TCSANOW, &ios );

  s_fd = fd;

 return 0;

 error:
 return -1;
}
#endif
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++=
static void requestRegistrationState(int request, void *data,
                                        size_t datalen, RIL_Token t) {

  D("%s", __func__);
  if (request == RIL_REQUEST_REGISTRATION_STATE) {
     D("### RIL_REQUEST_REGISTRATION_STATE");
  } else if (request == RIL_REQUEST_GPRS_REGISTRATION_STATE) {
     D("### RIL_REQUEST_GPRS_REGISTRATION_STATE");
  }
  libhtc_ril_onRequest(request, data, datalen, t);

  D("%s:%.*s", __func__ , datalen, (char *)data);
  return;
}
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++=
void requestDeactivateDataCall(void *data, size_t datalen, RIL_Token t)
{
  int ret, err;
  char * cmd;
  char * cid;
  int fd;

  open_modem();

  cid = ((char **)data)[0];

  D("%s, cid: %s", __func__, cid);

  asprintf(&cmd, "AT+CGACT=0,%s", cid);
  err = at_command(cmd, 10000);
  free(cmd);

  int i=0;
  i=0;
  while((fd = open("/etc/ppp/ppp0.pid",O_RDONLY)) > 0) {
	if(i%5 == 0) system("killall pppd");
	close(fd);
	if(i>25) goto error;
	i++;
	sleep(1);
  }

  close_modem();

  D("%s: RIL_E_SUCCESS ", __func__);
  RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
  return;

 error:
  close_modem();
  LOGE("%s: GENERIC_FAILURE ", __func__);
  RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
}
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++=
static void requestSetupDataCall(char **data, size_t datalen, RIL_Token t) {
  const char *apn;
  char *user = NULL;
  char *pass = NULL;
  char *cmd;
  char *userpass;
  int err;
  int fd;
  FILE *cfg;
  char *buffer;
  long buffSize, len;
  char *response[2] = { "1", "ppp0" };

  int status;
  int ret;

  D("%s", __func__);

  /* check if /dev/ppp exists else pppd will be fail*/
  int fd_ppp = open ("/dev/ppp", O_RDWR);
  if (fd_ppp == -1)  {
    system("mknod /dev/ppp c 108 0");
    fd_ppp = open ("/dev/ppp", O_RDWR);
    if (fd_ppp == -1) LOGE("Error opening /dev/ppp");
  }

  open_modem();
 
  apn = ((const char **)data)[2];
  user = ((char **)data)[3];
  pass = ((char **)data)[4];

  D("requesting data connection to APN '%s'", apn);

  /*at_command("ATZ", 10000);*/

  //D("Setting up PDP Context");
  asprintf(&cmd, "AT+CGDCONT=1,\"IP\",\"%s\",,0,0", apn);
  //FIXME check for error here
  err = at_command(cmd, 10000);
  free(cmd);

  // Set required QoS params to default
  err = at_command("AT+CGQREQ=1", 10000);

  // Set minimum QoS params to default
  err = at_command("AT+CGQMIN=1", 10000);

  // packet-domain event reporting
  err = at_command("AT+CGEREP=1,0", 10000);

  //D("Activating PDP Context");
  // Hangup anything that's happening there now
  err = at_command("AT+CGACT=1,0", 10000);

  //D("Start data on PDP context 1");
  err = at_command("ATD*99***1#", 10000);

  //err = at_command("ATD*99#", 1);
 
  asprintf(&userpass, "%s * %s", user, pass);
  len = strlen(userpass);
  fd = open("/etc/ppp/pap-secrets",O_WRONLY);
  if(fd < 0) {
    D("could not open /etc/ppp/pap-secrets");
    goto error;
  }
  write(fd, userpass, len);
  close(fd);

  fd = open("/etc/ppp/chap-secrets",O_WRONLY);
  if(fd < 0) {
    D("could not open /etc/ppp/chap-secrets");
    goto error;
  }
  write(fd, userpass, len);
  close(fd);
  free(userpass);

  cfg = fopen("/etc/ppp/options.smd","r");
  if(!cfg) {
    D("could not open /etc/ppp/options.smd");
    goto error;
  }

  //filesize
  fseek(cfg, 0, SEEK_END);
  buffSize = ftell(cfg);
  rewind(cfg);

  //allocate memory
  buffer = (char *) malloc (sizeof(char)*buffSize);
  if (buffer == NULL) {
    goto error;
  }

  //read in the original file
  len = fread (buffer,1,buffSize,cfg);
  if (len != buffSize) {
     D("could not set cfg");
    goto error;
  }
  fclose(cfg);

  cfg = fopen("/etc/ppp/options.smd1","w");
  fwrite(buffer,1,buffSize,cfg);
  fprintf(cfg,"user %s",user);
  fclose(cfg);
  free(buffer);

  status = system("/bin/pppd /dev/smd1 debug defaultroute");
  if (status == 0) {
    sleep(5); // allow time for ip-up to run
    /*system("/system/bin/log -t pppd -c1 www.google.com");*/
  } else {
    LOGE("/bin/pppd failed, status: %d", status);
    goto error;
  }

/*
  inaddr_t addr,mask;
  unsigned int flags;
  ifc_init();
  ifc_get_info(PPP_TTY_PATH, &addr, &mask, &flags);
  ifc_close();
*/

  close_modem();
  D("%s: RIL_E_SUCCESS ", __func__);
  RIL_onRequestComplete(t, RIL_E_SUCCESS, response, sizeof(response));
  return;

 error:
  close_modem();
  LOGE("%s: GENERIC_FAILURE ", __func__);
  RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
}
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++=
void onRequest(int request, void *data, size_t datalen, RIL_Token t) {
        switch (request) {
        case RIL_REQUEST_SETUP_DATA_CALL:
            return requestSetupDataCall(data, datalen, t);
        case RIL_REQUEST_DEACTIVATE_DATA_CALL:
            return requestDeactivateDataCall(data, datalen, t);
        case RIL_REQUEST_REGISTRATION_STATE:
        case RIL_REQUEST_GPRS_REGISTRATION_STATE:
            return requestRegistrationState(request, data, datalen, t);           
        default:
	    return libhtc_ril_onRequest(request, data, datalen, t);
	}
}
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++=
static void usage(char *s)
{
#ifdef RIL_SHLIB
    fprintf(stderr, "leo-reference-ril requires: -p <tcp port> or -d /dev/tty_device");
#else
    fprintf(stderr, "usage: %s [-p <tcp port>] [-d /dev/tty_device]", s);
    exit(-1);
#endif
}
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++=
const RIL_RadioFunctions *RIL_Init(const struct RIL_Env *env, int argc, char **argv) {
    int ret;
    int fd = -1;
    int opt;

    s_rilenv = env;

    D("%s", __func__);

    while ( -1 != (opt = getopt(argc, argv, "p:d:s:"))) {
        switch (opt) {
            case 'd':
                s_device_path = optarg;             
            break;
            default:
                usage(argv[0]);
                return NULL;
        }
    }

    if (s_device_path == NULL) {
        usage(argv[0]);
        return NULL;
    }

    ril_handler=dlopen("/system/lib/libhtc_ril.so", 0/*Need to RTFM, 0 seems fine*/);
    RIL_RadioFunctions* (*htc_ril)(const struct RIL_Env *env, int argc, char **argv);

    htc_ril=dlsym(ril_handler, "RIL_Init");
    RIL_RadioFunctions *s_callbacks;
    s_callbacks=htc_ril(env, argc, argv);
    libhtc_ril_onRequest=s_callbacks->onRequest;
    s_callbacks->onRequest=onRequest;

    return s_callbacks;
}
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++=
