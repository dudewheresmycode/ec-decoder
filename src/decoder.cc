#ifndef _WIN32
#include <unistd.h>
#define Sleep(x) usleep((x)*1000)
#endif


#include "decoder.h"

using namespace v8;
using namespace Nan;
using namespace node;

namespace extracast {


  struct Emitter: Nan::ObjectWrap {
    static NAN_METHOD(New);
    static NAN_METHOD(Open);
    static NAN_METHOD(Ping);
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

  AVFrame         *avFrameYUV = NULL;
  size_t          avFrameYUVSize;
  AVFrame         *avFrameCopy = NULL;
  size_t          avFrameCopySize;

  class DecodeWorker : public AsyncWorker {
   public:
    DecodeWorker(Callback *callback, const char *inputFilename)
      : AsyncWorker(callback), inputFilename(inputFilename) {}
    ~DecodeWorker() {}

    void Execute () {
      //DecodeRequest *r = (DecodeRequest *)req->data;
      fprintf(stderr, "Open: %s\n", inputFilename);
      // Open video file
      if(avformat_open_input(&pFormatCtx, inputFilename, NULL, NULL)!=0){
        fprintf(stderr, "Could not open %s\n", inputFilename);
        exit(-1);
      }

      // Retrieve stream information
      if(avformat_find_stream_info(pFormatCtx, NULL)<0){
        fprintf(stderr, "avformat_find_stream_info failed\n");
        exit(-1);
      }
      // Dump information about file onto standard error
      av_dump_format(pFormatCtx, 0, inputFilename, 0);
    }

   private:
    Callback *callback;
    const char *inputFilename;
    //const char *inputPath;
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

  Emitter* self;

  NAN_METHOD(Emitter::New) {
    assert(info.IsConstructCall());
    self = new Emitter();
    self->Wrap(info.This());

    info.GetReturnValue().Set(info.This());
  }

  NAN_METHOD(Emitter::Open) {
    String::Utf8Value cmd(info[0]);
    char *in = (*cmd);
    char *input = (char *)malloc(strlen(in) * sizeof(char));
    strcpy(input, in);

    Callback *callback = new Callback(info[1].As<v8::Function>());
    AsyncQueueWorker(new DecodeWorker(callback, input));
  }

  // emits ping event
  NAN_METHOD(Emitter::Ping) {

    DecodeRequest *request = new DecodeRequest;
    //request->input = input;
    request->req.data = request;

    //DecodeRequest *r = reinterpret_cast<DecodeRequest *>(request->data);
    //int frameIndex = info[0]->Uint32Value();
    //request->emitter = info.This();
    request->callback.Reset(info[0].As<Function>());
    // set a circular pointer so we can get the "encode_req" back later
    //fprintf(stderr, "get buffered frames: %d\n", frameIndex);

    //decode_video();

    //!!uv_queue_work(uv_default_loop(), &request->req, ec_decode_buffer_async, (uv_after_work_cb)ec_decode_buffer_after);

    // Handle<Value> argv[2] = {
    //   Nan::New("ping").ToLocalChecked(), // event name
    //   Nan::New<String>("Hello").ToLocalChecked()
    //   //info[0]->ToString()  // argument
    // };

    // Nan::MakeCallback(info.This(), "emit", 2, argv);



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


    yuv = new YUVImage;
    // Allocate video frame
    pFrame = av_frame_alloc();
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
    //v8::Local<v8::Object> em = UnwrapPointer(r->emitter);

    i=0;
    while(av_read_frame(pFormatCtx, &packet)>=0) {
    //if(av_read_frame(pFormatCtx, &packet) >=0){
       // Is this a packet from the video stream?
       if(packet.stream_index==videoStream) {
         // Decode video frame
         avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);
         if(frameFinished) {
           if(++i<=240){
             fprintf(stderr, "frame: %d\n", i);

             //progress.Send(reinterpret_cast<const char*>(&i), sizeof(int));
            //  progress.Send(i, sizeof(int));
             //AVPicture pict = av_frame_alloc();

            avFrameCopySize = avpicture_get_size(pix_fmt, pCodecCtx->width, pCodecCtx->height);


            // AVPicture pict;
            // pict.data[0] = bmp->pixels[0];
            // pict.data[1] = bmp->pixels[2];
            // pict.data[2] = bmp->pixels[1];
            //
            // pict.linesize[0] = bmp->pitches[0];
            // pict.linesize[1] = bmp->pitches[2];
            // pict.linesize[2] = bmp->pitches[1];
            //fprintf(stderr, "before %s\n", bmp->pitches[0]);
            //
            // avFrameYUV->data[0] = bmp->pixels[0];
            // avFrameYUV->data[1] = bmp->pixels[2];
            // avFrameYUV->data[2] = bmp->pixels[1];
            //
            // avFrameYUV->linesize[0] = bmp->pitches[0];
            // avFrameYUV->linesize[1] = bmp->pitches[2];
            // avFrameYUV->linesize[2] = bmp->pitches[1];

             //AVPicture* pict = (AVPicture *)avFrameYUV;
            //r->bmp = new YUVImage;
            //int nbytes = avpicture_get_size(pix_fmt, pCodecCtx->width, pCodecCtx->height);
            //uint8_t* outbuffer = (uint8_t*)av_malloc(nbytes*sizeof(uint8_t));

            // avFrameYUV->linesize[0] = r->bmp->pitchY;
            // avFrameYUV->linesize[1] = r->bmp->pitchU;
            // avFrameYUV->linesize[2] = r->bmp->pitchV;
            // avFrameYUV->data[0] = r->bmp->avY;
            // avFrameYUV->data[1] = r->bmp->avU;
            // avFrameYUV->data[2] = r->bmp->avV;
            //
             sws_scale(sws_ctx, (uint8_t const * const *)pFrame->data, pFrame->linesize, 0, pCodecCtx->height, avFrameCopy->data, avFrameCopy->linesize);

             extractYUV();

             v8::Local<v8::Value> argv[] = {
               Nan::New("ping").ToLocalChecked(), // event name
               Nan::CopyBuffer((char *)yuv->avY, yuv->size_y).ToLocalChecked(),
               Nan::CopyBuffer((char *)yuv->avU, yuv->size_u).ToLocalChecked(),
               Nan::CopyBuffer((char *)yuv->avV, yuv->size_v).ToLocalChecked(),
               Nan::New<Integer>(yuv->pitchY),
               Nan::New<Integer>(yuv->pitchU),
               Nan::New<Integer>(yuv->pitchV),
               Nan::New<Integer>(width),
               Nan::New<Integer>(height)
             };

             Nan::MakeCallback(info.This(), "emit", 9, argv);

             //Nan::New(r->callback)->Call(Nan::GetCurrentContext()->Global(), 8, ((v8::Local<v8::Value>)argv));
             //Nan::New(r->callback)->Call(Nan::GetCurrentContext()->Global(), 8, argv);
            //Nan::MakeCallback(Nan::ObjectWrap::Unwrap<Emitter>(self), "emit", 8, argv);

            //  // cleanup
            //  r->callback.Reset();



            //  Handle<Value> argv[2] = {
            //    Nan::New("ping").ToLocalChecked(), // event name
            //    Nan::New<Integer>(i)
            //    //info[0]->ToString()  // argument
            //  };
            //  Nan::MakeCallback(em, "emit", 2, argv);

             //avpicture_fill((AVPicture *)avFrameYUV, pFrame->data[0], pix_fmt, pCodecCtx->width, pCodecCtx->height);


            //  if(avpicture_fill((AVPicture*) avFrameYUV, outbuffer, pix_fmt, pCodecCtx->width, pCodecCtx->height) < 0){
            //    fprintf(stderr, "avpicture error\n");
            //  }
             //

            //  memcpy(r->bmp->avY, avFrameYUV->data[0], avFrameYUV->linesize[0] * avFrameYUV->height);
            //  memcpy(r->bmp->avU, avFrameYUV->data[1], avFrameYUV->linesize[1] * avFrameYUV->height / 2);
            //  memcpy(r->bmp->avV, avFrameYUV->data[2], avFrameYUV->linesize[2] * avFrameYUV->height / 2);



            //  // Convert the image into YUV format that SDL uses

            //  buffer = pict.data[0];
            //  y = pict.data[0];
            //  u = pict.data[2];
            //  v = pict.data[1];
            //buffer = pict;
             //
             av_free_packet(&packet);
           }
         }
       }
    }

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

    Nan::New(request->callback)->Call(Nan::GetCurrentContext()->Global(), 8, argv);

    // cleanup
    request->callback.Reset();
    delete yuv;
    delete request;

  }


  NAN_METHOD(ec_decode_start) {

    String::Utf8Value cmd(info[0]);
    char *in = (*cmd);
    char *input = (char *)malloc(strlen(in) * sizeof(char));
    strcpy(input, in);

    Callback *callback = new Callback(info[1].As<v8::Function>());
    AsyncQueueWorker(new DecodeWorker(callback, input));
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
    delete yuv;
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
    Nan::SetMethod(target, "start", ec_decode_start);
    Nan::SetMethod(target, "destroy", ec_teardown);

    Local<FunctionTemplate> t = Nan::New<FunctionTemplate>(Emitter::New);
    t->InstanceTemplate()->SetInternalFieldCount(1);

    t->SetClassName(Nan::New("Emitter").ToLocalChecked());
    Nan::SetPrototypeMethod(t, "open", Emitter::Open);
    Nan::SetPrototypeMethod(t, "ping", Emitter::Ping);

    Nan::Set(target, Nan::New("Emitter").ToLocalChecked(), t->GetFunction());
    //Nan::SetMethod(target, "init_params", node_init_params);
  }

}
