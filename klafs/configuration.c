/*
 Author: Alexander Knauer <a-x-e@gmx.net>
 License: Apache 2.0
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libconfig.h>
#include <utlist.h>
#include <limits.h>

#include <digitalSTROM/dsuid.h>
#include <dsvdc/dsvdc.h>

#include "klafs.h"

int read_config() {
  config_t config;
  struct stat statbuf;
  int i;
  char *sval;
  int ivalue;

  if (stat(g_cfgfile, &statbuf) != 0) {
    vdc_report(LOG_ERR, "Could not find configuration file %s\n", g_cfgfile);
    return -1;
  }
  if (!S_ISREG(statbuf.st_mode)) {
    vdc_report(LOG_ERR, "Configuration file \"%s\" is not a regular file", g_cfgfile);
    return -2;
  }

  config_init(&config);
  if (!config_read_file(&config, g_cfgfile)) {
    vdc_report(LOG_ERR, "Error in configuration: l.%d %s\n", config_error_line(&config), config_error_text(&config));
    config_destroy(&config);
    return -3;
  }

  if (config_lookup_string(&config, "vdcdsuid", (const char **) &sval))
    strncpy(g_vdc_dsuid, sval, sizeof(g_vdc_dsuid));
  if (config_lookup_string(&config, "libdsuid", (const char **) &sval))
    strncpy(g_lib_dsuid, sval, sizeof(g_lib_dsuid));
  if (config_lookup_string(&config, "username", (const char **) &sval)) {
    klafs.username = strdup(sval);
  } else {
    vdc_report(LOG_ERR, "mandatory parameter 'username' is not set in klafs.cfg\n");  
    exit(0);
  }  
  if (config_lookup_string(&config, "password", (const char **) &sval)) {
    klafs.password = strdup(sval);
  } else {
    vdc_report(LOG_ERR, "mandatory parameter 'password' is not set in klafs.cfg\n");  
    exit(0);
  }
  if (config_lookup_string(&config, "pin", (const char **) &sval)) {
    klafs.pin = strdup(sval);
  } else {
    vdc_report(LOG_WARNING, "CONFIG WARNING: parameter 'pin' is not set in klafs.cfg. It is required if you want to use power on / off sauna! \n");  
  }
  if (config_lookup_int(&config, "reload_values", (int *) &ivalue))
    g_reload_values = ivalue;
  if (config_lookup_int(&config, "zone_id", (int *) &ivalue))
    g_default_zoneID = ivalue;
  if (config_lookup_int(&config, "debug", (int *) &ivalue)) {
    if (ivalue <= 10) {
      vdc_set_debugLevel(ivalue);
    }
  }
  if (config_lookup_string(&config, "sauna.name", (const char **) &sval))
    klafs.sauna.name = strdup(sval);
  if (config_lookup_string(&config, "sauna.id", (const char **) &sval)) {
    klafs.sauna.id = strdup(sval);
  } else {
    vdc_report(LOG_ERR, "mandatory parameter 'id' in section sauna: is not set in klafs.cfg\n");  
    exit(0);
  }
 
  char path[128];  
  i = 0;
  while(1) {
    sprintf(path, "sensor_values.s%d", i);
    if (i < MAX_SENSOR_VALUES && config_lookup(&config, path)) {
      sensor_value_t* value = &klafs.sauna.sensor_values[i];
      
      sprintf(path, "sensor_values.s%d.value_name", i);
      if (config_lookup_string(&config, path, (const char **) &sval)) {
        value->value_name = strdup(sval);  
      } else {
        value->value_name = strdup("");  
      }
      
      sprintf(path, "sensor_values.s%d.sensor_type", i);
      if (config_lookup_int(&config, path, (int *) &ivalue))
        value->sensor_type = ivalue;  
      
      sprintf(path, "sensor_values.s%d.sensor_usage", i);
      if (config_lookup_int(&config, path, (int *) &ivalue))
        value->sensor_usage = ivalue;  
      
      value->is_active = true;
      
      i++;
    } else {
      while (i < MAX_SENSOR_VALUES) {
        klafs.sauna.sensor_values[i].is_active = false;
        i++;
      }        
      break;
    }
  }

  i = 0;
  while(1) {
    sprintf(path, "binary_values.b%d", i);
    if (i < MAX_BINARY_VALUES && config_lookup(&config, path)) {
      binary_value_t* value = &klafs.sauna.binary_values[i];
      
      sprintf(path, "binary_values.b%d.value_name", i);
      config_lookup_string(&config, path, (const char **) &sval);
      value->value_name = strdup(sval);  
      
      sprintf(path, "binary_values.b%d.sensor_function", i);
      config_lookup_int(&config, path, &ivalue);
      value->sensor_function = ivalue;  
      
      value->is_active = true;
            
      i++;
    } else {
      while (i < MAX_BINARY_VALUES) {
        klafs.sauna.binary_values[i].is_active = false;
        i++;
      }        
      break;
    }
  }
 
  klafs.sauna.configured_scenes = strdup("-");
  i = 0;
  while(1) {
    sprintf(path, "sauna.scenes.s%d", i);
    if (i < MAX_SCENES && config_lookup(&config, path)) {
      scene_t* value = &klafs.sauna.scenes[i];
      
      sprintf(path, "sauna.scenes.s%d.dsId", i);
      config_lookup_int(&config, path, &ivalue);
      value->dsId = ivalue;
      char str[4];
      sprintf(str, "%d", ivalue);
      klafs.sauna.configured_scenes = realloc(klafs.sauna.configured_scenes, strlen(klafs.sauna.configured_scenes)+4);
      strcat(klafs.sauna.configured_scenes, str);
      strcat(klafs.sauna.configured_scenes, "-");
      
      sprintf(path, "sauna.scenes.s%d.isPoweredOn", i);
      if (config_lookup_int(&config, path, &ivalue)) value->isPoweredOn = ivalue;
      
      sprintf(path, "sauna.scenes.s%d.saunaSelected", i);
      if (config_lookup_int(&config, path, &ivalue)) value->saunaSelected = ivalue;
      
      sprintf(path, "sauna.scenes.s%d.sanariumSelected", i);
      if (config_lookup_int(&config, path, &ivalue)) value->sanariumSelected = ivalue;
      
      sprintf(path, "sauna.scenes.s%d.irSelected", i);
      if (config_lookup_int(&config, path, &ivalue)) value->irSelected = ivalue;
      
      sprintf(path, "sauna.scenes.s%d.selectedSaunaTemperature", i);
      if (config_lookup_int(&config, path, &ivalue)) value->selectedSaunaTemperature = ivalue;
      
      sprintf(path, "sauna.scenes.s%d.selectedSanariumTemperature", i);
      if (config_lookup_int(&config, path, &ivalue)) value->selectedSanariumTemperature = ivalue;
      
      sprintf(path, "sauna.scenes.s%d.selectedIrTemperature", i);
      if (config_lookup_int(&config, path, &ivalue)) value->selectedIrTemperature = ivalue;
      
      sprintf(path, "sauna.scenes.s%d.selectedHumLevel", i);
      if (config_lookup_int(&config, path, &ivalue)) value->selectedHumLevel = ivalue;
      
      sprintf(path, "sauna.scenes.s%d.selectedIrLevel", i);
      if (config_lookup_int(&config, path, &ivalue)) value->selectedIrLevel = ivalue; 
      
      sprintf(path, "sauna.scenes.s%d.showBathingHour", i);
      if (config_lookup_int(&config, path, &ivalue)) value->showBathingHour = ivalue;
      
      sprintf(path, "sauna.scenes.s%d.bathingHours", i);
      if (config_lookup_int(&config, path, &ivalue)) value->bathingHours = ivalue; 
      
      sprintf(path, "sauna.scenes.s%d.bathingMinutes", i);
      if (config_lookup_int(&config, path, &ivalue)) value->bathingMinutes = ivalue; 
      
      sprintf(path, "sauna.scenes.s%d.selectedHour", i);
      if (config_lookup_int(&config, path, &ivalue)) value->selectedHour = ivalue; 
      
      sprintf(path, "sauna.scenes.s%d.selectedMinute", i);
      if (config_lookup_int(&config, path, &ivalue)) value->selectedMinute = ivalue; 
    
      i++;
    } else {
      while (i < MAX_SCENES) {
        klafs.sauna.scenes[i].dsId = -1;
        i++;
      }        
      break;
    }
  }

  if (config_lookup_string(&config, "aspxauth", (const char **) &sval)) {
    klafs.aspxauth = strdup(sval);
    klafs_validate_authcookie(klafs.aspxauth);
  } else {
    klafs_login();
  }

  if (g_cfgfile != NULL) {
    config_destroy(&config);
  }

  klafs_sauna_t* sauna = &klafs.sauna;
  if (sauna->id) {
    char buffer[128];
    strcpy(buffer, sauna->id);
    
    sauna_device = malloc(sizeof(klafs_vdcd_t));
    if (!sauna_device) {
      return KLAFS_OUT_OF_MEMORY;
    }
    memset(sauna_device, 0, sizeof(klafs_vdcd_t));

    sauna_device->announced = false;
    sauna_device->present = true; 
    sauna_device->sauna = sauna;

    dsuid_generate_v3_from_namespace(DSUID_NS_IEEE_MAC, buffer, &sauna_device->dsuid);
    dsuid_to_string(&sauna_device->dsuid, sauna_device->dsuidstring);
  }

	return 0;
}

int write_config() {
  config_t config;
  config_setting_t* cfg_root;
  config_setting_t* setting;
  config_setting_t* saunasetting;
  int i;

  config_init(&config);
  cfg_root = config_root_setting(&config);

  setting = config_setting_add(cfg_root, "vdcdsuid", CONFIG_TYPE_STRING);
  if (setting == NULL) {
    setting = config_setting_get_member(cfg_root, "vdcdsuid");
  }
  config_setting_set_string(setting, g_vdc_dsuid);
  
  if (g_lib_dsuid != NULL && strcmp(g_lib_dsuid,"") != 0) { 
    setting = config_setting_add(cfg_root, "libdsuid", CONFIG_TYPE_STRING);
    if (setting == NULL) {
      setting = config_setting_get_member(cfg_root, "libdsuid");
    }  
    config_setting_set_string(setting, g_lib_dsuid);
  }

  setting = config_setting_add(cfg_root, "username", CONFIG_TYPE_STRING);
  if (setting == NULL) {
    setting = config_setting_get_member(cfg_root, "username");
  }
  config_setting_set_string(setting, klafs.username);

  setting = config_setting_add(cfg_root, "password", CONFIG_TYPE_STRING);
  if (setting == NULL) {
    setting = config_setting_get_member(cfg_root, "password");
  }
  config_setting_set_string(setting, klafs.password);
  
  setting = config_setting_add(cfg_root, "pin", CONFIG_TYPE_STRING);
  if (setting == NULL) {
    setting = config_setting_get_member(cfg_root, "pin");
  }
  config_setting_set_string(setting, klafs.pin);

  setting = config_setting_add(cfg_root, "aspxauth", CONFIG_TYPE_STRING);
  if (setting == NULL) {
    setting = config_setting_get_member(cfg_root, "aspxauth");
  }
  config_setting_set_string(setting, klafs.aspxauth);
  
  setting = config_setting_add(cfg_root, "reload_values", CONFIG_TYPE_INT);
  if (setting == NULL) {
    setting = config_setting_get_member(cfg_root, "reload_values");
  }
  config_setting_set_int(setting, g_reload_values);

  setting = config_setting_add(cfg_root, "zone_id", CONFIG_TYPE_INT);
  if (setting == NULL) {
    setting = config_setting_get_member(cfg_root, "zone_id");
  }
  config_setting_set_int(setting, g_default_zoneID);

  setting = config_setting_add(cfg_root, "debug", CONFIG_TYPE_INT);
  if (setting == NULL) {
    setting = config_setting_get_member(cfg_root, "debug");
  }
  config_setting_set_int(setting, vdc_get_debugLevel());

  saunasetting = config_setting_add(cfg_root, "sauna", CONFIG_TYPE_GROUP);
    
  setting = config_setting_add(saunasetting, "id", CONFIG_TYPE_STRING);
  if (setting == NULL) {
    setting = config_setting_get_member(saunasetting, "id");
  }
  config_setting_set_string(setting, klafs.sauna.id);

  setting = config_setting_add(saunasetting, "name", CONFIG_TYPE_STRING);
  if (setting == NULL) {
    setting = config_setting_get_member(saunasetting, "name");
  }
  config_setting_set_string(setting, klafs.sauna.name);
    
  char path[128];
  sprintf(path, "scenes");   
  config_setting_t *scenes_path = config_setting_add(saunasetting, path, CONFIG_TYPE_GROUP);
  
  if (scenes_path == NULL) {
    scenes_path = config_setting_get_member(saunasetting, path);
  }

  i = 0;
  while(1) {
    if (i < MAX_SCENES && klafs.sauna.scenes[i].dsId != -1) {
      scene_t* value = &klafs.sauna.scenes[i];
     
      sprintf(path, "s%d", i);   
      config_setting_t *v = config_setting_add(scenes_path, path, CONFIG_TYPE_GROUP);

      if (v == NULL) {
        v = config_setting_get_member(scenes_path, path);
      }
      
      setting = config_setting_add(v, "dsId", CONFIG_TYPE_INT);
      if (setting == NULL) {
        setting = config_setting_get_member(v, "dsId");
      }
      config_setting_set_int(setting, value->dsId);

      setting = config_setting_add(v, "isPoweredOn", CONFIG_TYPE_INT);
      if (setting == NULL) {
        setting = config_setting_get_member(v, "isPoweredOn");
      }
      config_setting_set_int(setting, value->isPoweredOn);
                  
      if (value->isPoweredOn) {
        setting = config_setting_add(v, "saunaSelected", CONFIG_TYPE_INT);
        if (setting == NULL) {
          setting = config_setting_get_member(v, "saunaSelected");
        }
        config_setting_set_int(setting, value->saunaSelected);
        
        setting = config_setting_add(v, "sanariumSelected", CONFIG_TYPE_INT);
        if (setting == NULL) {
          setting = config_setting_get_member(v, "sanariumSelected");
        }
        config_setting_set_int(setting, value->sanariumSelected);
        
        setting = config_setting_add(v, "irSelected", CONFIG_TYPE_INT);
        if (setting == NULL) {
          setting = config_setting_get_member(v, "irSelected");
        }
        config_setting_set_int(setting, value->irSelected);
        
        setting = config_setting_add(v, "selectedSaunaTemperature", CONFIG_TYPE_INT);
        if (setting == NULL) {
          setting = config_setting_get_member(v, "selectedSaunaTemperature");
        }
        config_setting_set_int(setting, value->selectedSaunaTemperature);
        
        setting = config_setting_add(v, "selectedSanariumTemperature", CONFIG_TYPE_INT);
        if (setting == NULL) {
          setting = config_setting_get_member(v, "selectedSanariumTemperature");
        }
        config_setting_set_int(setting, value->selectedSanariumTemperature);
        
        setting = config_setting_add(v, "selectedIrTemperature", CONFIG_TYPE_INT);
        if (setting == NULL) {
          setting = config_setting_get_member(v, "selectedIrTemperature");
        }
        config_setting_set_int(setting, value->selectedIrTemperature);
        
        setting = config_setting_add(v, "selectedHumLevel", CONFIG_TYPE_INT);
        if (setting == NULL) {
          setting = config_setting_get_member(v, "selectedHumLevel");
        }
        config_setting_set_int(setting, value->selectedHumLevel);
        
        setting = config_setting_add(v, "selectedIrLevel", CONFIG_TYPE_INT);
        if (setting == NULL) {
          setting = config_setting_get_member(v, "selectedIrLevel");
        }
        config_setting_set_int(setting, value->selectedIrLevel);
        
        setting = config_setting_add(v, "showBathingHour", CONFIG_TYPE_INT);
        if (setting == NULL) {
          setting = config_setting_get_member(v, "showBathingHour");
        }
        config_setting_set_int(setting, value->showBathingHour);
        
        setting = config_setting_add(v, "bathingHours", CONFIG_TYPE_INT);
        if (setting == NULL) {
          setting = config_setting_get_member(v, "bathingHours");
        }
        config_setting_set_int(setting, value->bathingHours);
        
        setting = config_setting_add(v, "bathingMinutes", CONFIG_TYPE_INT);
        if (setting == NULL) {
          setting = config_setting_get_member(v, "bathingMinutes");
        }
        config_setting_set_int(setting, value->bathingMinutes);
        
        setting = config_setting_add(v, "selectedHour", CONFIG_TYPE_INT);
        if (setting == NULL) {
          setting = config_setting_get_member(v, "selectedHour");
        }
        config_setting_set_int(setting, value->selectedHour);
        
        setting = config_setting_add(v, "selectedMinute", CONFIG_TYPE_INT);
        if (setting == NULL) {
          setting = config_setting_get_member(v, "selectedMinute");
        }
        config_setting_set_int(setting, value->selectedMinute);
        
        
      }
      i++;
    } else {
      break;
    }
  }

  sprintf(path, "binary_values");   
  config_setting_t *binary_values_path = config_setting_add(cfg_root, path, CONFIG_TYPE_GROUP);
  
  if (binary_values_path == NULL) {
    binary_values_path = config_setting_get_member(setting, path);
  }
  
  i = 0;
  while(1) {
    if (i < MAX_BINARY_VALUES && klafs.sauna.binary_values[i].value_name != NULL) {
      binary_value_t* value = &klafs.sauna.binary_values[i];
      
      sprintf(path, "b%d", i);   
      config_setting_t *v = config_setting_add(binary_values_path, path, CONFIG_TYPE_GROUP);
      
      setting = config_setting_add(v, "value_name", CONFIG_TYPE_STRING);
      if (setting == NULL) {
        setting = config_setting_get_member(v, "value_name");
      }
      config_setting_set_string(setting, value->value_name);
  
      setting = config_setting_add(v, "sensor_function", CONFIG_TYPE_INT);
      if (setting == NULL) {
        setting = config_setting_get_member(v, "sensor_function");
      }
      config_setting_set_int(setting, value->sensor_function);
      
      i++;
    } else {
      break;
    }   
  } 
  
  sprintf(path, "sensor_values");   
  config_setting_t *sensor_values_path = config_setting_add(cfg_root, path, CONFIG_TYPE_GROUP);

  if (sensor_values_path == NULL) {
    sensor_values_path = config_setting_get_member(setting, path);
  }
  
  i = 0;
  while(1) {
    if (i < MAX_SENSOR_VALUES && klafs.sauna.sensor_values[i].value_name != NULL) {
      sensor_value_t* value = &klafs.sauna.sensor_values[i];
      
      sprintf(path, "s%d", i);   
      config_setting_t *v = config_setting_add(sensor_values_path, path, CONFIG_TYPE_GROUP);
      
      setting = config_setting_add(v, "value_name", CONFIG_TYPE_STRING);
      if (setting == NULL) {
        setting = config_setting_get_member(v, "value_name");
      }
      config_setting_set_string(setting, value->value_name);
      
      setting = config_setting_add(v, "sensor_type", CONFIG_TYPE_INT);
      if (setting == NULL) {
        setting = config_setting_get_member(v, "sensor_type");
      }
      config_setting_set_int(setting, value->sensor_type);
      
      setting = config_setting_add(v, "sensor_usage", CONFIG_TYPE_INT);
      if (setting == NULL) {
        setting = config_setting_get_member(v, "sensor_usage");
      }
      config_setting_set_int(setting, value->sensor_usage);
      
      i++;
    } else {
      break;
    }   
  } 

  char tmpfile[PATH_MAX];
  sprintf(tmpfile, "%s.cfg.new", g_cfgfile);

  int ret = config_write_file(&config, tmpfile);
  if (!ret) {
    vdc_report(LOG_ERR, "Error while writing new configuration file %s\n", tmpfile);
    unlink(tmpfile);
  } else {
    rename(tmpfile, g_cfgfile);
  }

  config_destroy(&config);

  return 0;
}

sensor_value_t* find_sensor_value_by_name(char *key) {
  sensor_value_t* value;
  for (int i = 0; i < MAX_SENSOR_VALUES; i++) {
    value = &klafs.sauna.sensor_values[i];
    if (value->value_name != NULL && strcasecmp(key, value->value_name) == 0) {
        return value;
    }
  }
  
  return NULL;
}

binary_value_t* find_binary_value_by_name(char *key) {
  binary_value_t* value;
  for (int i = 0; i < MAX_BINARY_VALUES; i++) {
    value = &klafs.sauna.binary_values[i];
    if (value->value_name != NULL && strcasecmp(key, value->value_name) == 0) {
      return value;
    }
  }
  
  return NULL;  
}

void save_scene(int scene) {
  int i = 0;
  scene_t* value = NULL;
  while(1) {
    if (i < MAX_SCENES && klafs.sauna.scenes[i].dsId != -1) {
      //scene is already configured, so we will reconfigure the existing one
      if (klafs.sauna.scenes[i].dsId == scene) {
        value = &klafs.sauna.scenes[i];
        break;
      }
    } else if (i < MAX_SCENES)  {
      //scene is currently not configured and we have less than MAX_SCENES configured in config file, so we add a new scene config
      value = &klafs.sauna.scenes[i];
      break;
    } else {
      //scene is not already configured in config file, but we have already MAX_SCENES configured in config file, so we ignore the save scene request
      break;
    }
    
    i++;
  }
  
  if (value != NULL) {
    value->dsId = scene;
    value->isPoweredOn = sauna_current_values->isPoweredOn;
    value->saunaSelected = sauna_current_values->saunaSelected;
    value->sanariumSelected = sauna_current_values->sanariumSelected;
    value->irSelected = sauna_current_values->irSelected;
    value->selectedSaunaTemperature = sauna_current_values->selectedSaunaTemperature;
    value->selectedSanariumTemperature = sauna_current_values->selectedSanariumTemperature;
    value->selectedIrTemperature = sauna_current_values->selectedIrTemperature;
    value->selectedHumLevel = sauna_current_values->selectedHumLevel;
    value->selectedIrLevel = sauna_current_values->selectedIrLevel;
    value->bathingHours = sauna_current_values->bathingHours;
    value->bathingMinutes = sauna_current_values->bathingMinutes;
  }
}
