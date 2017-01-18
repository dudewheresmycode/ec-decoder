#ifndef _WIN32
#include <unistd.h>
#define Sleep(x) usleep((x)*1000)
#endif


#include "decoder.h"

using namespace v8;
using namespace Nan;
using namespace node;

namespace extracast {
  int frameIndex;
  static int convert_tags                 = 0;
  static int show_value_unit              = 0;
  static int use_value_prefix             = 0;
  static int use_byte_value_binary_prefix = 0;
  static int use_value_sexagesimal_format = 0;

  static const char *unit_second_str          = "s"    ;
  static const char *unit_hertz_str           = "Hz"   ;
  static const char *unit_byte_str            = "byte" ;
  static const char *unit_bit_per_second_str  = "bit/s";
  static const char *binary_unit_prefixes [] = { "", "Ki", "Mi", "Gi", "Ti", "Pi" };
  static const char *decimal_unit_prefixes[] = { "", "K" , "M" , "G" , "T" , "P"  };


  #define MAX_AUDIOQ_SIZE (5 * 16 * 1024)
  #define MAX_VIDEOQ_SIZE (5 * 256 * 1024)
  #define VIDEO_PICTURE_QUEUE_SIZE 1
  #define SDL_AUDIO_BUFFER_SIZE 1024
  #define MAX_AUDIO_FRAME_SIZE 192000

  int width, height, owidth, oheight;
  YUVImage *yuv;
  double vpts;
  AVFormatContext *pFormatCtx = NULL;
  int             i, videoStream, audioStream;
  AVCodecContext  *pCodecCtxOrig = NULL;
  AVCodecContext  *pCodecCtx = NULL;
  AVCodec         *pCodec = NULL;
  AVFrame         *pFrame = NULL;
  AVFrame         *pFrameOut = NULL;
  AVFrame         *pFrameCopy = NULL;
  size_t          pFrameOutSize;
  size_t          pFrameCopySize;
  AVPacket        packet;

  //uint8_t *stream;
  AudioFrame  *audioData;

  uint8_t         audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
  unsigned int    audio_buf_size;
  unsigned int    audio_buf_index;
  AVFrame         audio_frame;
  AVPacket        audio_pkt;
  uint8_t         *audio_pkt_data;
  int             audio_pkt_size;
  AVCodecContext  *aCodecCtxOrig = NULL;
  AVCodecContext  *aCodecCtx = NULL;
  AVCodec         *aCodec = NULL;
  int             audioLen;


  AVPixelFormat   pix_fmt = AV_PIX_FMT_YUV420P; //AV_PIX_FMT_YUV420P; //AV_PIX_FMT_RGB24;
  int             frameFinished;
  int             shouldQuit;
  uint8_t *buffer;

  //YUVImage *bmp;
  struct SwsContext   *sws_ctx            = NULL;
  // AVDictionary        *videoOptionsDict   = NULL;

  // AVFrame         *avFrameYUV = NULL;
  // size_t          avFrameYUVSize;

  PacketQueue     videoq;
  PacketQueue     audioq;




  void packet_queue_init(PacketQueue *q) {
    memset(q, 0, sizeof(PacketQueue));
  }

  int packet_queue_put(PacketQueue *q, AVPacket *pkt) {


    if(av_dup_packet(pkt) < 0) {
      return -1;
    }
    AVPacketList *pkt1;
    pkt1 = (AVPacketList *)av_malloc(sizeof(AVPacketList));
    if (!pkt1)
      return -1;
    pkt1->pkt = *pkt;
    pkt1->next = NULL;

    //SDL_LockMutex(q->mutex);

    if (!q->last_pkt)
      q->first_pkt = pkt1;
    else
      q->last_pkt->next = pkt1;
    q->last_pkt = pkt1;
    q->nb_packets++;
    q->size += pkt1->pkt.size;
    //SDL_CondSignal(q->cond);

    //SDL_UnlockMutex(q->mutex);
    return 0;
  }
  static int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block)
  {
    AVPacketList *pkt1;
    int ret;

    //SDL_LockMutex(q->mutex);

    for(;;) {

      // if(global_video_state->quit) {
      //   ret = -1;
      //   break;
      // }

      pkt1 = q->first_pkt;
      if (pkt1) {
        q->first_pkt = pkt1->next;
        if (!q->first_pkt) q->last_pkt = NULL;
        q->nb_packets--;
        q->size -= pkt1->pkt.size;
        *pkt = pkt1->pkt;
        av_free(pkt1);
        ret = 1;
        break;
      } else if (!block) {
        fprintf(stderr, "no block\n");
        ret = 0;
        break;
      } else {
        fprintf(stderr, "sdl wait.. ?\n");
        Sleep(100); //wait 100ms? no SDL_CondWait here, so i put a random sleep?...?
        ret = 0; //????
        break; //????
        //SDL_CondWait(q->cond, q->mutex);
      }
    }
    //SDL_UnlockMutex(q->mutex);
    return ret;
  }

  int audio_decode_frame(AVCodecContext *aCodecCtx, uint8_t *audio_buf, int buf_size) {

    static AVPacket pkt;
    static uint8_t *audio_pkt_data = NULL;
    static int audio_pkt_size = 0;
    static AVFrame frame;

    int len1, data_size = 0;

    for(;;) {
      while(audio_pkt_size > 0) {
        int got_frame = 0;
        len1 = avcodec_decode_audio4(aCodecCtx, &frame, &got_frame, &pkt);
        if(len1 < 0) {
          /* if error, skip frame */
          audio_pkt_size = 0;
          break;
        }
        audio_pkt_data += len1;
        audio_pkt_size -= len1;
        data_size = 0;
        if(got_frame) {
  	       data_size = av_samples_get_buffer_size(NULL, aCodecCtx->channels, frame.nb_samples, aCodecCtx->sample_fmt, 1);
           assert(data_size <= buf_size);
           memcpy(audio_buf, frame.data[0], data_size);
        }
        if(data_size <= 0) {
  	       /* No data yet, get more frames */
  	      continue;
        }
        /* We have data, return it and come back for more later */
        return data_size;
      }
      if(pkt.data)
        av_free_packet(&pkt);

      if(shouldQuit) {
        return -1;
      }

      if(packet_queue_get(&audioq, &pkt, 1) < 0) {
        return -1;
      }
      audio_pkt_data = pkt.data;
      audio_pkt_size = pkt.size;
    }
  }
  // void audio_callback(void *userdata, uint8_t *stream, int len) {
  //
  //   AVCodecContext *aCodecCtx = (AVCodecContext *)userdata;
  //   int len1, audio_size;
  //
  //   //static uint8_t audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
  //   static unsigned int audio_buf_size = 0;
  //   static unsigned int audio_buf_index = 0;
  //
  //   while(len > 0) {
  //     if(audio_buf_index >= audio_buf_size) {
  //       /* We have already sent all our data; get more */
  //       audio_size = audio_decode_frame(aCodecCtx, audio_buf, sizeof(audio_buf));
  //       if(audio_size < 0) {
  // 	/* If error, output silence */
  // 	audio_buf_size = 1024; // arbitrary?
  // 	memset(audio_buf, 0, audio_buf_size);
  //       } else {
  // 	audio_buf_size = audio_size;
  //       }
  //       audio_buf_index = 0;
  //     }
  //     len1 = audio_buf_size - audio_buf_index;
  //     if(len1 > len)
  //       len1 = len;
  //     memcpy(stream, (uint8_t *)audio_buf + audio_buf_index, len1);
  //     len -= len1;
  //     stream += len1;
  //     audio_buf_index += len1;
  //   }
  // }
  //


  class AudioWorker : public AsyncWorker {
   public:
    AudioWorker(Callback *callback, int len)
      : AsyncWorker(callback), len(len) {}
    ~AudioWorker() {}

    void Execute () {
      audioData = new AudioFrame;
      //int len1, audio_size;
      //
      // //static uint8_t audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
      static unsigned int audio_buf_size = 0;
      static unsigned int audio_buf_index = 0;
      // //stream = (uint8_t *)malloc((size_t)len);
      // //audioLen = len;

      static AVPacket pkt;
      static uint8_t *audio_pkt_data = NULL;
      static int audio_pkt_size = 0;
      static AVFrame frame;

      int len1, data_size = 0;
      //AVCodecContext *aCodecCtx, uint8_t *audio_buf, int buf_size

      for(;;) {
        fprintf(stderr, "loop: \n");

        while(audio_pkt_size > 0) {
          int got_frame = 0;
          len1 = avcodec_decode_audio4(aCodecCtx, &frame, &got_frame, &pkt);
          if(len1 < 0) {
            /* if error, skip frame */
            audio_pkt_size = 0;
            break;
          }
          audio_pkt_data += len1;
          audio_pkt_size -= len1;
          data_size = 0;
          if(got_frame) {
    	       data_size = av_samples_get_buffer_size(NULL, aCodecCtx->channels, frame.nb_samples, aCodecCtx->sample_fmt, 1);
             //assert(data_size <= buf_size);
             fprintf(stderr, "audio size: %d\n", data_size);
             //memcpy(audioData->left, frame.data[0], data_size);
             audioData->left = frame.data[0];
             audioData->size_left = data_size;
          }
          if(data_size <= 0) {
    	       /* No data yet, get more frames */
    	      continue;
          }
          /* We have data, return it and come back for more later */
          //return data_size;
          break;
        }
        if(data_size > 0){
          break;
        }
        if(pkt.data)
          av_free_packet(&pkt);


        if(packet_queue_get(&audioq, &pkt, 1) < 0) {
          break;
        }
        audio_pkt_data = pkt.data;
        audio_pkt_size = pkt.size;
        fprintf(stderr, "audio_pkt_size: %d\n", audio_pkt_size);
      }


      //while(len > 0) {
        // if(audio_buf_index >= audio_buf_size) {
        //   /* We have already sent all our data; get more */
        //   audio_size = audio_decode_frame(aCodecCtx, audio_buf, sizeof(audio_buf));
        //   if(audio_size < 0) {
        //   	/* If error, output silence */
        //   	audio_buf_size = 1024; // arbitrary?
        //   	memset(audio_buf, 0, audio_buf_size);
        //   } else {
    	  //      audio_buf_size = audio_size;
        //   }
        //   audio_buf_index = 0;
        // }
        // len1 = audio_buf_size - audio_buf_index;
        // if(len1 > len){
        //   len1 = len;
        // }
        //memcpy(stream, (uint8_t *)audio_buf + audio_buf_index, len1);
        //memcpy(stream, audio_buf + audio_buf_index, len1);
        //memcpy(audioData->left, audio_buf + audio_buf_index, len1);
        //audioData->size_left = len1;
        //memcpy(audio->right, frame.data[1], data_size);

        //len -= 128;
        //audioData->left += len1;
        //audio_buf_index += len1;
      //}
    }

    void Destroy(){
      //avcodec_close(aCodecCtxOrig);
      //avcodec_close(aCodecCtx);
    }

    void HandleOKCallback(){
      v8::Local<v8::Value> argv[] = {
        Nan::CopyBuffer((char *)audioData->left, audioData->size_left).ToLocalChecked(),
        //Nan::CopyBuffer((char *)audioData->right, audioData->size_right).ToLocalChecked()
        //Nan::New<Integer>(5),
        Nan::New<Integer>(5)
      };
      callback->Call(2, argv);
      delete audioData;
    }

   private:
     int len;
    //int milliseconds;
  };

  int pow2roundup (int x){
    if (x < 0)
        return 0;
    --x;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    return x+1;
  }

  void extractYUV(){

    yuv->pitchY = pFrameOut->linesize[0];
    yuv->pitchU = pFrameOut->linesize[1];
    yuv->pitchV = pFrameOut->linesize[2];

    yuv->avY = pFrameOut->data[0];
    yuv->avU = pFrameOut->data[1];
    yuv->avV = pFrameOut->data[2];

    // if(yuv->pitchU % 2 != 0){
    //   width+=2;
    //   yuv->pitchY+=2;
    //   yuv->pitchU+=1;
    //   yuv->pitchV+=1;
    // }


    yuv->size_y = (yuv->pitchY * oheight);
    yuv->size_u = (yuv->pitchU * oheight / 2);
    yuv->size_v = (yuv->pitchV * oheight / 2);

  }

  class QueueWorker : public AsyncWorker {
   public:
    QueueWorker(Callback *callback)
      : AsyncWorker(callback) {}
    ~QueueWorker() {}

    void Execute () {
      //Sleep(1000);


      if(videoq.nb_packets > 0){

        AVPacket pkt1, *packet = &pkt1;
        int frameFinished;



        pFrame = av_frame_alloc();
        yuv = new YUVImage;

        int i=0;
        for(;;) {
          if(shouldQuit){
            fprintf(stderr, "quit ivoked!\n");
            break;
          }
          if(packet_queue_get(&videoq, packet, 1) < 0) {
            // means we quit getting packets
            fprintf(stderr, "quit getting packets!\n");
            break;
          }
          //fprintf(stderr, "decode...%d\n", packet);

          vpts=0;

          avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, packet);

          if((vpts = av_frame_get_best_effort_timestamp(pFrame)) == AV_NOPTS_VALUE) {
            vpts = 0;
          }
          vpts *= av_q2d(pFormatCtx->streams[videoStream]->time_base);


          if(frameFinished) {
            int color[3] = { 255, 0, 255 };
            //av_picture_pad((AVPicture *)pFrameCropped, (AVPicture *)pFrame, height, width, pix_fmt, 0, 0, 0, 0, color);
            //fprintf(stderr, "decoded !\n");
            //av_picture_crop((AVPicture *)pFrameCropped, (AVPicture *)pFrame, pix_fmt, height, width);

            //pFrameCopySize = avpicture_get_size(pix_fmt, owidth, oheight);
            //pFrameOutSize = avpicture_get_size(pix_fmt, owidth, oheight);


            sws_scale(sws_ctx, (uint8_t const * const *)pFrame->data, pFrame->linesize, 0, height, pFrameOut->data, pFrameOut->linesize);
            //av_picture_pad((AVPicture *)pFrameOut, (AVPicture *)pFrameCopy, oheight, owidth, pix_fmt, 0, 0, 0, (owidth - width), color);

            extractYUV();
            //got a frame.. call it back now
            break;
          }
          //no frame found.. free this packet and loop again ...
          av_free_packet(packet);
          // if(i >= 10){
          //   break;
          // }
          i++;

        }
        av_frame_free(&pFrame);
      }else{
        fprintf(stderr, "no more packets...\n");
      }


    }
    // void Destroy(){
    //   // Close the codecs
    //   avcodec_close(pCodecCtxOrig);
    //   // Close the codec
    //   avcodec_close(pCodecCtx);
    // }
    void HandleOKCallback(){
      v8::Local<v8::Value> argv[] = {
        Nan::CopyBuffer((char *)yuv->avY, yuv->size_y).ToLocalChecked(),
        Nan::CopyBuffer((char *)yuv->avU, yuv->size_u).ToLocalChecked(),
        Nan::CopyBuffer((char *)yuv->avV, yuv->size_v).ToLocalChecked(),
        Nan::New<Integer>(yuv->pitchY),
        Nan::New<Integer>(yuv->pitchU),
        Nan::New<Integer>(yuv->pitchV),
        Nan::New<Integer>(videoq.nb_packets),
        Nan::New<Number>(vpts)
      };
      callback->Call(8, argv);
      delete yuv;
    }

   private:
    //int milliseconds;
  };

  class DecodeWorker : public AsyncProgressWorker {
   public:
    DecodeWorker(
        Callback *callback
      , Callback *progress)
      : AsyncProgressWorker(callback), progress(progress) {}
    ~DecodeWorker() {}


    void Execute (const AsyncProgressWorker::ExecutionProgress& progress) {
      //video stuff

      // Find the decoder for the video stream
      pCodec=avcodec_find_decoder(pCodecCtxOrig->codec_id);
      if(pCodec==NULL) {
        fprintf(stderr, "Unsupported codec!\n");
        exit(-1);
      }

      // Copy context
      pCodecCtx = avcodec_alloc_context3(pCodec);
      if(avcodec_copy_context(pCodecCtx, pCodecCtxOrig) != 0) {
        fprintf(stderr, "Couldn't copy codec context");
        exit(-1);
      }

      // Open codec
      if(avcodec_open2(pCodecCtx, pCodec, NULL)<0){
        fprintf(stderr, "find videoStream failed\n");
      }


      // Allocate video frame
      //pFrame = av_frame_alloc();
      //avFrameYUV
      pFrameCopy = av_frame_alloc();
      pFrameOut = av_frame_alloc();

      int size = avpicture_get_size(pix_fmt, owidth, oheight);
      uint8_t* buffer = (uint8_t*)av_malloc(size);

      int sizeout = avpicture_get_size(pix_fmt, owidth, oheight);
      uint8_t* bufferout = (uint8_t*)av_malloc(sizeout);

      avpicture_fill((AVPicture *)pFrameCopy, buffer, pix_fmt, owidth, oheight);
      avpicture_fill((AVPicture *)pFrameOut, bufferout, pix_fmt, owidth, oheight);


      // initialize SWS context for software scaling
      sws_ctx = sws_getContext(
           owidth,
           oheight,
           pCodecCtx->pix_fmt,
           owidth,
           oheight,
           pix_fmt,
           SWS_BILINEAR,
           NULL,
           NULL,
           NULL
      );
      //pCodecCtx->get_buffer2 = our_get_buffer;
      //pCodecCtx->release_buffer = our_release_buffer;

      //v8::Local<v8::Object> em = UnwrapPointer(r->emitter);

      AVPacket pkt1, *packet = &pkt1;
      frameIndex=0;
      for(;;) {
        // seek stuff goes here
        //if(audioq.size > MAX_AUDIOQ_SIZE || videoq.size > MAX_VIDEOQ_SIZE) {
        if(videoq.size > MAX_VIDEOQ_SIZE) {
          //fprintf(stderr, "max queue size.. waiting 10ms %d\n", i);
          Sleep(100); //sleep for a second?
          continue;
        }

        if(av_read_frame(pFormatCtx, packet) < 0) {
          if(pFormatCtx->pb->error == 0) {
            //fprintf(stderr, "no error.. sleeping %d\n", i);
            continue;
    	       //Sleep(100); /* no error; wait for user input */
             //continue;
          } else {
    	       break;
          }
        }

        // Is this a packet from the video stream?
        if(packet->stream_index == videoStream) {
          frameIndex++;
          packet_queue_put(&videoq, packet);
        // } else if(packet->stream_index == is->audioStream) {
        //   packet_queue_put(&is->audioq, packet);
        } else if(packet->stream_index == audioStream) {
          packet_queue_put(&audioq, packet);
        } else {
          //fprintf(stderr, "free: %d\n", i);
          av_free_packet(packet);
        }
        progress.Signal();
        //fprintf(stderr, "frame: %d\n", frameIndex);
        //progress.Send(reinterpret_cast<const char*>(&i), sizeof(int));

      }

      //delete yuv;
    }
    void HandleOKCallback(){
      v8::Local<v8::Value> argv[] = {
        Nan::New<Integer>(videoq.nb_packets)
      };
      progress->Call(1, argv);
    }
    void HandleProgressCallback(const char *data, size_t size) {
      Nan::HandleScope scope;
      v8::Local<v8::Value> argv[] = {
        Nan::New<Integer>(videoq.nb_packets)
      };
      // v8::Local<v8::Value> argv[] = {
      //   //Nan::New("ping").ToLocalChecked(), // event name
      //   Nan::CopyBuffer((char *)yuv->avY, yuv->size_y).ToLocalChecked(),
      //   Nan::CopyBuffer((char *)yuv->avU, yuv->size_u).ToLocalChecked(),
      //   Nan::CopyBuffer((char *)yuv->avV, yuv->size_v).ToLocalChecked(),
      //   Nan::New<Integer>(yuv->pitchY),
      //   Nan::New<Integer>(yuv->pitchU),
      //   Nan::New<Integer>(yuv->pitchV)
      //   //Nan::New<Integer>(width),
      //   //Nan::New<Integer>(height)
      // };
      progress->Call(1, argv);
    }

   private:
    // Callback *callback;
    Callback *progress;
    //const char *inputPath;
  };


  static char *time_value_string(char *buf, int buf_size, int64_t val){
    if (val == AV_NOPTS_VALUE) {
      snprintf(buf, buf_size, "N/A");
    } else {
      value_string(buf, buf_size, val * av_q2d(AV_TIME_BASE_Q), unit_second_str);
    }
    return buf;
  }

  static char *value_string(char *buf, int buf_size, double val, const char *unit){
      if (unit == unit_second_str && use_value_sexagesimal_format) {
          double secs;
          int hours, mins;
          secs  = val;
          mins  = (int)secs / 60;
          secs  = secs - mins * 60;
          hours = mins / 60;
          mins %= 60;
          snprintf(buf, buf_size, "%d:%02d:%09.6f", hours, mins, secs);
      } else if (use_value_prefix) {
          const char *prefix_string;
          int index;

          if (unit == unit_byte_str && use_byte_value_binary_prefix) {
              index = (int) (log(val)/log(2)) / 10;
              index = av_clip(index, 0, FF_ARRAY_ELEMS(binary_unit_prefixes) -1);
              val /= pow(2, index*10);
              prefix_string = binary_unit_prefixes[index];
          } else {
              index = (int) (log10(val)) / 3;
              index = av_clip(index, 0, FF_ARRAY_ELEMS(decimal_unit_prefixes) -1);
              val /= pow(10, index*3);
              prefix_string = decimal_unit_prefixes[index];
          }

          snprintf(buf, buf_size, "%.3f %s%s", val, prefix_string, show_value_unit ? unit : "");
      } else {
          snprintf(buf, buf_size, "%f %s", val, show_value_unit ? unit : "");
      }

      return buf;
  }

  // AVCodecContext  *aCodecCtxOrig = NULL;
  // AVCodecContext  *aCodecCtx = NULL;
  // AVCodec         *aCodec = NULL;


  //init function
  NAN_METHOD(ec_config) {

    //String::Utf8Value cmd(info[0]);

    // char *input = (char*)(*cmd);
    // strcpy(inputFile, input);
    //
    // //input->path = inputFile;
    // fprintf(stderr, "Decode: %s\n", inputFile);
    // int rtn = 0;

    //Nan::MaybeLocal<v8::Object> wrapper = WrapPointer((char *)request);
    //info.GetReturnValue().Set(wrapper.ToLocalChecked());
    info.GetReturnValue().Set(Nan::New<String>("OK").ToLocalChecked());
  }

  Emitter* self;

  NAN_METHOD(Emitter::New) {
    assert(info.IsConstructCall());
    self = new Emitter();
    self->Wrap(info.This());

    info.GetReturnValue().Set(info.This());
  }

  NAN_METHOD(Emitter::Open) {
    char val_str[128];

    String::Utf8Value cmd(info[0]);
    char *in = (*cmd);
    //char *input = (char *)malloc(strlen(in) * sizeof(char));
    //strcpy(input, in);

    //Callback *callback = new Callback(info[1].As<v8::Function>());
    Callback *callback = new Callback(info[1].As<Function>());

    packet_queue_init(&videoq);


    fprintf(stderr, "Open: %s\n", in);
    // Open video file
    if(avformat_open_input(&pFormatCtx, in, NULL, NULL)!=0){
      fprintf(stderr, "Could not open %s\n", in);
      exit(-1);
    }

    // Retrieve stream information
    if(avformat_find_stream_info(pFormatCtx, NULL)<0){
      fprintf(stderr, "avformat_find_stream_info failed\n");
      exit(-1);
    }

    av_dump_format(pFormatCtx, 0, in, 0);


    videoStream=-1;
    audioStream=-1;
    for(i=0; i<pFormatCtx->nb_streams; i++) {
      if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_VIDEO && videoStream < 0) {
        videoStream=i;
      }
      if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_AUDIO &&
         audioStream < 0) {
        audioStream=i;
      }
    }
    if(videoStream==-1){
      fprintf(stderr, "find videoStream failed\n");
      exit(-1);
    }
    if(audioStream==-1){
      fprintf(stderr, "find audioStream failed\n");
    }

    pCodecCtxOrig=pFormatCtx->streams[videoStream]->codec;

    aCodecCtxOrig=pFormatCtx->streams[audioStream]->codec;

    width = pCodecCtxOrig->width;
    height = pCodecCtxOrig->height;



    //audio stuff
    aCodec = avcodec_find_decoder(aCodecCtxOrig->codec_id);
    if(!aCodec) {
      fprintf(stderr, "Unsupported codec!\n");
      exit(-1);
    }

    // Copy context
    aCodecCtx = avcodec_alloc_context3(aCodec);
    if(avcodec_copy_context(aCodecCtx, aCodecCtxOrig) != 0) {
      fprintf(stderr, "Couldn't copy codec context");
      exit(-1); // Error copying codec context
    }

    avcodec_open2(aCodecCtx, aCodec, NULL);
    packet_queue_init(&audioq);



    float aspect_ratio;
    if(pCodecCtxOrig->sample_aspect_ratio.num == 0) {
      aspect_ratio = 0;
    } else {
      aspect_ratio = av_q2d(pCodecCtxOrig->sample_aspect_ratio) * pCodecCtxOrig->width / pCodecCtxOrig->height;
    }
    if(aspect_ratio <= 0.0) {
      aspect_ratio = (float)pCodecCtxOrig->width / (float)pCodecCtxOrig->height;
    }

    //attempt to fix power-of-two webgl issues with odd halfed U/V strides
    //owidth = pow2roundup(width);
    owidth = pCodecCtxOrig->coded_width;
    oheight = pCodecCtxOrig->coded_height;

    // int whalf = width / 2;
    // if(owidth % 8 != 0)
    //   owidth = (ceil(owidth / 8) * 8);
    //   oheight = height + 2;

    //if(whalf % 4 != 0)
    //  owidth = (whalf + 1) * 2;

    // "width": 1280,
    // "height": 720,
    // "coded_width": 1280,
    // "coded_height": 720,
    fprintf(stderr, "codec: %dx%d %dx%d\n", pCodecCtxOrig->width, pCodecCtxOrig->height, pCodecCtxOrig->coded_width, pCodecCtxOrig->coded_height);
    fprintf(stderr, "orig: %dx%d %dx%d\n", width, height, owidth, oheight);
    //   //pow2roundup()
    //   // int nw = width - 2;
    //   int nw = (whalf + 1) * 2; //(ceil(whalf/2)*2)*2;
    //   //int nh = ceil((height * (nw / width))/2)*2;
    //   //fprintf(stderr, "new width %d\n", nw);
    //   int nh = floor(floor((float)nw / aspect_ratio)/2)*2;
    //   //fprintf(stderr, "new height %d\n", nh);
    //   owidth = nw;
    //   //oheight = nh;
    // }

    //oheight = round(round((float)owidth / aspect_ratio)/2)*2;

    // //width = 856;
    // fprintf(stderr, "width is now %d, height is now: %d\n", width, height);
    // int hhalf = width/2;
    // if(whalf % 2 != 0){
    //   height = (ceil(whalf/2) * 2) * 2;
    // }


    // int screen_w = pCodecCtxOrig->width;
    // int screen_h = pCodecCtxOrig->width;
    // int w, h, x, y;
    //
    // h = screen_h;
    // w = ((int)rint(h * aspect_ratio)) & -3;
    // if(w > screen_w) {
    //   w = screen_w;
    //   h = ((int)rint(w / aspect_ratio)) & -3;
    // }
    // x = (screen_w - w) / 2;
    // y = (screen_h - h) / 2;
    //
    // rect.x = x;
    // rect.y = y;
    // rect.w = w;
    // rect.h = h;
    // Local<Object> pictureObj = Nan::New<Object>();
    // pictureObj->Set(Nan::New<String>("screen_w").ToLocalChecked(), Nan::New<Integer>(screen_w));
    // pictureObj->Set(Nan::New<String>("screen_h").ToLocalChecked(), Nan::New<Integer>(screen_h));
    // pictureObj->Set(Nan::New<String>("rect_w").ToLocalChecked(), Nan::New<Integer>(w));
    // pictureObj->Set(Nan::New<String>("rect_h").ToLocalChecked(), Nan::New<Integer>(h));
    // pictureObj->Set(Nan::New<String>("rect_x").ToLocalChecked(), Nan::New<Integer>(x));
    // pictureObj->Set(Nan::New<String>("rect_y").ToLocalChecked(), Nan::New<Integer>(y));



    Local<Object> obj = Nan::New<Object>();
    obj->Set(Nan::New<String>("filename").ToLocalChecked(), Nan::New<String>(pFormatCtx->filename).ToLocalChecked());
    obj->Set(Nan::New<String>("duration").ToLocalChecked(), Nan::New<String>(time_value_string(val_str, sizeof(val_str), pFormatCtx->duration)).ToLocalChecked());
    obj->Set(Nan::New<String>("width").ToLocalChecked(), Nan::New<Integer>(width));
    obj->Set(Nan::New<String>("height").ToLocalChecked(), Nan::New<Integer>(height));
    obj->Set(Nan::New<String>("coded_width").ToLocalChecked(), Nan::New<Integer>(owidth));
    obj->Set(Nan::New<String>("coded_height").ToLocalChecked(), Nan::New<Integer>(oheight));
    obj->Set(Nan::New<String>("aspect_ratio").ToLocalChecked(), Nan::New<Number>(aspect_ratio));
    //obj->Set(Nan::New<String>("time_base").ToLocalChecked(), Nan::New<String>(pFormatCtx->streams[videoStream]->time_base).ToLocalChecked());
    //obj->Set(Nan::New<String>("r_frame_rate").ToLocalChecked(), Nan::New<String>(pFormatCtx->streams[videoStream]->r_frame_rate).ToLocalChecked());
    //obj->Set(Nan::New<String>("avg_frame_rate").ToLocalChecked(), Nan::New<String>(pFormatCtx->streams[videoStream]->avg_frame_rate).ToLocalChecked());
    obj->Set(Nan::New<String>("nb_frames").ToLocalChecked(), Nan::New<Number>(pFormatCtx->streams[videoStream]->nb_frames));


    obj->Set(Nan::New<String>("sample_rate").ToLocalChecked(), Nan::New<Integer>(aCodecCtx->sample_rate));
    obj->Set(Nan::New<String>("channels").ToLocalChecked(), Nan::New<Integer>(aCodecCtx->channels));
    obj->Set(Nan::New<String>("samples").ToLocalChecked(), Nan::New<Integer>(SDL_AUDIO_BUFFER_SIZE));
    //obj->Set(Nan::New<String>("picture").ToLocalChecked(), pictureObj);
//    wanted_spec.userdata = aCodecCtx;

  //   // return object
    // info.GetReturnValue().Set(obj);
    //IncodeInput *meta = new IncodeInput;

    Nan:: HandleScope scope;
    Local<Value> argv[] = {
        // Nan::New(pFormatCtx->filename).ToLocalChecked(),
        // Nan::New(time_value_string(val_str, sizeof(val_str), pFormatCtx->duration)).ToLocalChecked(),
        // Nan::New(pCodecCtxOrig->width),
        // Nan::New(pCodecCtxOrig->height),
        // Nan::New(pFormatCtx->iformat->name).ToLocalChecked(),
        // Nan::New(pFormatCtx->iformat->long_name).ToLocalChecked(),
        obj
    };

    // Nan::Call(callback->GetFunction(), GetCurrentContext()->Global(), 1, argv);//callback->Call(1, argv);

    callback->Call(1, argv);

    //uv_queue_work(uv_default_loop(), &request->req, ec_quick_probe, (uv_after_work_cb)ec_decode_buffer_after);

    //AsyncQueueWorker(new DecodeWorker(callback, input));
  }

  // emits ping event
  NAN_METHOD(Emitter::Decode) {

//    DecodeRequest *request = new DecodeRequest;
    //request->input = input;
//    request->req.data = request;

    //DecodeRequest *r = reinterpret_cast<DecodeRequest *>(request->data);
    //int frameIndex = info[0]->Uint32Value();

    //request->callback.Reset(info[0].As<Function>());

    Callback *progress = new Callback(info[0].As<v8::Function>());
    Callback *callback = new Callback(info[1].As<v8::Function>());


    //!!uv_queue_work(uv_default_loop(), &request->req, ec_decode_buffer_async, (uv_after_work_cb)ec_decode_buffer_after);

    AsyncQueueWorker(new DecodeWorker(callback, progress));


  }

  NAN_METHOD(Emitter::ReadFrame) {

    Callback *callback = new Callback(info[0].As<v8::Function>());
    AsyncQueueWorker(new QueueWorker(callback));

  }
  NAN_METHOD(Emitter::ReadAudio) {

    //int len = Nan::New<Integer>(info[0]);
    int len = info[0]->Uint32Value();

    Callback *callback = new Callback(info[1].As<v8::Function>());

    AsyncQueueWorker(new AudioWorker(callback, len));

  }


  // NAN_METHOD(ec_decode_start) {
  //
  //   String::Utf8Value cmd(info[0]);
  //   char *in = (*cmd);
  //   char *input = (char *)malloc(strlen(in) * sizeof(char));
  //   strcpy(input, in);
  //
  //   Callback *callback = new Callback(info[1].As<v8::Function>());
  //   AsyncQueueWorker(new DecodeWorker(callback, input));
  // }


  NAN_METHOD(ec_decode_frame) {

    DecodeRequest *request = new DecodeRequest;
    //request->input = input;
    request->req.data = request;

    //DecodeRequest *r = reinterpret_cast<DecodeRequest *>(request->data);
    int frameIndex = info[0]->Uint32Value();
    request->callback.Reset(info[1].As<Function>());

    // set a circular pointer so we can get the "encode_req" back later
    fprintf(stderr, "get buffered frames: %d\n", frameIndex);

  }




  void ec_decode_buffer_async (uv_work_t *req) {
    DecodeRequest *r = (DecodeRequest *)req->data;

  }


  void ec_decode_buffer_after (uv_work_t *req) {
    Nan::HandleScope scope;
    DecodeRequest *r = (DecodeRequest *)req->data;

    Nan::TryCatch try_catch;


    //flush last frame
    v8::Local<v8::Value> argv[] = {
      Nan::CopyBuffer((char *)yuv->avY, yuv->size_y).ToLocalChecked(),
      Nan::CopyBuffer((char *)yuv->avU, yuv->size_u).ToLocalChecked(),
      Nan::CopyBuffer((char *)yuv->avV, yuv->size_v).ToLocalChecked(),
      Nan::New<Integer>(yuv->pitchY),
      Nan::New<Integer>(yuv->pitchU),
      Nan::New<Integer>(yuv->pitchV)
      //Nan::New<Integer>(owidth),
      //Nan::New<Integer>(oheight)
    };

    //Nan::New(r->callback)->Call(Nan::GetCurrentContext()->Global(), 8, ((v8::Local<v8::Value>)argv));
    Nan::New(r->callback)->Call(Nan::GetCurrentContext()->Global(), 8, argv);

    // cleanup
    r->callback.Reset();
    delete r;

    if (try_catch.HasCaught()) {
      FatalException(try_catch);
    }

  }
  NAN_METHOD(ec_teardown){
    // Close the codecs
    avcodec_close(pCodecCtxOrig);
    // Close the codec
    avcodec_close(pCodecCtx);
    avcodec_close(aCodecCtxOrig);
    avcodec_close(aCodecCtx);

    // Close the video file
    avformat_close_input(&pFormatCtx);

    info.GetReturnValue().Set(Nan::New<String>("OK").ToLocalChecked());
  }
  void ec_decode_buffer_flush (uv_work_t *req) {

  }
  NAN_METHOD(ec_flush){

    info.GetReturnValue().Set(Nan::New<String>("OK").ToLocalChecked());
  }


  void ec_decoder_init(Handle<Object> target){
    Nan::HandleScope scope;

    av_register_all();

    Nan::SetMethod(target, "config", ec_config);
    Nan::SetMethod(target, "frame", ec_decode_frame);
    //Nan::SetMethod(target, "start", ec_decode_start);
    Nan::SetMethod(target, "destroy", ec_teardown);

    Local<FunctionTemplate> t = Nan::New<FunctionTemplate>(Emitter::New);
    t->InstanceTemplate()->SetInternalFieldCount(1);

    t->SetClassName(Nan::New("Emitter").ToLocalChecked());
    Nan::SetPrototypeMethod(t, "open", Emitter::Open);
    Nan::SetPrototypeMethod(t, "decode", Emitter::Decode);
    Nan::SetPrototypeMethod(t, "readFrame", Emitter::ReadFrame);
    Nan::SetPrototypeMethod(t, "readAudio", Emitter::ReadAudio);

    Nan::Set(target, Nan::New("Emitter").ToLocalChecked(), t->GetFunction());
    //Nan::SetMethod(target, "init_params", node_init_params);
  }

}
