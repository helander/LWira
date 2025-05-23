#include <string.h>
#include <stdlib.h>
#include "enginegroup.h"
#include "utils/cJSON.h"

void enginegroup_free(EngineGroup *group) {
   for (int i = 0; i < group->engine_count; ++i) {
      free(group->engines[i].name);
      free(group->engines[i].plugin);
      free(group->engines[i].preset);
   }
   free(group->engines);
}

EngineGroup *enginegroup_parse(char *jsonstring) {
   cJSON *cjson = cJSON_Parse(jsonstring);
   if (!cjson) return NULL;

   EngineGroup *enginegroup = calloc(1,sizeof(EngineGroup)); 

   cJSON *group = cJSON_GetObjectItem(cjson, "group");
   if (cJSON_IsString(group)) enginegroup->group = strdup(group->valuestring);

   cJSON *engines = cJSON_GetObjectItem(cjson, "engines");
   if (cJSON_IsArray(engines)) {
      int n_engines = cJSON_GetArraySize(engines);
      enginegroup->engines = calloc(n_engines, sizeof(EngineSpec));
      enginegroup->engine_count = n_engines;
      for (int i = 0; i < n_engines; ++i) {
         cJSON *engine = cJSON_GetArrayItem(engines, i);
         cJSON *name = cJSON_GetObjectItem(engine, "name");
         cJSON *plugin = cJSON_GetObjectItem(engine, "plugin");
         cJSON *preset = cJSON_GetObjectItem(engine, "preset");
         cJSON *showui = cJSON_GetObjectItem(engine, "showui");
         cJSON *worker = cJSON_GetObjectItem(engine, "worker");
         cJSON *engineworker = cJSON_GetObjectItem(engine, "engineworker");

         if (cJSON_IsString(name)) enginegroup->engines[i].name = strdup(name->valuestring);
         if (cJSON_IsString(plugin)) enginegroup->engines[i].plugin = strdup(plugin->valuestring);
         if (cJSON_IsString(preset)) enginegroup->engines[i].preset = strdup(preset->valuestring);
         if (cJSON_IsBool(showui)) enginegroup->engines[i].showui = cJSON_IsTrue(showui);
         if (cJSON_IsBool(worker)) enginegroup->engines[i].worker = cJSON_IsTrue(worker);
         if (cJSON_IsBool(engineworker)) enginegroup->engines[i].engineworker = cJSON_IsTrue(engineworker);
      }
   }

   cJSON_Delete(cjson);

   return enginegroup;
}
