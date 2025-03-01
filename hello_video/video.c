/*
Copyright (c) 2012, Broadcom Europe Ltd
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the copyright holder nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

// Video deocode demo using OpenMAX IL though the ilcient helper library

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include "bcm_host.h"
#include "ilclient.h"

COMPONENT_T *video_render = NULL;

static int video_decode_test(char *filename, int loop, int layer, int x, int y, int height, int width)
{
   OMX_VIDEO_PARAM_PORTFORMATTYPE format;
   OMX_TIME_CONFIG_CLOCKSTATETYPE cstate;
   COMPONENT_T *list[5];
   COMPONENT_T *video_decode = NULL, *video_scheduler = NULL, *clock = NULL;
   TUNNEL_T tunnel[4];
   ILCLIENT_T *client;
   FILE *in;
   int status = 0;
   unsigned int playcount = 0;
   unsigned int data_len = 0;

   memset(list, 0, sizeof(list));
   memset(tunnel, 0, sizeof(tunnel));

   if((in = fopen(filename, "rb")) == NULL)
      return -2;

   if((client = ilclient_init()) == NULL)
   {
      fclose(in);
      return -3;
   }

   if(OMX_Init() != OMX_ErrorNone)
   {
      ilclient_destroy(client);
      fclose(in);
      return -4;
   }

   // create video_decode
   if(ilclient_create_component(client, &video_decode, "video_decode", ILCLIENT_DISABLE_ALL_PORTS | ILCLIENT_ENABLE_INPUT_BUFFERS) != 0)
      status = -14;
   list[0] = video_decode;

   // create video_render
   if(status == 0 && ilclient_create_component(client, &video_render, "video_render", ILCLIENT_DISABLE_ALL_PORTS) != 0)
      status = -14;
   list[1] = video_render;

   // create clock
   if(status == 0 && ilclient_create_component(client, &clock, "clock", ILCLIENT_DISABLE_ALL_PORTS) != 0)
      status = -14;
   list[2] = clock;

   memset(&cstate, 0, sizeof(cstate));
   cstate.nSize = sizeof(cstate);
   cstate.nVersion.nVersion = OMX_VERSION;
   cstate.eState = OMX_TIME_ClockStateWaitingForStartTime;
   cstate.nWaitMask = 1;
   if(clock != NULL && OMX_SetParameter(ILC_GET_HANDLE(clock), OMX_IndexConfigTimeClockState, &cstate) != OMX_ErrorNone)
      status = -13;

   // create video_scheduler
   if(status == 0 && ilclient_create_component(client, &video_scheduler, "video_scheduler", ILCLIENT_DISABLE_ALL_PORTS) != 0)
      status = -14;
   list[3] = video_scheduler;

   set_tunnel(tunnel, video_decode, 131, video_scheduler, 10);
   set_tunnel(tunnel+1, video_scheduler, 11, video_render, 90);
   set_tunnel(tunnel+2, clock, 80, video_scheduler, 12);

   // set region and layer
   OMX_CONFIG_DISPLAYREGIONTYPE region;
   memset(&region, 0, sizeof(OMX_CONFIG_DISPLAYREGIONTYPE));
   region.nSize = sizeof(OMX_CONFIG_DISPLAYREGIONTYPE);
   region.nVersion.nVersion = OMX_VERSION;
   region.nPortIndex = 90;
   region.layer = layer;
   region.fullscreen = OMX_FALSE;
   region.dest_rect.x_offset = x;
   region.dest_rect.y_offset = y;
   region.dest_rect.height = height;
   region.dest_rect.width = width;
   region.set |= OMX_DISPLAY_SET_LAYER;
   // set destrect only if height and width are non-null
   if ( height && width ) {
     region.set |= OMX_DISPLAY_SET_DEST_RECT;
     region.set |= OMX_DISPLAY_SET_FULLSCREEN;
   }

   if( video_render != NULL &&
       OMX_SetParameter(ILC_GET_HANDLE(video_render), OMX_IndexConfigDisplayRegion, &region) != OMX_ErrorNone)
      status = -40;

   // setup clock tunnel first
   if(status == 0 && ilclient_setup_tunnel(tunnel+2, 0, 0) != 0)
      status = -15;
   else
      ilclient_change_component_state(clock, OMX_StateExecuting);

   if(status == 0)
      ilclient_change_component_state(video_decode, OMX_StateIdle);

   memset(&format, 0, sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE));
   format.nSize = sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE);
   format.nVersion.nVersion = OMX_VERSION;
   format.nPortIndex = 130;
   format.eCompressionFormat = OMX_VIDEO_CodingAVC;

   if(status == 0 &&
      OMX_SetParameter(ILC_GET_HANDLE(video_decode), OMX_IndexParamVideoPortFormat, &format) == OMX_ErrorNone &&
      ilclient_enable_port_buffers(video_decode, 130, NULL, NULL, NULL) == 0)
   {
      OMX_BUFFERHEADERTYPE *buf;
      int port_settings_changed = 0;
      int first_packet = 1;

      ilclient_change_component_state(video_decode, OMX_StateExecuting);

      while((buf = ilclient_get_input_buffer(video_decode, 130, 1)) != NULL)
      {
         // feed data and wait until we get port settings changed
         unsigned char *dest = buf->pBuffer;

         data_len += fread(dest, 1, buf->nAllocLen-data_len, in);

         if(port_settings_changed == 0 &&
            ((data_len > 0 && ilclient_remove_event(video_decode, OMX_EventPortSettingsChanged, 131, 0, 0, 1) == 0) ||
             (data_len == 0 && ilclient_wait_for_event(video_decode, OMX_EventPortSettingsChanged, 131, 0, 0, 1,
                                                       ILCLIENT_EVENT_ERROR | ILCLIENT_PARAMETER_CHANGED, 10000) == 0)))
         {
            port_settings_changed = 1;

            if(ilclient_setup_tunnel(tunnel, 0, 0) != 0)
            {
               status = -7;
               break;
            }

            ilclient_change_component_state(video_scheduler, OMX_StateExecuting);

            // now setup tunnel to video_render
            if(ilclient_setup_tunnel(tunnel+1, 0, 1000) != 0)
            {
               status = -12;
               break;
            }

            ilclient_change_component_state(video_render, OMX_StateExecuting);
         }
         if(!data_len) {
            // Finished reading the file, either loop or exit.
            if (loop<0) {
                fseek(in, 0, SEEK_SET);
            } else {
                playcount++;
                if (playcount<loop){
                   fseek(in, 0, SEEK_SET); 
                }else{
                    break;
                }
            }
         }

         buf->nFilledLen = data_len;
         data_len = 0;

         buf->nOffset = 0;
         if(first_packet)
         {
            buf->nFlags = OMX_BUFFERFLAG_STARTTIME;
            first_packet = 0;
         }
         else
            buf->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN;

         if(OMX_EmptyThisBuffer(ILC_GET_HANDLE(video_decode), buf) != OMX_ErrorNone)
         {
            status = -6;
            break;
         }
      }

      buf->nFilledLen = 0;
      buf->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN | OMX_BUFFERFLAG_EOS;

      if(OMX_EmptyThisBuffer(ILC_GET_HANDLE(video_decode), buf) != OMX_ErrorNone)
         status = -20;

      // wait for EOS from render
      ilclient_wait_for_event(video_render, OMX_EventBufferFlag, 90, 0, OMX_BUFFERFLAG_EOS, 0,
                              ILCLIENT_BUFFER_FLAG_EOS, -1);

      // need to flush the renderer to allow video_decode to disable its input port
      ilclient_flush_tunnels(tunnel, 0);

      ilclient_disable_port_buffers(video_decode, 130, NULL, NULL, NULL);
   }

   fclose(in);

   ilclient_disable_tunnel(tunnel);
   ilclient_disable_tunnel(tunnel+1);
   ilclient_disable_tunnel(tunnel+2);
   ilclient_teardown_tunnels(tunnel);

   ilclient_state_transition(list, OMX_StateIdle);
   ilclient_state_transition(list, OMX_StateLoaded);

   ilclient_cleanup_components(list);

   OMX_Deinit();

   ilclient_destroy(client);
   return status;
}

void error_usage(char* name) {
   printf("Usage: %s [--loop[=5]] <filename>\n", name);
   exit(1);
}

void sig_usr1(int signum){
  printf("PAUSE");
  ilclient_change_component_state(video_render, OMX_StatePause);
}

void sig_usr2(int signum){
  printf("RESUME");
  ilclient_change_component_state(video_render, OMX_StateExecuting);
}



int main (int argc, char **argv)
{
   int loop = 0;
   int layer = 1;
   int x = 0;
   int y = 0;
   int height = 0;
   int width = 0;

   signal(SIGUSR1,sig_usr1); // Register signal handler
   signal(SIGUSR2,sig_usr2);

   int oc;             /* option character */
   // char *b_opt_arg;

   while ((oc = getopt(argc, argv, "il:x:y:h:w:")) != -1) {
      switch (oc) {
      case 'i':
         /* -i: loop indefinitely */
         loop = -1;
         break;
      case 'l':
         /* -l: layer number  */
         layer = atoi(optarg);
         break;
      case 'x':
         x = atoi(optarg);
         break;
      case 'y':
         y = atoi(optarg);
         break;
      case 'h':
         height = atoi(optarg);
         break;
      case 'w':
         width = atoi(optarg);
         break;
      case ':':
          /* missing option argument */
          fprintf(stderr, "%s: option '-%c' requires an argument\n",
                  argv[0], optopt);
          break;
      case '?':
      default:
         /* invalid option */
         fprintf(stderr, "%s: option '-%c' is invalid: ignored\n",
                 argv[0], optopt);
         break;
      }
   }
 
   bcm_host_init();
   return video_decode_test(argv[argc-1], loop, layer, x, y, height, width);
}


