#ifndef _WIN32
#include <unistd.h>
#define Sleep(x) usleep((x)*1000)
#endif


#include "decoder.h"

using namespace v8;
using namespace Nan;
using namespace node;

namespace extracast {



  int width, height;

  AVFormatContext *pFormatCtx = NULL;
  int             i, videoStream, audioStream;
  AVCodecContext  *pCodecCtxOrig = NULL;
  AVCodecContext  *pCodecCtx = NULL;
  AVCodec         *pCodec = NULL;
  AVFrame         *pFrame = NULL;
  AVPacket        packet;

  int             frameFinished;
  uint8_t *buffer;

  YUVImage *bmp;
  struct SwsContext   *sws_ctx            = NULL;
  // AVDictionary        *videoOptionsDict   = NULL;

  AVFrame         *avFrameYUV = NULL;
  size_t          avFrameYUVSize;
  AVFrame         *avFrameRGB = NULL;
  size_t          avFrameRGBSize;

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

  void f1(char const* const* a){}

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

    uv_queue_work(uv_default_loop(), &request->req, ec_decode_buffer_async, (uv_after_work_cb)ec_decode_buffer_after);
    // v8::Local<v8::Value> returnValue = Nan::CopyBuffer((char*)avFrameYUV->data[0], avFrameYUVSize).ToLocalChecked();
    // info.GetReturnValue().Set(returnValue);

  }




  void ec_decode_buffer_async (uv_work_t *req) {
    DecodeRequest *r = (DecodeRequest *)req->data;
    fprintf(stderr, "buffer!");


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
    pFrame = av_frame_alloc();
    //avFrameYUV
    avFrameRGB = av_frame_alloc();

    width = pCodecCtx->width;
    height = pCodecCtx->height;

    int size = avpicture_get_size(AV_PIX_FMT_RGB24, width, height);
    uint8_t* buffer = (uint8_t*)av_malloc(size);

    avpicture_fill((AVPicture *)avFrameRGB, buffer, AV_PIX_FMT_RGB24, width, height);

    // initialize SWS context for software scaling
    sws_ctx = sws_getContext(height,
         height,
         pCodecCtx->pix_fmt,
         pCodecCtx->width,
         pCodecCtx->height,
         AV_PIX_FMT_RGB24,
         SWS_BILINEAR,
         NULL,
         NULL,
         NULL
    );

    i=0;
    while(av_read_frame(pFormatCtx, &packet)>=0) {
    //if(av_read_frame(pFormatCtx, &packet) >=0){
       // Is this a packet from the video stream?
       if(packet.stream_index==videoStream) {
         // Decode video frame
         avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);
         if(frameFinished) {
           if(++i<=1){
             fprintf(stderr, "frame: %d\n", i);

             //progress.Send(reinterpret_cast<const char*>(&i), sizeof(int));
            //  progress.Send(i, sizeof(int));
             //AVPicture pict = av_frame_alloc();

            avFrameRGBSize = avpicture_get_size(AV_PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height);


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
            //int nbytes = avpicture_get_size(AV_PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height);
            //uint8_t* outbuffer = (uint8_t*)av_malloc(nbytes*sizeof(uint8_t));

            // avFrameYUV->linesize[0] = r->bmp->pitchY;
            // avFrameYUV->linesize[1] = r->bmp->pitchU;
            // avFrameYUV->linesize[2] = r->bmp->pitchV;
            // avFrameYUV->data[0] = r->bmp->avY;
            // avFrameYUV->data[1] = r->bmp->avU;
            // avFrameYUV->data[2] = r->bmp->avV;
            //
             sws_scale(sws_ctx, (uint8_t const * const *)pFrame->data, pFrame->linesize, 0, pCodecCtx->height, avFrameRGB->data, avFrameRGB->linesize);
             //avpicture_fill((AVPicture *)avFrameYUV, pFrame->data[0], AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height);


            //  if(avpicture_fill((AVPicture*) avFrameYUV, outbuffer, AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height) < 0){
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


    //r->yuv_y = Nan::CopyBuffer((char*)avFrameYUV->data[0], avFrameYUVSize).ToLocalChecked();
    //av_init_packet(&packet);
    //return array of frames?
    //r->rtn = avFrameYUV; //Nan::New<String>("OK").ToLocalChecked();
    //r->rtn = 5;
  }

  void ec_decode_buffer_after (uv_work_t *req) {
    Nan::HandleScope scope;
    DecodeRequest *r = (DecodeRequest *)req->data;

    // uint32_t pitchY = avFrameYUV->linesize[0];
    // uint32_t pitchU = avFrameYUV->linesize[1];
    // uint32_t pitchV = avFrameYUV->linesize[2];
    //
    // uint8_t *avY = avFrameYUV->data[0];
    // uint8_t *avU = avFrameYUV->data[1];
    // uint8_t *avV = avFrameYUV->data[2];
    //
    // Handle<Value> argv[1];
    // argv[0] = Nan::New<Integer>(r->rtn);
    //Local<v8::Object> wrapper = WrapPointer(bmp);
    // size_t size_y = (avFrameYUV->linesize[0] * pCodecCtx->height);
    // size_t size_u = (avFrameYUV->linesize[1] * pCodecCtx->height / 2);
    // size_t size_v = (avFrameYUV->linesize[2] * pCodecCtx->height / 2);


//    fprintf(stderr, "size_y: %d\n", avFrameRGB->data[0]);
    char *buffer = (char *)avFrameRGB->data[0];
    size_t bSize = sizeof(buffer);
    v8::Local<v8::Value> argv[] = {
      Nan::CopyBuffer(buffer, (avFrameRGB->linesize[0]*height)).ToLocalChecked(),
      //Nan::CopyBuffer((char *)avFrameYUV->data[1], size_u).ToLocalChecked(),
      //Nan::CopyBuffer((char *)avFrameYUV->data[2], size_v).ToLocalChecked(),
      Nan::New<Integer>(avFrameRGB->linesize[0]),
      //Nan::New<Integer>(pitchU),
      //Nan::New<Integer>(pitchV),

      Nan::New<Integer>(width),
      Nan::New<Integer>(height)
      //Nan::CopyBuffer(r->yuv_y, r->yuv_y_size).ToLocalChecked()
    };


    Nan::TryCatch try_catch;

    Nan::New(r->callback)->Call(Nan::GetCurrentContext()->Global(), 4, argv);

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


  void ec_decoder_init(Handle<Object> target){
    Nan::HandleScope scope;

    av_register_all();

    Nan::SetMethod(target, "config", ec_config);
    Nan::SetMethod(target, "frame", ec_decode_frame);
    Nan::SetMethod(target, "start", ec_decode_start);
    Nan::SetMethod(target, "destroy", ec_teardown);
    //Nan::SetMethod(target, "init_params", node_init_params);
  }

}
