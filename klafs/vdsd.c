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
#include <pthread.h>

#include <libconfig.h>
#include <curl/curl.h>
#include <json.h>
#include <utlist.h>

#include <digitalSTROM/dsuid.h>
#include <dsvdc/dsvdc.h>

#include "klafs.h"

#include "incbin.h"
INCBIN_EXTERN(IconStation16);
INCBIN_EXTERN(IconStation48);

void vdc_ping_cb(dsvdc_t *handle __attribute__((unused)), const char *dsuid, void *userdata __attribute__((unused))) {
  int ret;
  vdc_report(LOG_NOTICE, "received ping for dsuid %s\n", dsuid);
  if (strcasecmp(dsuid, g_vdc_dsuid) == 0) {
    ret = dsvdc_send_pong(handle, dsuid);
    vdc_report(LOG_NOTICE, "sent pong for vdc %s / return code %d\n", dsuid, ret);
    return;
  }
  if (strcasecmp(dsuid, g_lib_dsuid) == 0) {
    ret = dsvdc_send_pong(handle, dsuid);
    vdc_report(LOG_NOTICE, "sent pong for lib-dsuid %s / return code %d\n", dsuid, ret);
    return;
  }
  
  if (strcasecmp(dsuid, sauna_device->dsuidstring) == 0) {
      ret = dsvdc_send_pong(handle, sauna_device->dsuidstring);
      vdc_report(LOG_NOTICE, "sent pong for device %s / return code %d\n", dsuid, ret);
    return;
  }
  vdc_report(LOG_WARNING, "ping: no matching dsuid %s registered\n", dsuid);
}

void vdc_announce_device_cb(dsvdc_t *handle __attribute__((unused)), int code, void *arg, void *userdata __attribute__((unused))) {
  vdc_report(LOG_INFO, "announcement of device %s returned code: %d\n", (char *) arg, code);
}

void vdc_announce_container_cb(dsvdc_t *handle __attribute__((unused)), int code, void *arg, void *userdata __attribute__((unused))) {
  vdc_report(LOG_INFO, "announcement of container %s returned code: %d\n", (char *) arg, code);
}

void vdc_new_session_cb(dsvdc_t *handle, void *userdata) {
  (void)userdata;
  int ret;
  ret = dsvdc_announce_container(handle,
                                 g_vdc_dsuid,
                                 (void *) g_vdc_dsuid,
                                 vdc_announce_container_cb);
  if (ret != DSVDC_OK) {
    vdc_report(LOG_WARNING, "dsvdc_announce_container returned error %d\n", ret);
    return;
  }

  vdc_report(LOG_INFO, "new session, container announced successfully\n");
}

void vdc_end_session_cb(dsvdc_t *handle, void *userdata) {
  (void)userdata;
  int ret;

  vdc_report(LOG_WARNING, "end of session\n");
}

bool vdc_remove_cb(dsvdc_t *handle __attribute__((unused)), const char *dsuid, void *userdata) {
  (void)userdata;
  vdc_report(LOG_INFO, "received remove for dsuid %s\n", dsuid);

  // TODO: what to do on remove?

  return true;
}

void vdc_request_generic_cb(dsvdc_t *handle __attribute__((unused)), char *dsuid, char *method_name, dsvdc_property_t *property, const dsvdc_property_t *properties,  void *userdata) {
  int ret;
  uint8_t code = DSVDC_ERR_NOT_IMPLEMENTED;
  size_t i;
  
  vdc_report(LOG_INFO, "received request generic for dsuid %s, method name %s\n", dsuid, method_name);
  
  if (strcasecmp(sauna_device->dsuidstring, dsuid) == 0) {
    for (i = 0; i < dsvdc_property_get_num_properties(properties); i++) {
      char *name;
      ret = dsvdc_property_get_name(properties, i, &name);
      if (ret != DSVDC_OK) {
        vdc_report(LOG_ERR, "request_generic_cb: error getting property name\n");
        code = DSVDC_ERR_MISSING_DATA;
        break;
      }
      if (!name) {
        vdc_report(LOG_ERR, "request_generic_cb: not handling wildcard properties\n");
        code = DSVDC_ERR_NOT_IMPLEMENTED;
        break;
      }

      vdc_report(LOG_INFO, "request generic for name=\"%s\"\n", name);

      if (strcmp(name, "id") == 0) {
        char *id;
        ret = dsvdc_property_get_string(properties, i, &id);
        if (ret != DSVDC_OK) {
          vdc_report(LOG_ERR, "request_generic_cb: error getting property value from property %s\n", name);
          code = DSVDC_ERR_INVALID_VALUE_TYPE;
          break;
        }
        
        scene_t *scene_data;
        scene_data = malloc(sizeof(scene_t));
        if (!scene_data) {
          return;
        }
        memset(scene_data, 0, sizeof(scene_t));
          
        if(strcasecmp(id, "ActTurnOn") == 0) {
          vdc_report(LOG_DEBUG, "Exec Start Cabin command");
          klafs_power_on();
          
        } else if(strcasecmp(id, "ActTurnOff") == 0) {
          vdc_report(LOG_DEBUG, "Exec Start Cabin command");
          klafs_power_off();
        } else if(strcasecmp(id, "ActModeSauna") == 0) {
          vdc_report(LOG_DEBUG, "Exec Mode Sauna command");
          scene_data->saunaSelected = true;
          scene_data->sanariumSelected = false;
          scene_data->irSelected = false;
          klafs_change_mode(scene_data);
        } else if(strcasecmp(id, "ActModeSanarium") == 0) {
          vdc_report(LOG_DEBUG, "Exec Mode Sanarium command");
          scene_data->saunaSelected = false;
          scene_data->sanariumSelected = true;
          scene_data->irSelected = false;
          klafs_change_mode(scene_data);
        } else if(strcasecmp(id, "ActModeIR") == 0) {
          vdc_report(LOG_DEBUG, "Exec Mode IR command");
          scene_data->saunaSelected = false;
          scene_data->sanariumSelected = false;
          scene_data->irSelected = true;
          klafs_change_mode(scene_data);
        } else if(strcasecmp(id, "ActSommerSaunaClassic") == 0) {
          vdc_report(LOG_DEBUG, "Exec Program SommerSaunaClassic command");
          scene_data->saunaSelected = true;
          scene_data->sanariumSelected = false;
          scene_data->irSelected = false;
          scene_data->selectedSaunaTemperature = 75;
          klafs_change_mode(scene_data);
          klafs_change_temperature(scene_data);
          klafs_power_on();
        } else if(strcasecmp(id, "ActChilloutSauna") == 0) {
          vdc_report(LOG_DEBUG, "Exec Program ChilloutSauna command");
          scene_data->saunaSelected = true;
          scene_data->sanariumSelected = false;
          scene_data->irSelected = false;
          scene_data->selectedSaunaTemperature = 80;
          klafs_change_mode(scene_data);
          klafs_change_temperature(scene_data);
          //klafs_power_on();  
        } else if(strcasecmp(id, "ActAfterFitnessSauna") == 0) {
          vdc_report(LOG_DEBUG, "Exec Program FitnessSauna command");
          scene_data->saunaSelected = true;
          scene_data->sanariumSelected = false;
          scene_data->irSelected = false;
          scene_data->selectedSaunaTemperature = 85;
          klafs_change_mode(scene_data);
          klafs_change_temperature(scene_data);
          klafs_power_on();    
        } else if(strcasecmp(id, "ActClassicSauna") == 0) {
          vdc_report(LOG_DEBUG, "Exec Program ClassicSauna command");
          scene_data->saunaSelected = true;
          scene_data->sanariumSelected = false;
          scene_data->irSelected = false;
          scene_data->selectedSaunaTemperature = 90;
          klafs_change_mode(scene_data);
          klafs_change_temperature(scene_data);
          klafs_power_on();    
        } else if(strcasecmp(id, "ActSoftSauna") == 0) {
          vdc_report(LOG_DEBUG, "Exec Program SoftSauna command");
          scene_data->saunaSelected = true;
          scene_data->sanariumSelected = false;
          scene_data->irSelected = false;
          scene_data->selectedSaunaTemperature = 65;
          klafs_change_mode(scene_data);
          klafs_change_temperature(scene_data);
          klafs_power_on();      
        } else if(strcasecmp(id, "ActSummerSaunaSoft") == 0) {
          vdc_report(LOG_DEBUG, "Exec Program SommerSaunaSoft command");
          scene_data->saunaSelected = true;
          scene_data->sanariumSelected = false;
          scene_data->irSelected = false;
          scene_data->selectedSaunaTemperature = 70;
          klafs_change_mode(scene_data);
          klafs_change_temperature(scene_data);
          klafs_power_on();      
        } else if(strcasecmp(id, "ActRelaxSanarium") == 0) {
          vdc_report(LOG_DEBUG, "Exec Program RelaxSanarium command");
          scene_data->saunaSelected = false;
          scene_data->sanariumSelected = true;
          scene_data->irSelected = false;
          scene_data->selectedSaunaTemperature = 50;
          scene_data->selectedHumLevel = 4;
          klafs_change_mode(scene_data);
          klafs_change_temperature(scene_data);
          klafs_change_humidity(scene_data);
          klafs_power_on();        
        } else if(strcasecmp(id, "ActBeautySanarium") == 0) {
          vdc_report(LOG_DEBUG, "Exec Program BeautySanarium command");
          scene_data->saunaSelected = false;
          scene_data->sanariumSelected = true;
          scene_data->irSelected = false;
          scene_data->selectedSaunaTemperature = 55;
          scene_data->selectedHumLevel = 8;
          klafs_change_mode(scene_data);
          klafs_change_temperature(scene_data);
          klafs_change_humidity(scene_data);
          klafs_power_on();          
        } else if(strcasecmp(id, "ActFamilySanarium") == 0) {
          vdc_report(LOG_DEBUG, "Exec Program FamilySanarium command");
          scene_data->saunaSelected = false;
          scene_data->sanariumSelected = true;
          scene_data->irSelected = false;
          scene_data->selectedSaunaTemperature = 50;
          scene_data->selectedHumLevel = 8;
          klafs_change_mode(scene_data);
          klafs_change_temperature(scene_data);
          klafs_change_humidity(scene_data);
          klafs_power_on();            
        } else if(strcasecmp(id, "ActImmunPowerSanarium") == 0) {
          vdc_report(LOG_DEBUG, "Exec Program ImmunPowerSanarium command");
          scene_data->saunaSelected = false;
          scene_data->sanariumSelected = true;
          scene_data->irSelected = false;
          scene_data->selectedSaunaTemperature = 50;
          scene_data->selectedHumLevel = 6;
          klafs_change_mode(scene_data);
          klafs_change_temperature(scene_data);
          klafs_change_humidity(scene_data);
          klafs_power_on();              
        } else if(strcasecmp(id, "ActVitalSanarium") == 0) {
          vdc_report(LOG_DEBUG, "Exec Program VitalPowerSanarium command");
          scene_data->saunaSelected = false;
          scene_data->sanariumSelected = true;
          scene_data->irSelected = false;
          scene_data->selectedSaunaTemperature = 45;
          scene_data->selectedHumLevel = 6;
          klafs_change_mode(scene_data);
          klafs_change_temperature(scene_data);
          klafs_change_humidity(scene_data);
          klafs_power_on();                
        } else if(strcasecmp(id, "ActTropenSanarium") == 0) {
          vdc_report(LOG_DEBUG, "Exec Program TropenSanarium command");
          scene_data->saunaSelected = false;
          scene_data->sanariumSelected = true;
          scene_data->irSelected = false;
          scene_data->selectedSaunaTemperature = 60;
          scene_data->selectedHumLevel = 10;
          klafs_change_mode(scene_data);
          klafs_change_temperature(scene_data);
          klafs_change_humidity(scene_data);
          klafs_power_on();                  
        } else if(strcasecmp(id, "ActSubtropenSanarium") == 0) {
          vdc_report(LOG_DEBUG, "Exec Program SubtropenSanarium command");
          scene_data->saunaSelected = false;
          scene_data->sanariumSelected = true;
          scene_data->irSelected = false;
          scene_data->selectedSaunaTemperature = 60;
          scene_data->selectedHumLevel = 8;
          klafs_change_mode(scene_data);
          klafs_change_temperature(scene_data);
          klafs_change_humidity(scene_data);
          klafs_power_on();                    
        } else if(strcasecmp(id, "ActFitnessSanarium") == 0) {
          vdc_report(LOG_DEBUG, "Exec Program FitnessSanarium command");
          scene_data->saunaSelected = false;
          scene_data->sanariumSelected = true;
          scene_data->irSelected = false;
          scene_data->selectedSaunaTemperature = 55;
          scene_data->selectedHumLevel = 10;
          klafs_change_mode(scene_data);
          klafs_change_temperature(scene_data);
          klafs_change_humidity(scene_data);
          klafs_power_on();                      
        } else {
          vdc_report(LOG_NOTICE, "request_generic_cb: command = %s not implemented\n", id);
        }
      }
    }      
  }
}

void vdc_savescene_cb(dsvdc_t *handle __attribute__((unused)), char **dsuid, size_t n_dsuid, int32_t scene, int32_t *group, int32_t *zone_id, void *userdata) {
  vdc_report(LOG_NOTICE, "save scene %d\n", scene);
  if (strcasecmp(sauna_device->dsuidstring, *dsuid) == 0) {
    klafs_get_values();
    save_scene(scene);
    write_config();
  }
}
  
void vdc_callscene_cb(dsvdc_t *handle __attribute__((unused)), char **dsuid, size_t n_dsuid, int32_t scene, bool force, int32_t *group, int32_t *zone_id, void *userdata) {
/**  for(int n = 0; n < n_dsuid; n++)
    {
         vdc_report(LOG_NOTICE,"received %scall scene for device %s\n", force?"forced ":"", *dsuid);
    } **/

  if (strcasecmp(sauna_device->dsuidstring, *dsuid) == 0) {
    vdc_report(LOG_NOTICE, "called scene: %d\n", scene);

    bool is_configured = is_scene_configured(scene);
    if(is_configured) {
      scene_t *scene_data = get_scene_configuration(scene);
      if (scene_data != NULL) {
        if (!scene_data->isPoweredOn) {
          vdc_report(LOG_DEBUG, "handling a power OFF scene!\n");
          klafs_power_off();
        } else {
          vdc_report(LOG_DEBUG, "handling a power ON scene!\n");
          klafs_change_favoriteprogram(scene_data);
          klafs_power_on();          
        }  
        free(scene_data);
      } else {
          vdc_report(LOG_INFO, "memory allocation for scene data failed!");
      }        
    } else {
      vdc_report(LOG_INFO, "scene not handled"); 
    }  
  }
}

void vdc_setprop_cb(dsvdc_t *handle, const char *dsuid, dsvdc_property_t *property, const dsvdc_property_t *properties, void *userdata) {
  (void) userdata;
  int ret;
  uint8_t code = DSVDC_ERR_NOT_IMPLEMENTED;
  size_t i;
  vdc_report(LOG_INFO, "set property request for dsuid \"%s\"\n", dsuid);

  /*
   * Properties for the VDC
   */
  if (strcasecmp(g_vdc_dsuid, dsuid) == 0) {
    for (i = 0; i < dsvdc_property_get_num_properties(properties); i++) {
      char *name;
      ret = dsvdc_property_get_name(properties, i, &name);
      if (ret != DSVDC_OK) {
        vdc_report(LOG_ERR, "setprop_cb: error getting property name\n");
        code = DSVDC_ERR_MISSING_DATA;
        break;
      }
      if (!name) {
        vdc_report(LOG_ERR, "setprop_cb: not handling wildcard properties\n");
        code = DSVDC_ERR_NOT_IMPLEMENTED;
        break;
      }

      vdc_report(LOG_INFO, "set request for name=\"%s\"\n", name);

      if (strcmp(name, "zoneID") == 0) {
        uint64_t zoneID;
        ret = dsvdc_property_get_uint(properties, i, &zoneID);
        if (ret != DSVDC_OK) {
          vdc_report(LOG_ERR, "setprop_cb: error getting property value from property %s\n", name);
          code = DSVDC_ERR_INVALID_VALUE_TYPE;
          break;
        }
        vdc_report(LOG_NOTICE, "setprop_cb: \"%s\" = %d\n", name, zoneID);
        g_default_zoneID = zoneID;
        code = DSVDC_OK;
      } else {
        code = DSVDC_ERR_NOT_FOUND;
        break;
      }

      free(name);
    }

    if (code == DSVDC_OK) {
      write_config();
    }

    dsvdc_send_set_property_response(handle, property, code);
    return;
  } 
  
  if (strcasecmp(sauna_device->dsuidstring, dsuid) != 0) {	  
    vdc_report(LOG_WARNING, "set property: unhandled dsuid %s\n", dsuid);
    dsvdc_property_free(property);
    return;
  }

  /*
   * Properties for the VDSD's
   */
  pthread_mutex_lock(&g_network_mutex);
  for (i = 0; i < dsvdc_property_get_num_properties(properties); i++) {
    char *name;

    int ret = dsvdc_property_get_name(properties, i, &name);
    if (ret != DSVDC_OK) {
      vdc_report(LOG_ERR, "getprop_cb: error getting property name, abort\n");
      dsvdc_send_get_property_response(handle, property);
      pthread_mutex_unlock(&g_network_mutex);
      return;
    }
    if (!name) {
      vdc_report(LOG_ERR, "getprop_cb: not yet handling wildcard properties\n");
      //dsvdc_send_property_response(handle, property);
      continue;
    }
    vdc_report(LOG_NOTICE, "get request name!!=\"%s\"\n", name);

    if (strcmp(name, "zoneID") == 0) {
      uint64_t zoneID;
      ret = dsvdc_property_get_uint(properties, i, &zoneID);
      if (ret != DSVDC_OK) {
        vdc_report(LOG_ERR, "setprop_cb: error getting property value from property %s\n", name);
        code = DSVDC_ERR_INVALID_VALUE_TYPE;
        break;
      }
      vdc_report(LOG_NOTICE, "setprop_cb: \"%s\" = %d\n", name, zoneID);
      sauna_device->sauna->zoneID = zoneID;
      code = DSVDC_OK;
    } else {
      code = DSVDC_OK;
    }
      

    free(name);
  }
  pthread_mutex_unlock(&g_network_mutex);

  dsvdc_send_set_property_response(handle, property, code);
}

dsvdc_property_t* create_action_property(char *id, char *title, char* description) {
  dsvdc_property_t *nProp;
  if (dsvdc_property_new(&nProp) != DSVDC_OK) {
    vdc_report(LOG_ERR, "failed to allocate reply property");
    return NULL;
  }
               
  dsvdc_property_add_string (nProp, "id", id);
  dsvdc_property_add_string (nProp, "action", id);
  dsvdc_property_add_string (nProp, "title", title);
  dsvdc_property_add_string (nProp, "description", description);
  
  return nProp;
}

void vdc_getprop_cb(dsvdc_t *handle, const char *dsuid, dsvdc_property_t *property, const dsvdc_property_t *query, void *userdata) {
  (void) userdata;
  int ret;
  size_t i;
  char *name;
  
  vdc_report(LOG_INFO, "get property for dsuid: %s\n", dsuid);

  /*
   * Properties for the VDC
   */
  if (strcasecmp(g_vdc_dsuid, dsuid) == 0) {
    for (i = 0; i < dsvdc_property_get_num_properties(query); i++) {

      int ret = dsvdc_property_get_name(query, i, &name);
      if (ret != DSVDC_OK) {
        vdc_report(LOG_ERR, "getprop_cb: error getting property name, abort\n");
        dsvdc_send_get_property_response(handle, property);
        return;
      }
      if (!name) {
        vdc_report(LOG_ERR, "getprop_cb: not yet handling wildcard properties\n");
        dsvdc_send_get_property_response(handle, property);
        return;
      }
      vdc_report(LOG_NOTICE, "get request name=\"%s\"\n", name);

      if ((strcmp(name, "hardwareGuid") == 0) || (strcmp(name, "displayId") == 0)) {
        char info[256];
        char buffer[32];
        size_t n;

        memset(info, 0, sizeof(info));
        if (strcmp(name, "hardwareGuid") == 0) {
          strcpy(info, "sauna-id:");
        }
        sprintf(buffer, "%d", klafs.sauna.id);
        strcat(info, buffer);
        dsvdc_property_add_string(property, name, info);

      } else if (strcmp(name, "vendorId") == 0) {
      } else if (strcmp(name, "oemGuid") == 0) {

      } else if (strcmp(name, "implementationId") == 0) {
        dsvdc_property_add_string(property, name, "Klafs Sauna");

      } else if (strcmp(name, "modelUID") == 0) {
        dsvdc_property_add_string(property, name, "Klafs Sauna");

      } else if (strcmp(name, "modelGuid") == 0) {
        dsvdc_property_add_string(property, name, "Klafs Sauna");

      } else if (strcmp(name, "name") == 0) {
        char info[256];
        strcpy(info, "Klafs Sauna ");
        strcat(info, klafs.sauna.name);
        dsvdc_property_add_string(property, name, info);

      } else if (strcmp(name, "model") == 0) {
        char hostname[HOST_NAME_MAX];
        gethostname(hostname, HOST_NAME_MAX);
        char servicename[HOST_NAME_MAX + 32];
        strcpy(servicename, "Klafs Sauna Controller @");
        strcat(servicename, hostname);
        dsvdc_property_add_string(property, name, servicename);

      } else if (strcmp(name, "capabilities") == 0) {
        dsvdc_property_t *reply;
        ret = dsvdc_property_new(&reply);
        if (ret != DSVDC_OK) {
          vdc_report(LOG_ERR, "failed to allocate reply property for %s\n", name);
          free(name);
          continue;
        }
        dsvdc_property_add_bool(reply, "metering", false);
        dsvdc_property_add_bool(reply, "dynamicDefinitions", true);
        dsvdc_property_add_property(property, name, &reply);

      } else if (strcmp(name, "configURL") == 0) {


      } else if (strcmp(name, "zoneID") == 0) {
        dsvdc_property_add_uint(property, "zoneID", g_default_zoneID);

      /* user properties: user name, client_id, status */

      }
      free(name);
    }

    dsvdc_send_get_property_response(handle, property);
    return;
  } 

  if (strcasecmp(sauna_device->dsuidstring, dsuid) != 0) {	  
    vdc_report(LOG_WARNING, "get property: unhandled dsuid %s\n", dsuid);
    dsvdc_property_free(property);
    return;
  }

  /*
   * Properties for the VDSD's
   */
  pthread_mutex_lock(&g_network_mutex);
  for (i = 0; i < dsvdc_property_get_num_properties(query); i++) {

    int ret = dsvdc_property_get_name(query, i, &name);
    if (ret != DSVDC_OK) {
      vdc_report(LOG_ERR, "getprop_cb: error getting property name, abort\n");
      dsvdc_send_get_property_response(handle, property);
      pthread_mutex_unlock(&g_network_mutex);
      return;
    }
    if (!name) {
      vdc_report(LOG_ERR, "getprop_cb: not yet handling wildcard properties\n");
      //dsvdc_send_property_response(handle, property);
      continue;
    }
    vdc_report(LOG_NOTICE, "get request name=\"%s\"\n", name);

    if (strcmp(name, "primaryGroup") == 0) {
      dsvdc_property_add_uint(property, "primaryGroup", 9);
    } else if (strcmp(name, "zoneID") == 0) {
      dsvdc_property_add_uint(property, "zoneID", sauna_device->sauna->zoneID);
    } else if (strcmp(name, "buttonInputDescriptions") == 0) {
     

    } else if (strcmp(name, "buttonInputSettings") == 0) {
      
    } else if (strcmp(name, "dynamicActionDescriptions") == 0) { 
      dsvdc_property_t *reply;
      ret = dsvdc_property_new(&reply);
      if (ret != DSVDC_OK) {
        vdc_report(LOG_ERR, "failed to allocate reply property for %s\n", name);
        free(name);
        continue;
      }

      int i = 0;
      char propIndex[64];     
      dsvdc_property_t *nProp;

      nProp = create_action_property("dynamic.ActTurnOn", "01-Starten", "01-Starten");          
      //snprintf(propIndex, 64, "%d", i++);
      dsvdc_property_add_property(reply, "ActTurnOn", &nProp);
      
      nProp = create_action_property("dynamic.ActTurnOff", "02-Beenden", "02-Beenden");          
      //snprintf(propIndex, 64, "%d", i++);
      dsvdc_property_add_property(reply, "ActTurnOff", &nProp);
      
      nProp = create_action_property("dynamic.ActModeSauna", "03-Sauna wählen", "03-Sauna wählen");          
      //snprintf(propIndex, 64, "%d", i++);
      dsvdc_property_add_property(reply, "ActModeSauna", &nProp);
      
      nProp = create_action_property("dynamic.ActModeSanarium", "04-Sanarium wählen", "04-Sanarium wählen");          
      //snprintf(propIndex, 64, "%d", i++);
      dsvdc_property_add_property(reply, "ActModeSanarium", &nProp);
      
      nProp = create_action_property("dynamic.ActModeIR", "05-Infrarot wählen", "05-Infrarot wählen");          
      //snprintf(propIndex, 64, "%d", i++);
      dsvdc_property_add_property(reply, "ActModeIR", &nProp);
      
      nProp = create_action_property("dynamic.ActSommerSaunaClassic", "06-Sommer Sauna Classic", "06-Sommer Sauna Classic");          
      //snprintf(propIndex, 64, "%d", i++);
      dsvdc_property_add_property(reply, "ActSommerSaunaClassic", &nProp);
      
      nProp = create_action_property("dynamic.ActChilloutSauna", "07-Chillout Sauna", "07-Chillout Sauna");          
      //snprintf(propIndex, 64, "%d", i++);
      dsvdc_property_add_property(reply, "ActChilloutSauna", &nProp);
      
      nProp = create_action_property("dynamic.ActAfterFitnessSauna", "08-After-Fitness Sauna", "08-After-Fitness Sauna");          
      //snprintf(propIndex, 64, "%d", i++);
      dsvdc_property_add_property(reply, "ActAfterFitnessSauna", &nProp);
      
      nProp = create_action_property("dynamic.ActClassicSauna", "09-Classic Sauna", "09-Classic Sauna");          
      //snprintf(propIndex, 64, "%d", i++);
      dsvdc_property_add_property(reply, "ActClassicSauna", &nProp);
      
      nProp = create_action_property("dynamic.ActSoftSauna", "10-Soft Sauna", "10-Soft Sauna");          
      //snprintf(propIndex, 64, "%d", i++);
      dsvdc_property_add_property(reply, "ActSoftSauna", &nProp);
      
      nProp = create_action_property("dynamic.ActSummerSaunaSoft", "11-Sommer Sauna Soft", "11-Sommer Sauna Soft");          
      //snprintf(propIndex, 64, "%d", i++);
      dsvdc_property_add_property(reply, "ActSummerSaunaSoft", &nProp);
      
      nProp = create_action_property("dynamic.ActRelaxSanarium", "12-Relax Sanarium", "12-Relax Sanarium");          
      //snprintf(propIndex, 64, "%d", i++);
      dsvdc_property_add_property(reply, "ActRelaxSanarium", &nProp);
      
      nProp = create_action_property("dynamic.ActBeautySanarium", "13-Beauty Sanarium", "13-Beauty Sanarium");          
      //snprintf(propIndex, 64, "%d", i++);
      dsvdc_property_add_property(reply, "ActBeautySanarium", &nProp);
            
      nProp = create_action_property("dynamic.ActFamilySanarium", "14-Family Sanarium", "14-Family Sanarium");          
      //snprintf(propIndex, 64, "%d", i++);
      dsvdc_property_add_property(reply, "ActFamilySanarium", &nProp);
      
      nProp = create_action_property("dynamic.ActImmunPowerSanarium", "15-Immun Power Sanarium", "15-Immun Power Sanarium");          
      //snprintf(propIndex, 64, "%d", i++);
      dsvdc_property_add_property(reply, "ActImmunPowerSanarium", &nProp);
      
      nProp = create_action_property("dynamic.ActVitalSanarium", "16-Vital Sanarium", "16-Vital Sanarium");          
      //snprintf(propIndex, 64, "%d", i++);
      dsvdc_property_add_property(reply, "ActVitalSanarium", &nProp);
           
      nProp = create_action_property("dynamic.ActTropenSanarium", "17-Tropen Sanarium", "17-Tropen Sanarium");          
      //snprintf(propIndex, 64, "%d", i++);
      dsvdc_property_add_property(reply, "ActTropenSanarium", &nProp);
      
      nProp = create_action_property("dynamic.ActSubtropenSanarium", "18-Subtropen Sanarium", "18-Subtropen Sanarium");          
      //snprintf(propIndex, 64, "%d", i++);
      dsvdc_property_add_property(reply, "ActSubtropenSanarium", &nProp);
      
      nProp = create_action_property("dynamic.ActFitnessSanarium", "19-Fitness Sanarium", "19-Fitness Sanarium");          
      //snprintf(propIndex, 64, "%d", i++);
      dsvdc_property_add_property(reply, "ActFitnessSanarium", &nProp); 

      dsvdc_property_add_property(property, name, &reply); 

    } else if (strcmp(name, "outputDescription") == 0) {

    } else if (strcmp(name, "outputSettings") == 0) {

    } else if (strcmp(name, "channelDescriptions") == 0) {
    } else if (strcmp(name, "channelSettings") == 0) {
    } else if (strcmp(name, "channelStates") == 0) {
    } else if (strcmp(name, "deviceStates") == 0) {
      
    } else if (strcmp(name, "deviceProperties") == 0) {
      
    } else if (strcmp(name, "devicePropertyDescriptions") == 0) {
    
    } else if (strcmp(name, "customActions") == 0) {

    } else if (strcmp(name, "binaryInputDescriptions") == 0) {      
        dsvdc_property_t *reply;
        ret = dsvdc_property_new(&reply);
        if (ret != DSVDC_OK) {
          vdc_report(LOG_ERR, "failed to allocate reply property for %s\n", name);
          free(name);
          continue;
        }

        int i = 0;
        char sensorName[64];
        char sensorIndex[64];
        
        while (1) {
          if (sauna_device->sauna->binary_values[i].is_active) {
            vdc_report(LOG_ERR, "************* %d %s\n", i, sauna_device->sauna->binary_values[i].value_name);
           
            snprintf(sensorName, 64, "%s-%s", sauna_device->sauna->name, sauna_device->sauna->binary_values[i].value_name);
          
            dsvdc_property_t *nProp;
            if (dsvdc_property_new(&nProp) != DSVDC_OK) {
              vdc_report(LOG_ERR, "failed to allocate reply property for %s/%s\n", name, sensorName);
              break;
            }
        
            dsvdc_property_add_string(nProp, "name", sensorName);
            dsvdc_property_add_uint(nProp, "inputType", 1);
            dsvdc_property_add_uint(nProp, "inputUsage", 0);
            dsvdc_property_add_uint(nProp, "sensorFunction", sauna_device->sauna->binary_values[i].sensor_function);
            dsvdc_property_add_double(nProp, "updateInterval", 5);
          
            snprintf(sensorIndex, 64, "%d", i);
            dsvdc_property_add_property(reply, sensorIndex, &nProp);

            vdc_report(LOG_INFO, "binaryInputDescription: dsuid %s sensorIndex %s: %s function %d\n", dsuid, sensorIndex, sensorName, sauna_device->sauna->binary_values[i].sensor_function);
            
            i++;
          } else {
            break;
          }
        }
        
        dsvdc_property_add_property(property, name, &reply);
        
    } else if (strcmp(name, "binaryInputSettings") == 0) {      
      dsvdc_property_t *reply;
      ret = dsvdc_property_new(&reply);
      if (ret != DSVDC_OK) {
        vdc_report(LOG_ERR, "failed to allocate reply property for %s\n", name);
        free(name);
        continue;
      }

      char sensorIndex[64];
      int i = 0;
      while (1) {
        if (sauna_device->sauna->binary_values[i].is_active) {
          dsvdc_property_t *nProp;
          if (dsvdc_property_new(&nProp) != DSVDC_OK) {
            vdc_report(LOG_ERR, "failed to allocate reply property for %s\n", name);
            break;
          }
          dsvdc_property_add_uint(nProp, "group", 8);
          dsvdc_property_add_uint(nProp, "sensorFunction", sauna_device->sauna->binary_values[i].sensor_function);

          snprintf(sensorIndex, 64, "%d", i);
          dsvdc_property_add_property(reply, sensorIndex, &nProp);
        
          vdc_report(LOG_INFO, "binaryInputSettings: dsuid %s sensorIndex %s:  function %d\n", dsuid, sensorIndex, sauna_device->sauna->binary_values[i].sensor_function);
          
          i++;
        } else {
          break;
        }
      }
      dsvdc_property_add_property(property, name, &reply);
      
    } else if (strcmp(name, "sensorDescriptions") == 0) {
      dsvdc_property_t *reply;
      ret = dsvdc_property_new(&reply);
      if (ret != DSVDC_OK) {
        vdc_report(LOG_ERR, "failed to allocate reply property for %s\n", name);
        free(name);
        continue;
      }

      int i = 0;
      char sensorName[64];
      char sensorIndex[64];
	    
      while(1) {
        if (sauna_device->sauna->sensor_values[i].is_active) {
          vdc_report(LOG_ERR, "************* %d %s\n", i, sauna_device->sauna->sensor_values[i].value_name);
        
          snprintf(sensorName, 64, "%s-%s", sauna_device->sauna->name, sauna_device->sauna->sensor_values[i].value_name);

          dsvdc_property_t *nProp;
          if (dsvdc_property_new(&nProp) != DSVDC_OK) {
            vdc_report(LOG_ERR, "failed to allocate reply property for %s/%s\n", name, sensorName);
            break;
          }
          dsvdc_property_add_string(nProp, "name", sensorName);
          dsvdc_property_add_uint(nProp, "sensorType", sauna_device->sauna->sensor_values[i].sensor_type);
          dsvdc_property_add_uint(nProp, "sensorUsage", sauna_device->sauna->sensor_values[i].sensor_usage);
          dsvdc_property_add_double(nProp, "aliveSignInterval", 300);

          snprintf(sensorIndex, 64, "%d", i);
          dsvdc_property_add_property(reply, sensorIndex, &nProp);

          vdc_report(LOG_INFO, "sensorDescription: dsuid %s sensorIndex %s: %s type %d usage %d\n", dsuid, sensorIndex, sensorName, sauna_device->sauna->sensor_values[i].sensor_type, sauna_device->sauna->sensor_values[i].sensor_usage); 
          
          i++;
        } else {
          break;
        }
      }

		  dsvdc_property_add_property(property, name, &reply);  
    } else if (strcmp(name, "sensorSettings") == 0) {
      dsvdc_property_t *reply;
      ret = dsvdc_property_new(&reply);
      if (ret != DSVDC_OK) {
        vdc_report(LOG_ERR, "failed to allocate reply property for %s\n", name);
        free(name);
        continue;
      }

      char sensorIndex[64];
      int i = 0;
      while (1) {
        if (sauna_device->sauna->sensor_values[i].is_active) {
          dsvdc_property_t *nProp;
          if (dsvdc_property_new(&nProp) != DSVDC_OK) {
            vdc_report(LOG_ERR, "failed to allocate reply property for %s\n", name);
            break;
          }
          dsvdc_property_add_uint(nProp, "group", 8);
          dsvdc_property_add_uint(nProp, "minPushInterval", 5);
          dsvdc_property_add_double(nProp, "changesOnlyInterval", 5);

          snprintf(sensorIndex, 64, "%d", i);
          dsvdc_property_add_property(reply, sensorIndex, &nProp);
          
          i++;
        } else {
          break;
        }
      }
      dsvdc_property_add_property(property, name, &reply); 
      
      vdc_report(LOG_INFO, "sensorSettings: dsuid %s sensorIndex %s\n", dsuid, sensorIndex); 

    } else if (strcmp(name, "sensorStates") == 0) {
      dsvdc_property_t *reply;
      ret = dsvdc_property_new(&reply);
      if (ret != DSVDC_OK) {
        vdc_report(LOG_ERR, "failed to allocate reply property for %s\n", name);
        free(name);
        continue;
      }

      int idx;
      char* sensorIndex;
      dsvdc_property_t *sensorRequest;
      dsvdc_property_get_property_by_index(query, 0, &sensorRequest);
      if (dsvdc_property_get_name(sensorRequest, 0, &sensorIndex) != DSVDC_OK) {
        vdc_report(LOG_DEBUG, "sensorStates: no index in request\n");
        idx = -1;
      } else {
        idx = strtol(sensorIndex, NULL, 10);
      }
      dsvdc_property_free(sensorRequest);

      time_t now = time(NULL);
      
      int i = 0;
      while (1) {
        if (sauna_device->sauna->sensor_values[i].is_active) {
          if (idx >= 0 && idx != i) {
            i++;
            continue;
          }

          dsvdc_property_t *nProp;
          if (dsvdc_property_new(&nProp) != DSVDC_OK) {
            vdc_report(LOG_ERR, "failed to allocate reply property for %s\n", name);
            break;
          }

          double val = sauna_device->sauna->sensor_values[i].value;

          dsvdc_property_add_double(nProp, "value", val);
          dsvdc_property_add_int(nProp, "age", now - sauna_device->sauna->sensor_values[i].last_query);
          dsvdc_property_add_int(nProp, "error", 0);

          char replyIndex[64];
          snprintf(replyIndex, 64, "%d", i);
          dsvdc_property_add_property(reply, replyIndex, &nProp);
          
          i++;
        } else {
          break;
        }
      }
      dsvdc_property_add_property(property, name, &reply);  

    } else if (strcmp(name, "binaryInputStates") == 0) {      
      dsvdc_property_t *reply;
      ret = dsvdc_property_new(&reply);
      if (ret != DSVDC_OK) {
        vdc_report(LOG_ERR, "failed to allocate reply property for %s\n", name);
        free(name);
        continue;
      }

      int idx;
      char* sensorIndex;
      dsvdc_property_t *sensorRequest;
      dsvdc_property_get_property_by_index(query, 0, &sensorRequest);
      if (dsvdc_property_get_name(sensorRequest, 0, &sensorIndex) != DSVDC_OK) {
        vdc_report(LOG_DEBUG, "binaryInputStates: no index in request\n");
        idx = -1;
      } else {
        idx = strtol(sensorIndex, NULL, 10);
      }
      dsvdc_property_free(sensorRequest);
      
      time_t now = time(NULL);

      int i = 0;
      while (1) {
        if (sauna_device->sauna->binary_values[i].is_active) {
          if (idx >= 0 && idx != i) {
            i++;
            continue;
          }
        
          dsvdc_property_t *nProp;
          if (dsvdc_property_new(&nProp) != DSVDC_OK) {
            vdc_report(LOG_ERR, "failed to allocate reply property for %s\n", name);
            break;
          }
        
          dsvdc_property_add_bool(nProp, "value",  sauna_device->sauna->binary_values[i].value);
          dsvdc_property_add_int(nProp, "age", now - sauna_device->sauna->binary_values[i].last_query);
          dsvdc_property_add_int(nProp, "error", 0);

          char replyIndex[64];
          snprintf(replyIndex, 64, "%d", i);
          dsvdc_property_add_property(reply, replyIndex, &nProp);
          
          i++;
        } else {
          break;
        }
      }    
      dsvdc_property_add_property(property, name, &reply); 

    } else if (strcmp(name, "name") == 0) {
      dsvdc_property_add_string(property, name, sauna_device->sauna->name);

    } else if (strcmp(name, "type") == 0) {
      dsvdc_property_add_string(property, name, "vDSD");

    } else if (strcmp(name, "model") == 0) {
      char info[256];
      strcpy(info, "Sauna");
     
      dsvdc_property_add_string(property, name, info);
    } else if (strcmp(name, "modelFeatures") == 0) {
      dsvdc_property_t *nProp;
      dsvdc_property_new(&nProp);
      dsvdc_property_add_bool(nProp, "dontcare", false);
      dsvdc_property_add_bool(nProp, "blink", false);
      dsvdc_property_add_bool(nProp, "outmode", false);
      dsvdc_property_add_bool(nProp, "jokerconfig", true);
      dsvdc_property_add_property(property, name, &nProp);
    } else if (strcmp(name, "modelUID") == 0) {      
      dsvdc_property_add_string(property, name, "Klafs Sauna");

    } else if (strcmp(name, "modelVersion") == 0) {
      dsvdc_property_add_string(property, name, "0");

    } else if (strcmp(name, "deviceClass") == 0) {
    } else if (strcmp(name, "deviceClassVersion") == 0) {
    } else if (strcmp(name, "oemGuid") == 0) {
    } else if (strcmp(name, "oemModelGuid") == 0) {

    } else if (strcmp(name, "vendorId") == 0) {
      dsvdc_property_add_string(property, name, "vendor: Klafs");

    } else if (strcmp(name, "vendorName") == 0) {
      dsvdc_property_add_string(property, name, "Klafs");

    } else if (strcmp(name, "vendorGuid") == 0) {
      char info[256];
      strcpy(info, "Klafs vDC ");
      strcat(info, sauna_device->sauna->id);
      dsvdc_property_add_string(property, name, info);

    } else if (strcmp(name, "hardwareVersion") == 0) {
      dsvdc_property_add_string(property, name, "0.0.0");

    } else if (strcmp(name, "configURL") == 0) {
      dsvdc_property_add_string(property, name, "");

    } else if (strcmp(name, "hardwareModelGuid") == 0) {
      dsvdc_property_add_string(property, name, "");

    } else if (strcmp(name, "deviceIcon16") == 0) {
        dsvdc_property_add_bytes(property, name, gIconStation16Data, gIconStation16Size);

    } else if (strcmp(name, "deviceIcon48") == 0) {
        dsvdc_property_add_bytes(property, name, gIconStation48Data, gIconStation48Size);

    } else if (strcmp(name, "deviceIconName") == 0) {
      char info[256];
      strcpy(info, "klafs-sauna-16.png");
      
      dsvdc_property_add_string(property, name, info);

    } else {
      vdc_report(LOG_WARNING, "get property handler: unhandled name=\"%s\"\n", name);
    }

    free(name);
  }

  pthread_mutex_unlock(&g_network_mutex);
  dsvdc_send_get_property_response(handle, property);
}
