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

  struct Emitter: Nan::ObjectWrap {
    static NAN_METHOD(New);
    static NAN_METHOD(Open);
    static NAN_METHOD(Ping);
    static NAN_METHOD(ReadFrame);

  };



  int width, height;
  YUVImage *yuv;

  AVFormatContext *pFormatCtx = NULL;
  int             i, videoStream, audioStream;
  AVCodecContext  *pCodecCtxOrig = NULL;
  AVCodecContext  *pCodecCtx = NULL;
  AVCodec         *pCodec = NULL;
  AVFrame         *pFrame = NULL;
  AVPacket        packet;
  AVPixelFormat   pix_fmt = AV_PIX_FMT_YUV420P; //AV_PIX_FMT_YUV420P; //AV_PIX_FMT_RGB24;
  int             frameFinished;
  uint8_t *buffer;

  //YUVImage *bmp;
  struct SwsContext   *sws_ctx            = NULL;
  // AVDictionary        *videoOptionsDict   = NULL;

  // AVFrame         *avFrameYUV = NULL;
  // size_t          avFrameYUVSize;
  AVFrame         *avFrameCopy = NULL;
  size_t          avFrameCopySize;
  PacketQueue     videoq;



#define MAX_AUDIOQ_SIZE (5 * 16 * 1024)
#define MAX_VIDEOQ_SIZE (5 * 256 * 1024)
#define VIDEO_PICTURE_QUEUE_SIZE 1

  //vector <YUVImage> frames;


  // int our_get_buffer(struct AVCodecContext *c, AVFrame *pic, int flags) {
  //   int ret = avcodec_default_get_buffer(c, pic);
  //   uint64_t *pts = av_malloc(sizeof(uint64_t));
  //   *pts = global_video_pkt_pts;
  //   pic->opaque = pts;
  //   return ret;
  // }
  // void our_release_buffer(struct AVCodecContext *c, AVFrame *pic) {
  //   if(pic) av_freep(&pic->opaque);
  //   avcodec_default_release_buffer(c, pic);
  // }

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
        fprintf(stderr, "found packet\n");
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
        fprintf(stderr, "sdl wait.. \n");
        Sleep(100); //wait 100ms? no SDL
        ret = 0; //????
        break; //????
        //SDL_CondWait(q->cond, q->mutex);
      }
    }
    //SDL_UnlockMutex(q->mutex);
    return ret;
  }

  class QueueWorker : public AsyncWorker {
   public:
    QueueWorker(Callback *callback)
      : AsyncWorker(callback) {}
    ~QueueWorker() {}

    void Execute () {
      //Sleep(1000);
      AVPacket pkt1, *packet = &pkt1;
      int frameFinished;
      double pts;
      pFrame = av_frame_alloc();
      yuv = new YUVImage;
      fprintf(stderr, "exec!\n");
      int i=0;
      for(;;) {

        if(packet_queue_get(&videoq, packet, 1) < 0) {
          // means we quit getting packets
          fprintf(stderr, "quit getting packets!\n");
          break;
        }
        //fprintf(stderr, "decode...%d\n", packet);


        avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, packet);
        if(frameFinished) {
          fprintf(stderr, "decoded !\n");
          avFrameCopySize = avpicture_get_size(pix_fmt, pCodecCtx->width, pCodecCtx->height);
          sws_scale(sws_ctx, (uint8_t const * const *)pFrame->data, pFrame->linesize, 0, pCodecCtx->height, avFrameCopy->data, avFrameCopy->linesize);
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

    //   for(;;) {
    //     if(packet_queue_get(&videoq, packet, 1) < 0) {
    //       // means we quit getting packets
    //       fprintf(stderr, "packet get!\n");
    //       break;
    //     }
    //
    //     fprintf(stderr, "decode...%d\n", i);
    //     avcodec_decode_video2(pCodecCtx, pFrameRead, &frameFinished, packet);
    // //
    // //     if(packet->dts == AV_NOPTS_VALUE && pFrameRead->opaque && *(uint64_t*)pFrameRead->opaque != AV_NOPTS_VALUE) {
    // //       pts = *(uint64_t *)pFrameRead->opaque;
    // //     } else if(packet->dts != AV_NOPTS_VALUE) {
    // //       pts = packet->dts;
    // //     } else {
    // //       pts = 0;
    // //     }
    // //     pts *= av_q2d(pFormatCtx->streams[videoStream]->time_base);
    // //     //fprintf(stderr, "pts %d\n", (int)pts);
    // //
    // //     // Did we get a video frame?
    // //
    //     if(frameFinished) {
    //       fprintf(stderr, "frame !\n");
    //       Local<Value> argv[] = {
    //           Nan::New("Frame!").ToLocalChecked()
    //       };
    //       callback->Call(1, argv);
    //
    //       break;
    //       // avFrameCopySize = avpicture_get_size(pix_fmt, pCodecCtx->width, pCodecCtx->height);
    //       // sws_scale(sws_ctx, (uint8_t const * const *)pFrameRead->data, pFrameRead->linesize, 0, pCodecCtx->height, avFrameCopy->data, avFrameCopy->linesize);
    //       // extractYUV();
    //       //
    //       // v8::Local<v8::Value> argv[] = {
    //       //   //Nan::New("ping").ToLocalChecked(), // event name
    //       //   Nan::CopyBuffer((char *)yuv->avY, yuv->size_y).ToLocalChecked(),
    //       //   Nan::CopyBuffer((char *)yuv->avU, yuv->size_u).ToLocalChecked(),
    //       //   Nan::CopyBuffer((char *)yuv->avV, yuv->size_v).ToLocalChecked(),
    //       //   Nan::New<Integer>(yuv->pitchY),
    //       //   Nan::New<Integer>(yuv->pitchU),
    //       //   Nan::New<Integer>(yuv->pitchV)
    //       //   //Nan::New<Integer>(width),
    //       //   //Nan::New<Integer>(height)
    //       // };
    //       // callback->Call(6, argv);
    //       //return frame?
    // //      info.GetReturnValue().Set(Nan::New<String>("OK").ToLocalChecked());
    //   //     /pts = synchronize_video(is, pFrame, pts);
    //   //     if(queue_picture(is, pFrame, pts) < 0) {
    //   // break;
    //   //     }
    //     }
    //   }
    //
    //
    //   delete yuv;

    }

    void HandleOKCallback(){
      v8::Local<v8::Value> argv[] = {
        Nan::CopyBuffer((char *)yuv->avY, yuv->size_y).ToLocalChecked(),
        Nan::CopyBuffer((char *)yuv->avU, yuv->size_u).ToLocalChecked(),
        Nan::CopyBuffer((char *)yuv->avV, yuv->size_v).ToLocalChecked(),
        Nan::New<Integer>(yuv->pitchY),
        Nan::New<Integer>(yuv->pitchU),
        Nan::New<Integer>(yuv->pitchV)
      };
      callback->Call(6, argv);
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
          avFrameCopy = av_frame_alloc();

          width = pCodecCtx->width;
          height = pCodecCtx->height;

          int size = avpicture_get_size(pix_fmt, width, height);
          uint8_t* buffer = (uint8_t*)av_malloc(size);

          avpicture_fill((AVPicture *)avFrameCopy, buffer, pix_fmt, width, height);

          // initialize SWS context for software scaling
          sws_ctx = sws_getContext(height,
               height,
               pCodecCtx->pix_fmt,
               pCodecCtx->width,
               pCodecCtx->height,
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
            if(videoq.size > MAX_VIDEOQ_SIZE) {
              fprintf(stderr, "max queue size.. waiting 10ms %d\n", i);
              Sleep(1000); //sleep for a second?
              continue;
            }

            if(av_read_frame(pFormatCtx, packet) < 0) {
              if(pFormatCtx->pb->error == 0) {
                fprintf(stderr, "no error.. sleeping %d\n", i);
        	       Sleep(100); /* no error; wait for user input */
                 continue;
              } else {
        	       break;
              }
            }

            // Is this a packet from the video stream?
            if(packet->stream_index == videoStream) {
              //fprintf(stderr, "put: %d\n", frameIndex);
              packet_queue_put(&videoq, packet);
              frameIndex++;
              progress.Signal();
            // } else if(packet->stream_index == is->audioStream) {
            //   packet_queue_put(&is->audioq, packet);
            } else {
              //fprintf(stderr, "free: %d\n", i);
              av_free_packet(packet);
            }
            //fprintf(stderr, "frame: %d\n", frameIndex);
            //progress.Send(reinterpret_cast<const char*>(&i), sizeof(int));

          }


          // i=0;
          // while(av_read_frame(pFormatCtx, &packet)>=0) {
          // //if(av_read_frame(pFormatCtx, &packet) >=0){
          //    // Is this a packet from the video stream?
          //    if(packet.stream_index==videoStream) {
          //      // Decode video frame
          //      avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);
          //      if(frameFinished) {
          //        if(++i<=240){
          //          fprintf(stderr, "frame: %d\n", i);
          //
          //          //progress.Send(reinterpret_cast<const char*>(&i), sizeof(int));
          //
          //         avFrameCopySize = avpicture_get_size(pix_fmt, pCodecCtx->width, pCodecCtx->height);
          //
          //          sws_scale(sws_ctx, (uint8_t const * const *)pFrame->data, pFrame->linesize, 0, pCodecCtx->height, avFrameCopy->data, avFrameCopy->linesize);
          //
          //          extractYUV();
          //
          //          //progress.Send(reinterpret_cast<const char*>(&i), sizeof(int));
          //          progress.Signal();
          //
          //          av_free_packet(&packet);
          //        }
          //      }
          //    }
          // }

          //delete yuv;
    }

    void HandleProgressCallback(const char *data, size_t size) {
      Nan::HandleScope scope;
      v8::Local<v8::Value> argv[] = {
        Nan::New<Integer>(frameIndex)
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


    videoStream=-1;
    for(i=0; i<pFormatCtx->nb_streams; i++) {
      if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_VIDEO && videoStream < 0) {
        videoStream=i;
      }
    }
    if(videoStream==-1){
      fprintf(stderr, "find videoStream failed\n");
      exit(-1);
    }

    pCodecCtxOrig=pFormatCtx->streams[videoStream]->codec;

  //   // return object
    // info.GetReturnValue().Set(obj);
    //IncodeInput *meta = new IncodeInput;

    Nan:: HandleScope scope;
    Local<Value> argv[] = {
        Nan::New(pFormatCtx->filename).ToLocalChecked(),
        Nan::New(time_value_string(val_str, sizeof(val_str), pFormatCtx->duration)).ToLocalChecked(),
        Nan::New(pCodecCtxOrig->width),
        Nan::New(pCodecCtxOrig->height),
        Nan::New(pFormatCtx->iformat->name).ToLocalChecked(),
        Nan::New(pFormatCtx->iformat->long_name).ToLocalChecked()
    };

    // Nan::Call(callback->GetFunction(), GetCurrentContext()->Global(), 1, argv);//callback->Call(1, argv);

    callback->Call(6, argv);

    //uv_queue_work(uv_default_loop(), &request->req, ec_quick_probe, (uv_after_work_cb)ec_decode_buffer_after);

    //AsyncQueueWorker(new DecodeWorker(callback, input));
  }

  // emits ping event
  NAN_METHOD(Emitter::Ping) {

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

    //decode_video();

    //uv_queue_work(uv_default_loop(), &request->req, ec_decode_buffer_async, (uv_after_work_cb)ec_decode_buffer_after);

    // v8::Local<v8::Value> returnValue = Nan::CopyBuffer((char*)avFrameYUV->data[0], avFrameYUVSize).ToLocalChecked();
    // info.GetReturnValue().Set(returnValue);

  }




  void ec_decode_buffer_async (uv_work_t *req) {
    DecodeRequest *r = (DecodeRequest *)req->data;
    fprintf(stderr, "buffer!");



    //r->yuv_y = Nan::CopyBuffer((char*)avFrameYUV->data[0], avFrameYUVSize).ToLocalChecked();
    //av_init_packet(&packet);
    //return array of frames?
    //r->rtn = avFrameYUV; //Nan::New<String>("OK").ToLocalChecked();
    //r->rtn = 5;
  }


  void extractYUV(){

    yuv->pitchY = avFrameCopy->linesize[0];
    yuv->pitchU = avFrameCopy->linesize[1];
    yuv->pitchV = avFrameCopy->linesize[2];

    yuv->avY = avFrameCopy->data[0];
    yuv->avU = avFrameCopy->data[1];
    yuv->avV = avFrameCopy->data[2];

    yuv->size_y = (avFrameCopy->linesize[0] * pCodecCtx->height);
    yuv->size_u = (avFrameCopy->linesize[1] * pCodecCtx->height / 2);
    yuv->size_v = (avFrameCopy->linesize[2] * pCodecCtx->height / 2);

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
      Nan::New<Integer>(yuv->pitchV),
      Nan::New<Integer>(width),
      Nan::New<Integer>(height)
    };

    //Nan::New(r->callback)->Call(Nan::GetCurrentContext()->Global(), 8, ((v8::Local<v8::Value>)argv));
    Nan::New(r->callback)->Call(Nan::GetCurrentContext()->Global(), 8, argv);

    // cleanup
    r->callback.Reset();
    delete r;

    if (try_catch.HasCaught()) {
      FatalException(try_catch);
    }
    // DecodeRequest *r = (DecodeRequest *)req->data;
    //
    // Handle<Value> argv[1];
    // argv[0] = Nan::New<v8::Object>(r->rtn);
    // //
    // Nan::TryCatch try_catch;
    // //Nan::CopyBuffer((char*)bmp->pixels, sizeof(bmp->pixels)).ToLocalChecked();
    // //v8::Local<v8::Value> returnValue = Nan::CopyBuffer((char*)r->rtn, r->rsize).ToLocalChecked();
    //
    // // v8::Local<v8::Value> argv[] = {
    // //   r->rtn
    // // };
    // Nan::New(r->callback)->Call(Nan::GetCurrentContext()->Global(), 1, argv);
    //
    // // cleanup
    // r->callback.Reset();
    // delete r;
    //
    // if (try_catch.HasCaught()) {
    //   FatalException(try_catch);
    // }
  }
  NAN_METHOD(ec_teardown){
    fprintf(stderr, "destroy \n");
    // Free the YUV frame
    //av_frame_free(&pFrame);
    // Close the codecs
    avcodec_close(pCodecCtxOrig);
    // Close the codec
    avcodec_close(pCodecCtx);

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
    Nan::SetPrototypeMethod(t, "ping", Emitter::Ping);
    Nan::SetPrototypeMethod(t, "readFrame", Emitter::ReadFrame);

    Nan::Set(target, Nan::New("Emitter").ToLocalChecked(), t->GetFunction());
    //Nan::SetMethod(target, "init_params", node_init_params);
  }

}
