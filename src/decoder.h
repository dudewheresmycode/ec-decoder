
#include <nan.h>
#include <node.h>
#include <node_buffer.h>
#include "node_pointer.h"

extern "C" {
  #include <libavcodec/avcodec.h>
  #include <libavformat/avformat.h>
  #include <libavfilter/avfiltergraph.h>
  #include <libavfilter/buffersink.h>
  #include <libavfilter/buffersrc.h>
  #include <libavutil/opt.h>
  #include <libavutil/pixdesc.h>
  #include <libavutil/mathematics.h>
  #include <libavutil/imgutils.h>
  #include <libswscale/swscale.h>
}

using namespace v8;
using namespace node;

namespace extracast {


  typedef struct YUVImage {
    // v8::Uint32 format;
    int w, h;
    int planes;

    uint32_t pitchY;
    uint32_t pitchU;
    uint32_t pitchV;

    uint8_t *avY;
    uint8_t *avU;
    uint8_t *avV;

  } YUVImage;

  typedef struct IncodeInput {
    char *path;
  };
  typedef struct DecodeRequest {
    uv_work_t req;
    char *input;
    //Nan::Persistent<v8::Function> progress;
    Nan::Persistent<v8::Function> callback;
    v8::Local<v8::Integer> frameIndex;
    unsigned char *output;
    int output_size;
    //YUVImage rtn
    v8::Local<v8::Value> yuv_y;
    //uint8_t *yuv_y;
    size_t yuv_y_size;
    YUVImage *bmp;
    //v8::Local<v8::Value> yuv_u;
    //v8::Local<v8::Value> yuv_v;
    size_t rsize;
  };

  void ec_decode_buffer_async (uv_work_t *);
  void ec_decode_buffer_after (uv_work_t *);
  void ec_decode_buffer_flush (uv_work_t *);
  //void decode_video();
  #define ec_decode_flush_nogap_after ec_decode_buffer_after

}
