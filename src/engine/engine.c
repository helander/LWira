
#include "engine.h"

#include <spa/debug/types.h>
#include <spa/pod/iter.h>

#include "types.h"
#include "host.h"
#include "node.h"
#include "ui.h"
#include "stb_ds.h"
#include "ports.h"


static
void process_work_responses(Engine *engine)

   {
      struct spa_ringbuffer *ring = &engine->host.work_response_ring;
      uint8_t *buffer = engine->host.work_response_buffer;
      uint32_t read_index;
      uint32_t write_index;
      spa_ringbuffer_get_read_index(ring, &read_index);
      spa_ringbuffer_get_write_index(ring, &write_index);
      while ((write_index - read_index) >= sizeof(uint16_t)) {
         uint16_t msg_len;
         uint32_t offset = read_index & (WORK_RESPONSE_RINGBUFFER_SIZE - 1);
         uint32_t space = WORK_RESPONSE_RINGBUFFER_SIZE - offset;

         if (space >= sizeof(uint16_t)) {
            memcpy(&msg_len, buffer + offset, sizeof(uint16_t));
         } else {
            uint8_t tmp[2];
            memcpy(tmp, buffer + offset, space);
            memcpy(tmp + space, buffer, sizeof(uint16_t) - space);
            memcpy(&msg_len, tmp, sizeof(uint16_t));
         }

         if ((write_index - read_index) < sizeof(uint16_t) + msg_len) {
            break;  // Incomplete message
         }

         uint8_t payload[MAX_WORK_RESPONSE_MESSAGE_SIZE];
         offset = (read_index + sizeof(uint16_t)) & (WORK_RESPONSE_RINGBUFFER_SIZE - 1);
         space = WORK_RESPONSE_RINGBUFFER_SIZE - offset;

         if (space >= msg_len) {
            memcpy(payload, buffer + offset, msg_len);
         } else {
            memcpy(payload, buffer + offset, space);
            memcpy(payload + space, buffer, msg_len - space);
         }
         // printf("\nCall work_response from on_process");fflush(stdout);
        if (engine->host.iface && engine->host.iface->work_response)
            engine->host.iface->work_response(engine->host.handle, msg_len, payload);

         read_index += sizeof(uint16_t) + msg_len;
         spa_ringbuffer_read_update(ring, read_index);
      }
   }


static void on_process(void *userdata, struct spa_io_position *position) {
//   printf("   ONP   ");fflush(stdout);
   Engine *engine = userdata;

   uint32_t n_samples = position->clock.duration;
   uint64_t frame = engine->node.clock_time;
   float denom = (float)position->clock.rate.denom;
   engine->node.clock_time += position->clock.duration;

   for (int n = 0; n < arrlen(engine->ports); n++) {
      EnginePort *port = &engine->ports[n];
      if (port->pre_run) {
         port->pre_run(port, engine, frame, denom, (uint64_t)n_samples);
      }
   }

   lilv_instance_run(engine->host.instance, n_samples);

   process_work_responses(engine);
   if (engine->host.iface && engine->host.iface->end_run)
            engine->host.iface->end_run(engine->host.handle);

   for (int n = 0; n < arrlen(engine->ports); n++) {
      EnginePort *port = &engine->ports[n];
      if (port->post_run) port->post_run(port, engine);
   }
}

static void on_command(void *data, const struct spa_command *command) {
   Engine *engine = (Engine *)data;
   if (SPA_NODE_COMMAND_ID(command) == SPA_NODE_COMMAND_User) {
      if (SPA_POD_TYPE(&command->pod) == SPA_TYPE_Object) {
         const struct spa_pod_object *obj = (const struct spa_pod_object *)&command->pod;
         struct spa_pod_prop *prop;
         SPA_POD_OBJECT_FOREACH(obj, prop) {
            if (prop->key == SPA_COMMAND_NODE_extra) {
               const struct spa_pod *value = &prop->value;
               if (SPA_POD_TYPE(value) == SPA_TYPE_String) {
                  const char *command_string = SPA_POD_BODY(value);
                  char args[100];
                  if (sscanf(command_string, "preset %s", args) == 1) {
                     pw_loop_invoke(pw_thread_loop_get_loop(engine->node.master_loop), host_on_preset,
                                    0, args, strlen(args) + 1, false, engine);
                  } else if (sscanf(command_string, "save %s", args) == 1) {
                     pw_loop_invoke(pw_thread_loop_get_loop(engine->node.master_loop), host_on_save,
                                    0, args, strlen(args) + 1, false, engine);
                  } else {
                     printf("\nUnknown command [%s]", command_string);
                  }
               }
            }
         }
      }
   }
}

static void on_param_changed(void *data, void *port_data, uint32_t id,
                             const struct spa_pod *param) {
   printf("\nParam changed type %d  size %d", param->type, param->size);
   if (param->type == SPA_TYPE_Object) printf("\nobject");
   fflush(stdout);
}

static void on_filter_destroy(void *data) {
   Engine *engine = (Engine *)data;
   if (engine->node.filter) pw_filter_destroy(engine->node.filter);
}

const struct pw_filter_events engine_filter_events = {
    PW_VERSION_FILTER_EVENTS,
    .process = on_process,
    .command = on_command,
    .destroy = on_filter_destroy,
    .param_changed = on_param_changed,
};

void engine_defaults(Engine *engine) {
   engine->setname[0] = 0;
   engine->enginename[0] = 0;
   engine->plugin_uri[0] = 0;
   engine->preset_uri[0] = 0;
   engine->host.start_ui = true;
   engine->node.samplerate = 48000;
   engine->node.latency_period = 512;
}

#if 0
void node_destroy(struct node_data *node) {
   printf("\nTermination of node [%s]", node->nodename);
   fflush(stdout);
   pw_thread_loop_destroy(node->pw.node_loop);

   node->pw.node_loop = NULL;
   node->pw.filter = NULL;
   node->nodename[0] = 0;
   node->plugin_uri[0] = 0;
   node->preset_uri[0] = 0;
   node->host.lilv_preset = NULL;
   node->host.suil_instance = NULL;
}

#endif

static
void engine_ports_setup(Engine *engine) {
  engine->ports = NULL;
  for (int n = 0; n < arrlen(engine->node.ports); n++) {
    NodePort *node_port = &engine->node.ports[n];
    HostPort *host_port = NULL;
    for (int n = 0; n < arrlen(engine->host.ports); n++ ) {
       HostPort *port = &engine->host.ports[n];
       if (port->index == node_port->index) {
          host_port = port;
          break;
       }
    }
    printf("\nEP %s %d",host_port->name,node_port->type);fflush(stdout);
    EnginePort *engine_port = (EnginePort *) calloc(1,sizeof(EnginePort));
    engine_port->host_port = host_port;
    engine_port->node_port = node_port;
    switch(node_port->type) {
      case NODE_CONTROL_INPUT:
        engine_port->type = ENGINE_CONTROL_INPUT;
        engine_port->ringbuffer = calloc(1, ATOM_RINGBUFFER_SIZE);
        spa_ringbuffer_init(&engine_port->ring);
        engine_port->pre_run = pre_run_control_input;
        engine_port->post_run = post_run_control_input;
        break;
      case NODE_CONTROL_OUTPUT:
        engine_port->type = ENGINE_CONTROL_OUTPUT;
        engine_port->ringbuffer = calloc(1, ATOM_RINGBUFFER_SIZE);
        spa_ringbuffer_init(&engine_port->ring);
        engine_port->pre_run = pre_run_control_output;
        engine_port->post_run = post_run_control_output;
        break;
      case NODE_AUDIO_INPUT:
        engine_port->type = ENGINE_AUDIO_INPUT;
        engine_port->pre_run = pre_run_audio_input;
        engine_port->post_run = post_run_audio_input;
        break;
      case NODE_AUDIO_OUTPUT:
        engine_port->type = ENGINE_AUDIO_OUTPUT;
        engine_port->pre_run = pre_run_audio_output;
        engine_port->post_run = post_run_audio_output;
        break;
      default:
        free(engine_port);
        engine_port = NULL;
    }
    if (engine_port) arrput(engine->ports, *engine_port);

  }
}

int engine_entry(struct spa_loop *loop, bool async, uint32_t seq, const void *data, size_t size,
                 void *user_data) {
   Engine *engine = (Engine *)user_data;

   if (engine->started) {
      printf("\nAlready started engine %s in group %s", engine->enginename, engine->setname);
      fflush(stdout);
      return 0;
   }
   engine->started = true;
   engine->node.filter = NULL;
   engine->host.lilv_preset = NULL;
   engine->host.suil_instance = NULL;
   engine->node.engine_loop = pw_thread_loop_new("engine", NULL);
   pw_thread_loop_start(engine->node.engine_loop);

   printf("\nStarting engine %s in group %s\n\n", engine->enginename, engine->setname);
   fflush(stdout);

   host_setup(engine);
   node_setup(engine);
   engine_ports_setup(engine);

   lilv_instance_activate(engine->host.instance);  // create host_activate() and call it?

   // embed this in a function host_apply_preset (can we make host indep of engine and only
   // engine->host, we could then pass loop with the call)
   if (strlen(engine->preset_uri)) {
      pw_loop_invoke(pw_thread_loop_get_loop(engine->node.master_loop), host_on_preset, 0,
                     engine->preset_uri, strlen(engine->preset_uri), false, engine);
   }


      if (engine->host.start_ui)                                                                                                                                                                   
         pw_loop_invoke(pw_thread_loop_get_loop(engine->node.engine_loop), pluginui_on_start, 0, NULL,                                                                                               
                        0, false, engine);            

}
