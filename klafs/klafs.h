/*
 Author: Alexander Knauer <a-x-e@gmx.net>
 License: Apache 2.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <syslog.h>

#include <digitalSTROM/dsuid.h>
#include <dsvdc/dsvdc.h>

#define MAX_SENSOR_VALUES 15
#define MAX_BINARY_VALUES 15
#define MAX_SCENES 128

typedef struct scene {
  int dsId;
  bool isPoweredOn;
  bool isReadyForUse;
  bool isConnected;
  int currentTemperature;
  bool saunaSelected;
  bool sanariumSelected;
  bool irSelected;
  int selectedSaunaTemperature;
  int selectedSanariumTemperature;
  int selectedIrTemperature;
  int selectedHumLevel;
  int selectedIrLevel;
  bool showBathingHour;
  int selectedHour;
  int selectedMinute;
  int bathingHours;
  int bathingMinutes;
} scene_t;

typedef struct sensor_value {
  bool is_active;
  char *value_name;
  int sensor_type;
  int sensor_usage;
  double value;
  double last_value;
  time_t last_query;
  time_t last_reported;
} sensor_value_t;

typedef struct binary_value {
  bool is_active;
  char *value_name;
  int sensor_function;
  bool value;
  bool last_value;
  time_t last_query;
  time_t last_reported;
} binary_value_t;

typedef struct klafs_sauna {
  dsuid_t dsuid;
  char *id;
  char *name;
  char *configured_scenes;
  binary_value_t binary_values[MAX_BINARY_VALUES];
  sensor_value_t sensor_values[MAX_SENSOR_VALUES];
  scene_t scenes[MAX_SCENES];
  uint16_t zoneID;
} klafs_sauna_t;

typedef struct klafs_data {
  char *username;
  char *password;
  char *pin;
  char *aspxauth;
  char *verificationtoken;
  klafs_sauna_t sauna;
} klafs_data_t;

typedef struct klafs_vdcd {
  struct klafs_vdcd* next;
  dsuid_t dsuid;
  char dsuidstring[36];
  bool announced;
  bool presentSignaled;
  bool present;
  klafs_sauna_t* sauna;
} klafs_vdcd_t;

#define KLAFS_OK 0
#define KLAFS_OUT_OF_MEMORY -1
#define KLAFS_AUTH_FAILED -10
#define KLAFS_BAD_CONFIG -12
#define KLAFS_CONNECT_FAILED -13
#define KLAFS_GETMEASURE_FAILED -14
#define KLAFS_CONFIGCHANGE_FAILED -15
#define KLAFS_GETREQVERIFYTOKEN_FAILED -16

extern const char *g_cfgfile;
extern int g_shutdown_flag;
extern klafs_data_t klafs;
extern klafs_vdcd_t* sauna_device;
extern pthread_mutex_t g_network_mutex;
extern scene_t* sauna_current_values;

extern char g_vdc_modeluid[33];
extern char g_vdc_dsuid[35];
extern char g_lib_dsuid[35];

extern time_t g_reload_values;
extern int g_default_zoneID;

extern void vdc_new_session_cb(dsvdc_t *handle __attribute__((unused)), void *userdata);
extern void vdc_ping_cb(dsvdc_t *handle __attribute__((unused)), const char *dsuid, void *userdata __attribute__((unused)));
extern void vdc_announce_device_cb(dsvdc_t *handle __attribute__((unused)), int code, void *arg, void *userdata __attribute__((unused)));
extern void vdc_announce_container_cb(dsvdc_t *handle __attribute__((unused)), int code, void *arg, void *userdata __attribute__((unused)));
extern void vdc_end_session_cb(dsvdc_t *handle __attribute__((unused)), void *userdata);
extern bool vdc_remove_cb(dsvdc_t *handle __attribute__((unused)), const char *dsuid, void *userdata);
extern void vdc_blink_cb(dsvdc_t *handle __attribute__((unused)), char **dsuid, size_t n_dsuid, int32_t *group, int32_t *zone_id, void *userdata);
extern void vdc_getprop_cb(dsvdc_t *handle, const char *dsuid, dsvdc_property_t *property, const dsvdc_property_t *query, void *userdata);
extern void vdc_setprop_cb(dsvdc_t *handle, const char *dsuid, dsvdc_property_t *property, const dsvdc_property_t *properties, void *userdata);
extern void vdc_callscene_cb(dsvdc_t *handle __attribute__((unused)), char **dsuid, size_t n_dsuid, int32_t scene, bool force, int32_t *group, int32_t *zone_id, void *userdata);
extern void vdc_savescene_cb(dsvdc_t *handle __attribute__((unused)), char **dsuid, size_t n_dsuid, int32_t scene, int32_t *group, int32_t *zone_id, void *userdata);
extern void vdc_request_generic_cb(dsvdc_t *handle __attribute__((unused)), char *dsuid, char *method_name, dsvdc_property_t *property, const dsvdc_property_t *properties,  void *userdata);

int klafs_login();
void klafs_validate_authcookie(char *aspxauth);
int klafs_get_values();
int klafs_power_off();
int klafs_power_on();
int klafs_change_favoriteprogram(scene_t *scene_data);
int klafs_change_mode(scene_t *scene_data);
int klafs_change_temperature(scene_t *scene_data);
int klafs_change_humidity(scene_t *scene_data);
void push_binary_input_states();
void push_sensor_data();
void push_device_states();
bool is_scene_configured();
scene_t* get_program_configuration(bool saunaSelected, bool sanariumSelected, bool irSelected, int selectedSaunaTemperature, int selectedSanariumTemperature, int selectedIrTemperature, int selectedHumLevel, int selectedIrLevel, int bathingHours, int bathingMinutes, int selectedHour, int selectedMinute);
scene_t* get_scene_configuration(int scene);
int decodeURIComponent (char *sSource, char *sDest);
sensor_value_t* find_sensor_value_by_name(char *key);
binary_value_t* find_binary_value_by_name(char *key);
void save_scene(int scene);

int write_config();
int read_config();

void vdc_init_report();
void vdc_set_debugLevel(int debug);
int vdc_get_debugLevel();
void vdc_report(int errlevel, const char *fmt, ... );
void vdc_report_extraLevel(int errlevel, int maxErrlevel, const char *fmt, ... );
