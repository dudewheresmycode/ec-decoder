#include <nan.h>
// #include "probe.h"
// #include "transcode.h"
// #include "player.h"

using namespace v8;
using namespace node;

namespace extracast {


  void ec_decoder_init(Handle<Object>);

  void Initialize(Handle<Object> target) {
    ec_decoder_init(target);
  }

  NODE_MODULE(addon, extracast::Initialize)

}
