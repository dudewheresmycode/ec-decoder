#ifndef _WIN32
#include <unistd.h>
#define Sleep(x) usleep((x)*1000)
#endif


#include "decoder.h"

using namespace v8;
using namespace Nan;
using namespace node;

namespace extracast {



  AVFormatContext *pFormatCtx = NULL;
  int             i, videoStream, audioStream;
  AVCodecContext  *pCodecCtxOrig = NULL;
  AVCodecContext  *pCodecCtx = NULL;
  AVCodec         *pCodec = NULL;
  AVFrame         *pFrame = NULL;
  AVPacket        packet;
  int             frameFinished;
  uint8_t *buffer;
//  YUVImage *bmp;
  struct SwsContext   *sws_ctx            = NULL;
  AVDictionary        *videoOptionsDict   = NULL;

  class DecodeWorker : public AsyncProgressWorker {
   public:
    DecodeWorker(
        Callback *callback
      , Callback *progress
      , const char *inputPath)
      : AsyncProgressWorker(callback), progress(progress)
      , inputPath(inputPath) {}
    ~DecodeWorker() {}

    void Execute (const AsyncProgressWorker::ExecutionProgress& progress) {

      fprintf(stderr, "Start: %s\n", inputPath);

      // Open video file
      if(avformat_open_input(&pFormatCtx, inputPath, NULL, NULL)!=0){
        fprintf(stderr, "Could not open %s\n", inputPath);
        exit(-1);
      }

      // Retrieve stream information
      if(avformat_find_stream_info(pFormatCtx, NULL)<0){
        fprintf(stderr, "avformat_find_stream_info failed\n");
        exit(-1);
      }
      // Dump information about file onto standard error
      av_dump_format(pFormatCtx, 0, inputPath, 0);

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
      pFrame=av_frame_alloc();

      // initialize SWS context for software scaling
      sws_ctx = sws_getContext(pCodecCtx->width,
           pCodecCtx->height,
           pCodecCtx->pix_fmt,
           pCodecCtx->width,
           pCodecCtx->height,
           AV_PIX_FMT_YUV420P,
           SWS_BILINEAR,
           NULL,
           NULL,
           NULL
      );

      i=0;
      while(av_read_frame(pFormatCtx, &packet)>=0) {
         // Is this a packet from the video stream?
         if(packet.stream_index==videoStream) {
           // Decode video frame
           avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);
           if(frameFinished) {
             if(++i<=1){
               fprintf(stderr, "frame: %d\n", i);
               progress.Send(reinterpret_cast<const char*>(&i), sizeof(int));
              //  progress.Send(i, sizeof(int));
              //  AVPicture pict;
              //  // Convert the image into YUV format that SDL uses
              //  sws_scale(sws_ctx, (uint8_t const * const *)pFrame->data, pFrame->linesize, 0, pCodecCtx->height, pict.data, pict.linesize);
              //  buffer = pict.data[0];
              //  //y = pict.data[0];
              //  //u = pict.data[2];
              //  //v = pict.data[1];
               //
               av_free_packet(&packet);
             }
           }
         }
      }

      // Allocate video frame
      //pFrame=av_frame_alloc();
      //
      //
      // progress.Send("p1", 2);
      // Sleep(1000);
      // progress.Send("p2", 2);
      // Sleep(1000);

      // Free the YUV frame
      //av_free(pFrame);




    }

    void HandleProgressCallback(const char *data, size_t dataSize) {
      Nan::HandleScope scope;
      //v8::Local<v8::Value> rbuffer = Nan::CopyBuffer((char*)data, dataSize).ToLocalChecked();

      v8::Local<v8::Value> argv[] = {
        New<v8::Integer>(*reinterpret_cast<int*>(const_cast<char*>(data)))
        //rbuffer
        //New<v8::Integer>(pCodecCtx->width),
        //New<v8::Integer>(pCodecCtx->height)
      };
      progress->Call(1, argv);
    }
    void Destroy(){
      fprintf(stderr, "destroy \n");
      // Free the YUV frame
      av_frame_free(&pFrame);
      // Close the codecs
      avcodec_close(pCodecCtxOrig);
      // Close the codec
      avcodec_close(pCodecCtx);

      // Close the video file
      avformat_close_input(&pFormatCtx);
      exit(-1);
    }
   private:
    Callback *progress;
    const char *inputPath;
  };




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


  NAN_METHOD(ec_decode_start) {

    String::Utf8Value cmd(info[0]);
    char *input = (char*)(*cmd);

    fprintf(stderr, "Decode: %s\n", input);

    Callback *progress = new Callback(info[1].As<v8::Function>());
    Callback *callback = new Callback(info[2].As<v8::Function>());

    AsyncQueueWorker(new DecodeWorker(
        callback
      , progress
      , input)
    );

  }


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

    uv_queue_work(uv_default_loop(), &request->req, ec_decode_buffer_async, (uv_after_work_cb)ec_decode_buffer_after);
    //info.GetReturnValue().Set(Nan::New<v8::Integer>(5));
  }




  void ec_decode_buffer_async (uv_work_t *req) {
    DecodeRequest *r = (DecodeRequest *)req->data;
    fprintf(stderr, "buffer!");

    //av_init_packet(&packet);
    //return array of frames?
    r->rtn = 200; //Nan::New<String>("OK").ToLocalChecked();

  }

  void ec_decode_buffer_after (uv_work_t *req) {
    Nan::HandleScope scope;
    DecodeRequest *r = (DecodeRequest *)req->data;


    // Handle<Value> argv[1];
    // argv[0] = Nan::New<Integer>(r->rtn);
    char *data = "pop";
    v8::Local<v8::Value> argv[] = {
      Nan::New<Integer>(r->rtn)
      //Nan::CopyBuffer(r->yuv_y, r->yuv_y_size).ToLocalChecked()
    };


    Nan::TryCatch try_catch;

    Nan::New(r->callback)->Call(Nan::GetCurrentContext()->Global(), 1, argv);

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

  void ec_decode_buffer_flush (uv_work_t *req) {

  }


  void ec_decoder_init(Handle<Object> target){
    Nan::HandleScope scope;

    av_register_all();

    Nan::SetMethod(target, "config", ec_config);
    Nan::SetMethod(target, "frame", ec_decode_frame);
    Nan::SetMethod(target, "start", ec_decode_start);
    //Nan::SetMethod(target, "init_params", node_init_params);
  }

}
