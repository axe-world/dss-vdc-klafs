/*
 Author: Alexander Knauer <a-x-e@gmx.net>
 License: Apache 2.0
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>

#include <libconfig.h>
#include <curl/curl.h>
#include <json.h>
#include <utlist.h>

#include <digitalSTROM/dsuid.h>
#include <dsvdc/dsvdc.h>

#include "klafs.h"

/* vdSD data */

const char *g_cfgfile = "klafs.cfg";
const char *version = "0.0.1";
int g_shutdown_flag = 0;
klafs_data_t klafs;
klafs_vdcd_t* sauna_device = NULL;
scene_t* sauna_current_values = NULL;

/* VDC-API data */

char g_vdc_modeluid[33] = { 0, };
char g_vdc_dsuid[35] = { 0, };
char g_lib_dsuid[35] = { 0, };


/* Klafs Data */

time_t g_reload_values = 1 * 60;
int g_default_zoneID = 65534;

static time_t g_query_values_time = 0;
static bool g_network_changes = false;
pthread_mutex_t g_network_mutex;

dsvdc_t *handle = NULL;

#if defined(HAVE_GETOPT_H) && defined(HAVE_GETOPT_LONG)
#include <getopt.h>
#define OPTSTR "c:d:h"
#else
#error Need getopt_long!
#endif

void print_copyright() {
}

void print_usage() {
}

void signal_handler(int signum) {
  if ((signum == SIGINT) || (signum == SIGTERM)) {
    g_shutdown_flag++;
  }
}

void* networkThread(void *arg __attribute__((unused))) {
  int rc;
  static time_t last = 0;

  while (!g_shutdown_flag) {
    sleep(5);
    time_t now = time(NULL);
  
    if (now >= last + 10) {
      vdc_report(LOG_DEBUG, "Network Thread: time %ld, last time %ld, queryValuesTime %d\n", now, last, g_query_values_time);

      if ( (g_query_values_time <= now)) {        
        rc = klafs_get_values();
        if (rc == 0) {                 //getting values from KLAFS API succeeded and some values have changed compared to previous get values
          g_query_values_time = g_reload_values + now;
          g_network_changes = true;                  // send to upstream DSS
          vdc_report(LOG_DEBUG, "changed values detected - sending to DSS\n");
        } else if (rc == 1) {         //getting values from KLAFS API succeeded but no values have changed compared to previous get values
          g_query_values_time = g_reload_values + now;
          g_network_changes = false;                 // no send to upstream DSS  
          vdc_report(LOG_DEBUG, "sauna values did not change - not sending to DSS\n");
        } else {                                     //getting values from KLAFS API failed - retry in one minute
          now = time(NULL);
          g_query_values_time = 60 + now;
          g_network_changes = false;                 // no send to upstream DSS  
          dsvdc_send_pong(handle, sauna_device->dsuidstring);
        }
      }
      last = now;
    }
  }

  return NULL;
}

void announce_device() {
  vdc_report(LOG_INFO, "Announcing device %p: %s...\n", sauna_device, sauna_device->dsuidstring);
  int ret = dsvdc_announce_device(handle,
                            g_vdc_dsuid,
                            sauna_device->dsuidstring,
                            (void *) NULL,
                            vdc_announce_device_cb);
  vdc_report(LOG_DEBUG, "Announce device return code: %d\n", ret);      
  if (ret == DSVDC_OK) {
    sauna_device->announced = true;
  }
}

void push_sensor_data() {
  dsvdc_property_t* pushEnvelope;
  dsvdc_property_t* propState;  
  dsvdc_property_t* propDevState;  
  dsvdc_property_t* prop;

  dsvdc_property_new (&pushEnvelope);
  dsvdc_property_new (&propState);
  dsvdc_property_new (&propDevState);
  
  int i = 0;
  while (1) {
    if (sauna_device->sauna->sensor_values[i].is_active) {
      double val = sauna_device->sauna->sensor_values[i].value;
      time_t now = time (NULL);

      if (dsvdc_property_new (&prop) != DSVDC_OK) {
        vdc_report(LOG_ERR, "create new property failed!");
        continue;
      }
      dsvdc_property_add_double (prop, "value", val);
      dsvdc_property_add_int (prop, "age", now - sauna_device->sauna->sensor_values[i].last_query);
      dsvdc_property_add_int (prop, "error", 0);

      char sensorIndex[64];
      snprintf (sensorIndex, 64, "%d", i);
      dsvdc_property_add_property (propState, sensorIndex, &prop);

      sauna_device->sauna->sensor_values[i].last_reported = now;
      
      i++;
    } else {
      break;
    }
  }
  
  if (dsvdc_property_new (&prop) != DSVDC_OK) {
    vdc_report(LOG_ERR, "create new property failed!");
  }
  dsvdc_property_add_string (prop, "name", "SaunaConnected");
  dsvdc_property_add_string (prop, "value", "1");
  dsvdc_property_add_property (propDevState, 0, &prop);

  dsvdc_property_add_property (pushEnvelope, "sensorStates", &propState);
  dsvdc_property_add_property (pushEnvelope, "deviceStates", &propDevState);
  dsvdc_push_property (handle, sauna_device->dsuidstring, pushEnvelope);
  dsvdc_property_free (pushEnvelope);  
}

void push_binary_input_states() {
  dsvdc_property_t* pushEnvelope;
  dsvdc_property_t* propState;
  dsvdc_property_t* prop;

  dsvdc_property_new (&pushEnvelope);
  dsvdc_property_new (&propState);
 
  int i = 0;
  while(1) {
    if (sauna_device->sauna->binary_values[i].is_active) {
      bool val = sauna_device->sauna->binary_values[i].value;
      time_t now = time (NULL);

      if (dsvdc_property_new (&prop) != DSVDC_OK) {
        vdc_report(LOG_ERR, "create new property failed!");
        continue;
      }

      dsvdc_property_add_bool (prop, "value", sauna_device->sauna->binary_values[i].value);
      dsvdc_property_add_int (prop, "age", now - sauna_device->sauna->binary_values[i].last_query);
      dsvdc_property_add_int (prop, "error", 0);

      char sensorIndex[64];
      snprintf (sensorIndex, 64, "%d", i);
      dsvdc_property_add_property (propState, sensorIndex, &prop);

      sauna_device->sauna->binary_values[i].last_reported = now;
      
      i++;
    } else {
      break;
    }
  }

  dsvdc_property_add_property (pushEnvelope, "binaryInputStates", &propState);
  dsvdc_push_property (handle, sauna_device->dsuidstring, pushEnvelope);
  dsvdc_property_free (pushEnvelope);     
}

int main(int argc __attribute__((unused)), char **argv __attribute__((unused))) {
  struct sigaction action;
  pthread_t networkThreadId;

  int o, opt_index;
  bool ready = false;

  static struct option long_options[] =
    {
        {"cfgfile",     1, 0, 'c'},
        {"debuglevel",  1, 0, 'd'},
        {"help",        0, 0, 'h'},
        {0, 0, 0, 0}
    };

  vdc_init_report();
  while (1) {
    o = getopt_long(argc, argv, OPTSTR, long_options, &opt_index);
    if (o == -1) break;

    switch(o) {
      case 'c':
        g_cfgfile = optarg;
        break;
      case 'd':
        vdc_set_debugLevel(atoi(optarg));
        break;
      case 'v':
        print_copyright();
        exit(EXIT_SUCCESS);
      default:
        print_usage();
        exit(EXIT_FAILURE);
    }
  }

  memset(&action, 0, sizeof(action));
  action.sa_handler = signal_handler;
  action.sa_flags = 0;
  sigfillset(&action.sa_mask);

  if (sigaction(SIGINT, &action, NULL) < 0) {
    vdc_report(LOG_ERR, "Could not register SIGINT handler!\n");
    return EXIT_FAILURE;
  }

  if (sigaction(SIGTERM, &action, NULL) < 0) {
    vdc_report(LOG_ERR, "Could not register SIGTERM handler!\n");
    return EXIT_FAILURE;
  }

  curl_global_init(CURL_GLOBAL_ALL);

  memset(&klafs, 0, sizeof(klafs_data_t));
  int rc = read_config();
  if (rc < -1) {
    vdc_report(LOG_ERR, "Could not read configuration data!\n");
    exit(0);
  } else if (rc == -1) {
    vdc_report(LOG_ERR, "Could not read configuration data! Writing a new one. Just a template, you MUST change it according to your requirements!\n");
    write_config();
    exit(0);
  }

  char buffer[128] = { 0, };
  strncpy(buffer, klafs.sauna.id, 64);

  /* generate a dsuid v1 for the vdc */
  dsuid_t gdsuid;
  if (g_vdc_dsuid[0] == 0) {
    dsuid_generate_v1(&gdsuid);
    dsuid_to_string(&gdsuid, g_vdc_dsuid);
    vdc_report(LOG_INFO, "Generated VDC DSUID: %s\n", g_vdc_dsuid);
  }
  
  if (g_lib_dsuid[0] == 0) {
    dsuid_generate_v1(&gdsuid);
    dsuid_to_string(&gdsuid, g_lib_dsuid);
    vdc_report(LOG_INFO, "Generated LIB DSUID: %s\n", g_lib_dsuid);
  }

  /* store configuration data, including the Klafs device setup and the VDC DSUID */
  if (write_config() < 0) {
    vdc_report(LOG_ERR, "Could not write configuration data!\n");
  }

   sauna_current_values = malloc(sizeof(scene_t));
   if (!sauna_current_values) {
    return KLAFS_OUT_OF_MEMORY;
   }
   memset(sauna_current_values, 0, sizeof(scene_t));
    
  /* initialize new library instance */
  char hostname[HOST_NAME_MAX];
  gethostname(hostname, HOST_NAME_MAX);
  char servicename[HOST_NAME_MAX + 32];
  strcpy(servicename, "Klafs Sauna Controller @");
  strcat(servicename, hostname);

  
  if (dsvdc_new(0, g_lib_dsuid, servicename, false, &ready, &handle) != DSVDC_OK) {
    vdc_report(LOG_ERR, "dsvdc_new() initialization failed\n");
    return EXIT_FAILURE;
  }

  /* connection callbacks */
  dsvdc_set_ping_callback(handle, vdc_ping_cb);
  dsvdc_set_remove_callback(handle, vdc_remove_cb);

  /* setup callbacks */
  dsvdc_set_new_session_callback(handle, vdc_new_session_cb);
  dsvdc_set_end_session_callback(handle, vdc_end_session_cb);
  dsvdc_set_get_property_callback(handle, vdc_getprop_cb);
  dsvdc_set_set_property_callback(handle, vdc_setprop_cb);
  dsvdc_set_call_scene_notification_callback(handle, vdc_callscene_cb);
  dsvdc_set_save_scene_notification_callback(handle, vdc_savescene_cb);
  dsvdc_set_send_request_generic_request(handle, vdc_request_generic_cb);

  /* delegate network access on a separate thread */
  /* avoid to block the dsvdc main loop and vdsm query timeouts */
  pthread_mutexattr_t mta;
  pthread_mutexattr_init(&mta);
  pthread_mutexattr_settype(&mta, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&g_network_mutex, &mta);
  if (pthread_create(&networkThreadId, NULL, &networkThread, 0) != 0) {
    vdc_report(LOG_ERR, "Network thread initialization failed\n");
    return EXIT_FAILURE;
  }

  while (!g_shutdown_flag) {
    /* let the work function do our timing, 2secs timeout */
    dsvdc_work(handle, 2);

    /* do not block here if network thread currently pulls new values,
     * push properties can wait and sent later if lock can be taken
     */
    if (pthread_mutex_trylock(&g_network_mutex) != 0) {
      continue;
    }

    if (!dsvdc_has_session (handle)) {
      sauna_device->announced = false;
   
      pthread_mutex_unlock(&g_network_mutex);
      continue;
    }

    if (!sauna_device->announced) {
      announce_device();
      continue;
    }

    if (!sauna_device->present) {
      if(sauna_device->presentSignaled) {
        dsvdc_device_vanished(handle, sauna_device->dsuidstring);
        sauna_device->presentSignaled = false;
        continue;
      }
    } else {
      if (!sauna_device->presentSignaled) {
        dsvdc_identify_device(handle, sauna_device->dsuidstring);
        sauna_device->presentSignaled = true;
        continue;
      } 
    }

    // new data from the network?
    if (g_network_changes) {
      g_network_changes = false;

      vdc_report(LOG_DEBUG, "Main loop: sauna_device %p: - dsuid %s - presentSignaled %s, announced %s\n",
            sauna_device, sauna_device->dsuidstring,
            sauna_device->presentSignaled ? "yes" : "no",
            sauna_device->announced? "yes" : "no"); 

      vdc_report(LOG_INFO, "Reporting new values from device %p: %s...\n", sauna_device, sauna_device->dsuidstring);

      push_sensor_data();
      push_binary_input_states(); 
    }

    pthread_mutex_unlock(&g_network_mutex);
  }
  
  for (int i = 0; i < MAX_SENSOR_VALUES; i++) {
    sensor_value_t* value = &klafs.sauna.sensor_values[i];    
    free(value->value_name);    
  } 
  
  for (int i = 0; i < MAX_BINARY_VALUES; i++) {
    binary_value_t* value = &klafs.sauna.binary_values[i];    
    free(value->value_name);    
  } 
 
  free(sauna_current_values);
  free(klafs.aspxauth);
  free(klafs.verificationtoken);
  
  dsvdc_cleanup(handle);
  curl_global_cleanup();
  pthread_join(networkThreadId, NULL);
  pthread_mutex_destroy(&g_network_mutex);

  return EXIT_SUCCESS;
}
