
#include <nan.h>
#include <node.h>
#include <node_buffer.h>
#include "node_pointer.h"

extern "C" {
  #include <libavcodec/avcodec.h>
  #include <libavformat/avformat.h>
  #include <libavformat/avio.h>
  #include <libswscale/swscale.h>
  #include <libavutil/avstring.h>
  #include <libavutil/time.h>

  // #include <libavcodec/avcodec.h>
  // #include <libavformat/avformat.h>
  // #include <libavformat/avio.h>
  // #include <libavfilter/avfiltergraph.h>
  // #include <libavfilter/buffersink.h>
  // #include <libavfilter/buffersrc.h>
  // #include <libavutil/opt.h>
  // #include <libavutil/pixdesc.h>
  // #include <libavutil/mathematics.h>
  // #include <libavutil/imgutils.h>
  // #include <libswscale/swscale.h>
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

    size_t size_y;
    size_t size_u;
    size_t size_v;

  } YUVImage;

  void extractYUV();

  static char *time_value_string(char *, int, int64_t);
  static char *value_string(char *, int, double, const char *);

  typedef struct IncodeInput {
    char *path;
    char *duration;
    int width;
    int height;
    char *codec;
    char *codec_long;
  };

  struct Emitter: Nan::ObjectWrap {
    static NAN_METHOD(New);
    static NAN_METHOD(Open);
    static NAN_METHOD(Ping);
    static NAN_METHOD(ReadFrame);

  };
  

  typedef struct PacketQueue {
    AVPacketList *first_pkt, *last_pkt;
    int nb_packets;
    int size;
  } PacketQueue;


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
    v8::Local<v8::Object> emitter;
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
