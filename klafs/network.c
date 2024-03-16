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
#include <ctype.h>
#include <pthread.h>

#include <curl/curl.h>
#include <json.h>
#include <utlist.h>

#include <digitalSTROM/dsuid.h>
#include <dsvdc/dsvdc.h>

#include "klafs.h"

const char *url_getsaunastatus = "https://sauna-app-19.klafs.com/SaunaApp/GetData";
const char *url_startcabin = "https://sauna-app-19.klafs.com/SaunaApp/StartCabin";
const char *url_postconfigchange = "https://sauna-app-19.klafs.com//Control/PostConfigChange";
const char *url_stopcabin = "https://sauna-app-19.klafs.com/SaunaApp/StopCabin";
const char *url_login = "https://sauna-app-19.klafs.com/Account/Login";
const char *url_changeTemperature = "https://sauna-app-19.klafs.com/SaunaApp/ChangeTemperature";
const char *url_changeHumidity = "https://sauna-app-19.klafs.com/SaunaApp/ChangeHumLevel";
const char *url_changeFavoriteProgram = "https://sauna-app-19.klafs.com/SaunaApp/FavoriteSelected";
const char *url_changeMode = "https://sauna-app-19.klafs.com/SaunaApp/SetMode";


struct memory_struct {
  char *memory;
  size_t size;
};

struct data {
  char trace_ascii; /* 1 or 0 */
};

struct curl_slist *cookielist;

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
  size_t realsize = size * nmemb;
  struct memory_struct *mem = (struct memory_struct *) userp;

  mem->memory = realloc(mem->memory, mem->size + realsize + 1);
  if (mem->memory == NULL) {
    vdc_report(LOG_ERR, "network module: not enough memory (realloc returned NULL)\n");
    return 0;
  }

  memcpy(&(mem->memory[mem->size]), contents, realsize);
  mem->size += realsize;
  mem->memory[mem->size] = 0;
  
  return realsize;
}

static void DebugDump(const char *text, FILE *stream, unsigned char *ptr, size_t size, char nohex) {
  size_t i;
  size_t c;

  unsigned int width = 0x10;

  if (nohex)
    /* without the hex output, we can fit more on screen */
    width = 0x40;

  fprintf(stream, "%s, %10.10ld bytes (0x%8.8lx)\n", text, (long) size, (long) size);

  for (i = 0; i < size; i += width) {
    fprintf(stream, "%4.4lx: ", (long) i);

    if (!nohex) {
      /* hex not disabled, show it */
      for (c = 0; c < width; c++)
        if (i + c < size)
          fprintf(stream, "%02x ", ptr[i + c]);
        else
          fputs("   ", stream);
    }

    for (c = 0; (c < width) && (i + c < size); c++) {
      /* check for 0D0A; if found, skip past and start a new line of output */
      if (nohex && (i + c + 1 < size) && ptr[i + c] == 0x0D && ptr[i + c + 1] == 0x0A) {
        i += (c + 2 - width);
        break;
      }
      fprintf(stream, "%c", (ptr[i + c] >= 0x20) && (ptr[i + c] < 0x80) ? ptr[i + c] : '.');
      /* check again for 0D0A, to avoid an extra \n if it's at width */
      if (nohex && (i + c + 2 < size) && ptr[i + c + 1] == 0x0D && ptr[i + c + 2] == 0x0A) {
        i += (c + 3 - width);
        break;
      }
    }
    fputc('\n', stream); /* newline */
  }
  fflush(stream);
}

static int DebugCallback(CURL *handle, curl_infotype type, char *data, size_t size, void *userp) {
  struct data *config = (struct data *) userp;
  const char *text;
  (void) handle; /* prevent compiler warning */

  switch (type) {
    case CURLINFO_TEXT:
      fprintf(stderr, "== Info: %s", data);
    default: /* in case a new one is introduced to shock us */
      return 0;

    case CURLINFO_HEADER_OUT:
      text = "=> Send header";
      break;
    case CURLINFO_DATA_OUT:
      text = "=> Send data";
      break;
    case CURLINFO_SSL_DATA_OUT:
      text = "=> Send SSL data";
      break;
    case CURLINFO_HEADER_IN:
      text = "<= Recv header";
      break;
    case CURLINFO_DATA_IN:
      text = "<= Recv data";
      break;
    case CURLINFO_SSL_DATA_IN:
      text = "<= Recv SSL data";
      break;
  }
  DebugDump(text, stdout, (unsigned char *) data, size, config->trace_ascii);
  return 0;
}

int decodeURIComponent (char *sSource, char *sDest) {
  int nLength;
  for (nLength = 0; *sSource; nLength++) {
    if (*sSource == '%' && sSource[1] && sSource[2] && isxdigit(sSource[1]) && isxdigit(sSource[2])) {
      sSource[1] -= sSource[1] <= '9' ? '0' : (sSource[1] <= 'F' ? 'A' : 'a')-10;
      sSource[2] -= sSource[2] <= '9' ? '0' : (sSource[2] <= 'F' ? 'A' : 'a')-10;
      sDest[nLength] = 16 * sSource[1] + sSource[2];
      sSource += 3;
      continue;
    }
    sDest[nLength] = *sSource++;
  }
  sDest[nLength] = '\0';
  return nLength;
}

void extractTokenFromCookie(char *cookiedata) {
	char delimiter[] = "\x09";
	char *ptr;
	char temp[255];  //TODO make sizing dynamically to avoid potential overflow
	
	ptr = strtok(cookiedata, delimiter);
	
	bool tokenfound = FALSE;
	while (ptr != NULL) {
		vdc_report(LOG_DEBUG, "token %s\n", ptr);
		if (strcmp(ptr, ".ASPXAUTH") == 0) {
			ptr = strtok(NULL, delimiter);
			tokenfound = TRUE;
			break;
		}
		ptr = strtok(NULL, delimiter);
	}
	
	vdc_report(LOG_DEBUG, "token %s\n", ptr);

	if (tokenfound) {
		strcpy(temp,".ASPXAUTH=");
		strcat(temp, ptr);
		strcat(temp, ";");
    //check!!!
    if (klafs.aspxauth != NULL) {
      free(klafs.aspxauth);
    }
		klafs.aspxauth = strdup(temp);
	} else {
		vdc_report(LOG_ERR, "Authtoken in cookie not found");
	}	
}

void extractRequestVerificationToken(char *s) {
  char *sub = strstr(s, "RequestVerificationToken");
  char token[150];
  if(sub != NULL) {
    char *sub1 = strstr(sub,"value=");
    strncpy(token, sub1+7, 108);
    vdc_report(LOG_DEBUG, "RequestVerificationToken found: %s\n", token);
    //check!!!
    if (klafs.verificationtoken != NULL) {
      free(klafs.verificationtoken);
    }
    klafs.verificationtoken = strdup(token);
  }
}

struct memory_struct* http_post_get(bool post, const char *url, const char *htmldata, json_object *jsondata, const char *cookies) {
  CURL *curl;
  CURLcode res;
  struct memory_struct *chunk;

  chunk = malloc(sizeof(struct memory_struct));
  if (chunk == NULL) {
    vdc_report(LOG_ERR, "network: not enough memory\n");
    return NULL;
  }
  chunk->memory = malloc(1);
  chunk->size = 0;

  curl = curl_easy_init();
  if (curl == NULL) {
    vdc_report(LOG_ERR, "network: curl init failure\n");
    free(chunk->memory);
    free(chunk);
    return NULL;
  }
      
  struct curl_slist *headers = NULL;

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void * )chunk);
  if (post) {
    curl_easy_setopt(curl, CURLOPT_POST, 1);
  } else {
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1);
  }

  if (htmldata != NULL) {
    if (post) {
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, htmldata);
      curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long )strlen(htmldata));
    } else {
      char get_url[1024];
      strcpy(get_url, url);
      strcat(get_url, htmldata);
      curl_easy_setopt(curl, CURLOPT_URL, get_url);
    } 
    //headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded;charset=UTF-8");
  } else if(jsondata != NULL) {
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_object_to_json_string(jsondata));
    headers = curl_slist_append(headers, "Content-Type: application/json");
  } else {
    vdc_report(LOG_ERR, "network: post data missing");
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);  
    free(chunk->memory);
    free(chunk);
    
    return NULL;
  }

  curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/59.0.3071.71 Safari/537.36");  
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 42);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, FALSE);
  curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "");  
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

  if (cookies != NULL) {
    curl_easy_setopt(curl, CURLOPT_COOKIE, cookies);	
    vdc_report(LOG_ERR, cookies);
  }
  
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

  if (vdc_get_debugLevel() > LOG_DEBUG) {
    struct data config;
    config.trace_ascii = 1; /* enable ascii tracing */

    curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, DebugCallback);
    curl_easy_setopt(curl, CURLOPT_DEBUGDATA, &config);
    /* the DEBUGFUNCTION has no effect until we enable VERBOSE */
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
  }
  
  res = curl_easy_perform(curl);

  if (res != CURLE_OK) {
    vdc_report(LOG_ERR, "network: curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
    
    if (chunk != NULL) {
      if (chunk->memory != NULL) {
        free(chunk->memory);
      }
      free(chunk);
      chunk = NULL;
    }
  } else {
    //vdc_report(LOG_ERR, "Response: %s\n", chunk->memory);    // results in segmentation fault if response is too long
    res = curl_easy_getinfo(curl, CURLINFO_COOKIELIST, &cookielist);

    long response_code;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

    if (response_code == 403 || response_code == 404 || response_code == 503) {
      vdc_report(LOG_ERR, "Klafs server response: %d - ignoring response\n", response_code);
      if (chunk != NULL) {
        if (chunk->memory != NULL) {
          free(chunk->memory);
        }
        free(chunk);
        chunk = NULL;
      }
    } 
  }

  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  return chunk;
}

int parse_json_data(struct memory_struct *response) {
  bool changed_values = FALSE;
  time_t now;
    
  now = time(NULL);
  vdc_report(LOG_DEBUG, "network: klafs sauna values response = %s\n", response->memory);
  
  json_object *jobj = json_tokener_parse(response->memory);
  
  if (NULL == jobj) {
    vdc_report(LOG_ERR, "network: parsing json data failed, length %d, data:\n%s\n", response->size, response->memory);
    return KLAFS_GETMEASURE_FAILED;
  }

  //pthread_mutex_lock(&g_network_mutex);

  json_object_object_foreach(jobj, key, val) {
    enum json_type type = json_object_get_type(val);
    
    sensor_value_t* svalue;
    binary_value_t* bvalue;
    
    //save all relevant data rettrieved from klafs sauna API as current values in memory; in case of saving a scene, these values will be used to save as scene
    if (strcmp(key, "isPoweredOn") == 0) {
      sauna_current_values->isPoweredOn = json_object_get_boolean(val);
    } else if (strcmp(key, "saunaSelected") == 0)  {
      sauna_current_values->saunaSelected = json_object_get_boolean(val);
    } else if (strcmp(key, "sanariumSelected") == 0)  {
      sauna_current_values->sanariumSelected = json_object_get_boolean(val);
    } else if (strcmp(key, "irSelected") == 0)  {
      sauna_current_values->irSelected = json_object_get_boolean(val);
    } else if (strcmp(key, "selectedSaunaTemperature") == 0)  {
      sauna_current_values->selectedSaunaTemperature = json_object_get_int(val);
    } else if (strcmp(key, "selectedSanariumTemperature") == 0)  {
      sauna_current_values->selectedSanariumTemperature = json_object_get_int(val);
    } else if (strcmp(key, "selectedIrTemperature") == 0)  {
      sauna_current_values->selectedIrTemperature = json_object_get_int(val);
    } else if (strcmp(key, "selectedHumLevel") == 0)  {
      sauna_current_values->selectedHumLevel = json_object_get_int(val);
    } else if (strcmp(key, "selectedIrLevel") == 0)  {
      sauna_current_values->selectedIrLevel = json_object_get_int(val);
    } else if (strcmp(key, "selectedHour") == 0)  {
      sauna_current_values->selectedHour = json_object_get_int(val);
    } else if (strcmp(key, "selectedMinute") == 0)  {
      sauna_current_values->selectedMinute = json_object_get_int(val);
    } else if (strcmp(key, "bathingHours") == 0)  {
      sauna_current_values->bathingHours = json_object_get_int(val);
    } else if (strcmp(key, "bathingMinutes") == 0)  {
      sauna_current_values->bathingMinutes = json_object_get_int(val);
    }
    
    
    svalue = find_sensor_value_by_name(key);
    if (svalue == NULL) {
      bvalue = find_binary_value_by_name(key);
    }
    if (svalue == NULL && bvalue == NULL) {
      vdc_report(LOG_WARNING, "value %s is not configured for evaluation - ignoring\n", key);
    } else {
      if (type == json_type_int) {
        vdc_report(LOG_WARNING, "network: getmeasure returned %s: %d\n", key, json_object_get_int(val));
        
        if (svalue != NULL) {
          //if ((svalue->last_reported == 0) || (svalue->last_value != json_object_get_int(val)) || (now - svalue->last_reported) > 180) {
          if ((svalue->last_reported == 0) || (svalue->last_value != json_object_get_int(val))) {
            changed_values = TRUE;
          } 
          svalue->last_value = svalue->value;
          svalue->value = json_object_get_int(val);
          svalue->last_query = now;
        } else if (bvalue != NULL) {
          //if ((bvalue->last_reported == 0) || (bvalue->last_value != json_object_get_int(val)) || (now - bvalue->last_reported) > 180) {
          if ((bvalue->last_reported == 0) || (bvalue->last_value != json_object_get_int(val))) {
            changed_values = TRUE;
          } 
          bvalue->last_value = bvalue->value;
          bvalue->value = json_object_get_int(val);
          bvalue->last_query = now;
        }
      } else if (type == json_type_boolean) {
        vdc_report(LOG_WARNING, "network: getmeasure returned %s: %s\n", key, json_object_get_boolean(val)? "true": "false");
          
        if (svalue != NULL) {
          //if ((svalue->last_reported == 0) || (svalue->last_value != json_object_get_boolean(val)) || (now - svalue->last_reported) > 180) {
          if ((svalue->last_reported == 0) || (svalue->last_value != json_object_get_boolean(val))) {
           changed_values = TRUE;
          }
          svalue->last_value = svalue->value;
          svalue->value = json_object_get_boolean(val);
          svalue->last_query = now;
        } else if (bvalue != NULL) {
          //if ((bvalue->last_reported == 0) || (bvalue->last_value != json_object_get_boolean(val)) || (now - bvalue->last_reported) > 180) {
          if ((bvalue->last_reported == 0) || (bvalue->last_value != json_object_get_boolean(val))) {
            changed_values = TRUE;
          }
          bvalue->last_value = bvalue->value;
          bvalue->value = json_object_get_boolean(val);
          bvalue->last_query = now;
        }
      }
    }
  }

	pthread_mutex_unlock(&g_network_mutex);
  
  json_object_put(jobj);
   
  if (changed_values ) {
    return 0;
  } else return 1;
}

void klafs_validate_authcookie(char *aspxauth) {
  char request_body[1024];
  strcpy(request_body, "?id=");
  strcat(request_body, klafs.sauna.id);
  
  struct memory_struct *response = http_post_get(false, url_getsaunastatus, request_body, NULL, klafs.aspxauth);
  
  vdc_report(LOG_ERR, response->memory);
  
  if (response == NULL) {
    vdc_report(LOG_ERR, "network: trying sample request with auth cookie failed\n");
  } else {    
    if(strstr(response->memory, "\"LoginRequired\":true") != NULL) {    //seems the authcookie taken from config file does not work => get a new authcookie
      klafs_login();   
    }
  
    free(response->memory);
    free(response);
  }
}

int klafs_login() {
  vdc_report(LOG_ERR, "Klafs login calling\n");
  struct memory_struct *response = NULL;

  char request_body[1024];
  strcpy(request_body, "UserName=");
  strcat(request_body, klafs.username);
  strcat(request_body, "&Password=");
  strcat(request_body, klafs.password);
  

  response = http_post_get(true, url_login, request_body, NULL, NULL);
  
  extractRequestVerificationToken(response->memory);
   
  if (response == NULL) {
    vdc_report(LOG_ERR, "Klafs login failed\n");
    return KLAFS_AUTH_FAILED;
  }
  vdc_report(LOG_ERR, "Klafs login succeeded\n");

  if (cookielist) {
	 vdc_report(LOG_DEBUG, "%s\n", cookielist->data);
	 extractTokenFromCookie(cookielist->data);
  }

  free(response->memory);
  free(response);
    
  return KLAFS_OK;
}

bool is_scene_configured(int scene) {
  char scene_str[5];
  sprintf(scene_str, "-%d-", scene);
  if (strstr(klafs.sauna.configured_scenes, scene_str) != NULL) {
    return TRUE;
  } else return FALSE;
}

scene_t* get_scene_configuration(int scene) {
  scene_t *scene_data;
  int v;
  
  scene_data = malloc(sizeof(scene_t));
  if (!scene_data) {
    return NULL;
  }
  memset(scene_data, 0, sizeof(scene_t));
  
  v = 0;
  while (1) {
    if (&klafs.sauna.scenes[v] != NULL) {
      if (klafs.sauna.scenes[v].dsId == scene) {
        scene_data->isPoweredOn = klafs.sauna.scenes[v].isPoweredOn;
        scene_data->saunaSelected = klafs.sauna.scenes[v].saunaSelected;
        scene_data->sanariumSelected = klafs.sauna.scenes[v].sanariumSelected;
        scene_data->irSelected = klafs.sauna.scenes[v].irSelected;
        scene_data->selectedSaunaTemperature = klafs.sauna.scenes[v].selectedSaunaTemperature;
        scene_data->selectedSanariumTemperature = klafs.sauna.scenes[v].selectedSanariumTemperature;
        scene_data->selectedIrTemperature = klafs.sauna.scenes[v].selectedIrTemperature;
        scene_data->selectedHumLevel = klafs.sauna.scenes[v].selectedHumLevel;
        scene_data->selectedIrLevel = klafs.sauna.scenes[v].selectedIrLevel;
        scene_data->selectedHour = klafs.sauna.scenes[v].selectedHour;
        scene_data->selectedMinute = klafs.sauna.scenes[v].selectedMinute;
        scene_data->selectedHour = klafs.sauna.scenes[v].selectedHour;
        scene_data->selectedMinute = klafs.sauna.scenes[v].selectedMinute;
        scene_data->bathingHours = klafs.sauna.scenes[v].bathingHours;
        scene_data->bathingMinutes = klafs.sauna.scenes[v].bathingMinutes;
        break;
      }
      
      v++;
    } else {
      break;
    }
  }
    
  return scene_data;
}

int klafs_change_temperature(scene_t *scene_data) {
  struct memory_struct *response = NULL;

  json_object *json1; 

  json_object *jstring_saunaid = json_object_new_string(klafs.sauna.id);
  json_object *jstring_true = json_object_new_string("true");
  json_object *jstring_false = json_object_new_string("false");
    
  json1 = json_object_new_object();

  json_object_object_add(json1,"id", jstring_saunaid);
  json_object_object_add(json1,"temperature", json_object_new_int(scene_data->selectedSaunaTemperature));
  
  response = http_post_get(true, url_changeTemperature, NULL, json1, klafs.aspxauth);
  
  //free mem
  json_object_put(json1);
  
  if (response == NULL) {
    vdc_report(LOG_ERR, "Klafs config change failed\n");
    return KLAFS_CONFIGCHANGE_FAILED;
  } 
  
  if (strstr(response->memory,"security check") != NULL || strstr(response->memory,"Sicherheitskontrolle") != NULL) {
    vdc_report(LOG_NOTICE, "security control for sauna not done; cannot change values remotely!\n");
    return -1;
  }
  
  free(response->memory);
  free(response);
  
}

int klafs_change_humidity(scene_t *scene_data) {
  struct memory_struct *response = NULL;

  json_object *json1; 

  json_object *jstring_saunaid = json_object_new_string(klafs.sauna.id);
    
  json1 = json_object_new_object();

  json_object_object_add(json1,"id", jstring_saunaid);
  json_object_object_add(json1,"level", json_object_new_int(scene_data->selectedHumLevel));
  
  response = http_post_get(true, url_changeHumidity, NULL, json1, klafs.aspxauth);
  
  //free mem
  json_object_put(json1);
  
  if (response == NULL) {
    vdc_report(LOG_ERR, "Klafs config change failed\n");
    return KLAFS_CONFIGCHANGE_FAILED;
  } 
  
  if (strstr(response->memory,"security check") != NULL || strstr(response->memory,"Sicherheitskontrolle") != NULL) {
    vdc_report(LOG_NOTICE, "security control for sauna not done; cannot change values remotely!\n");
    return -1;
  }
  
  free(response->memory);
  free(response);
  
}

int klafs_change_mode(scene_t *scene_data) {
  struct memory_struct *response = NULL;

  json_object *json1; 
  
  json_object *jstring_saunaid = json_object_new_string(klafs.sauna.id);
  json_object *jstring_true = json_object_new_string("true");
  json_object *jstring_false = json_object_new_string("false");
 
  json1 = json_object_new_object();

  json_object_object_add(json1,"id", jstring_saunaid);

  if (scene_data->saunaSelected) json_object_object_add(json1, "selected_mode", json_object_new_int(1));
    else if (scene_data->sanariumSelected) json_object_object_add(json1, "selected_mode", json_object_new_int(2));
    else if (scene_data->irSelected) json_object_object_add(json1, "selected_mode", json_object_new_int(3));
  
  response = http_post_get(true, url_changeMode, NULL, json1, klafs.aspxauth);
  
  //free mem
  json_object_put(json1);
  
  if (response == NULL) {
    vdc_report(LOG_ERR, "Klafs config change failed\n");
    return KLAFS_CONFIGCHANGE_FAILED;
  } 
  
  if (strstr(response->memory,"security check") != NULL || strstr(response->memory,"Sicherheitskontrolle") != NULL) {
    vdc_report(LOG_NOTICE, "security control for sauna not done; cannot change values remotely!\n");
    return -1;
  }
  
  free(response->memory);
  free(response);  
}
  
int klafs_change_favoriteprogram(scene_t *scene_data) {
  struct memory_struct *response = NULL;

  json_object *json1; 

  json_object *jstring_saunaid = json_object_new_string(klafs.sauna.id);
  json_object *jstring_true = json_object_new_string("true");
  json_object *jstring_false = json_object_new_string("false");
    
  json1 = json_object_new_object();
  
  json_object_object_add(json1,"id", jstring_saunaid);
  
  //TODO call setmode
  klafs_change_mode(scene_data);
  
  if (scene_data->saunaSelected) json_object_object_add(json1,"temp", json_object_new_int(scene_data->selectedSaunaTemperature));
    else if (scene_data->sanariumSelected) json_object_object_add(json1,"temp", json_object_new_int(scene_data->selectedSanariumTemperature));
    else if (scene_data->irSelected) json_object_object_add(json1,"temp", json_object_new_int(scene_data->selectedIrTemperature));
  
  json_object_object_add(json1,"hum_level", json_object_new_int(scene_data->selectedHumLevel));
  json_object_object_add(json1,"ir_level", json_object_new_int(scene_data->selectedIrLevel));
  //json_object_object_add(json2,"showBathingHour", json_object_new_int(scene_data->showBathingHour));
  //json_object_object_add(json2,"bathingHours", json_object_new_int(scene_data->bathingHours));
  //json_object_object_add(json2,"bathingMinutes", json_object_new_int(scene_data->bathingMinutes));
  time_t rawtime;
  struct tm * timeinfo;

  time ( &rawtime );
  timeinfo = localtime ( &rawtime );

//  if (scene_data->selectedHour > -1) {
  //  json_object_object_add(json2,"selectedHour", json_object_new_int(scene_data->selectedHour));
//  } else {
  //  json_object_object_add(json2,"selectedHour", json_object_new_int(timeinfo->tm_hour));
//  }
  //if (scene_data->selectedMinute > -1) {
    //json_object_object_add(json2,"selectedMinute", json_object_new_int(scene_data->selectedMinute));
//  } else {
  //  json_object_object_add(json2,"selectedMinute", json_object_new_int(timeinfo->tm_min+5));
  //}
  
   
  response = http_post_get(true, url_changeFavoriteProgram, NULL, json1, klafs.aspxauth);
  
  //free mem
  json_object_put(json1);
  
  if (response == NULL) {
    vdc_report(LOG_ERR, "Klafs config change failed\n");
    return KLAFS_CONFIGCHANGE_FAILED;
  } 
  
  if (strstr(response->memory,"security check") != NULL || strstr(response->memory,"Sicherheitskontrolle") != NULL) {
    vdc_report(LOG_NOTICE, "security control for sauna not done; cannot change values remotely!\n");
    return -1;
  }
  
  free(response->memory);
  free(response);
}

int klafs_power_on() {
  vdc_report(LOG_NOTICE, "network: Power on sauna\n");
  
  struct memory_struct *response = NULL;
  
  json_object *json1; 

  json_object *jstring_saunaid = json_object_new_string(klafs.sauna.id);
  json_object *jstring_klafspin = json_object_new_string(klafs.pin);
  json_object *jstring_true = json_object_new_string("true");
  json_object *jstring_false = json_object_new_string("false");
    
  json1 = json_object_new_object();
  
  json_object_object_add(json1,"id", jstring_saunaid);
  json_object_object_add(json1,"pin", jstring_klafspin);
  json_object_object_add(json1,"time_selected", jstring_false);
  json_object_object_add(json1,"sel_hour", json_object_new_int(0));
  json_object_object_add(json1,"sel_min", json_object_new_int(0));
  
  response = http_post_get(true, url_startcabin, NULL, json1, klafs.aspxauth);

  if (response == NULL) {
    vdc_report(LOG_ERR, "network: power on sauna failed\n");
    return KLAFS_CONNECT_FAILED;
  }
  
  if (strstr(response->memory,"security check") != NULL || strstr(response->memory,"Sicherheitskontrolle") != NULL) {
    vdc_report(LOG_NOTICE, "security control for sauna not done; cannot power on remotely!\n");
    return -1;
  }
  
  free(response->memory);
  free(response);
  
  //get latest sauna values and do a immediate push to DSS; in case power on failed, e.g. security check not done in sauna, isPoweredOn stays "false"
  klafs_get_values();
  push_binary_input_states();   
  
  return 0;
}

int klafs_power_off() {
  vdc_report(LOG_NOTICE, "network: Power off sauna\n");

  char request_body[1024];
  strcpy(request_body, "id=");
  strcat(request_body, klafs.sauna.id);
  
  struct memory_struct *response = http_post_get(true, url_stopcabin, request_body, NULL, klafs.aspxauth);
  
  if (response == NULL) {
    vdc_report(LOG_ERR, "network: power off sauna values failed\n");
    return KLAFS_CONNECT_FAILED;
  }
  
  free(response->memory);
  free(response);
  
  //get latest sauna values and do a immediate push to DSS
  klafs_get_values();
  push_binary_input_states();
  
  return 0;
}

int klafs_get_values() {
  int rc;
  
  vdc_report(LOG_NOTICE, "network: reading Klafs Sauna values\n");

  char request_body[strlen(klafs.sauna.id)+4];
  strcpy(request_body, "?id=");
  strcat(request_body, klafs.sauna.id);
  
  
  struct memory_struct *response = http_post_get(false, url_getsaunastatus, request_body, NULL, klafs.aspxauth);
  
  if (response == NULL) {
    vdc_report(LOG_ERR, "network: getting sauna values failed\n");
    return KLAFS_CONNECT_FAILED;
  }
  
  rc = parse_json_data(response);
  
  free(response->memory);
  free(response);
  
  return rc;  
}

