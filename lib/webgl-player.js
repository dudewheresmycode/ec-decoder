//
// function initWebGL(canvas) {
//   gl = null;
//
//   // Try to grab the standard context. If it fails, fallback to experimental.
//   gl = canvas.getContext("webgl") || canvas.getContext("experimental-webgl");
//
//   // If we don't have a GL context, give up now
//   if (!gl) {
//     console.warn("Unable to initialize WebGL. Your browser may not support it.");
//   }
//
//   return gl;
// }
var ecDecoder = require('../index.js').Decoder;
var EventEmitter = require('events').EventEmitter;
var util = require('util');



var YUVBuffer = require('yuv-buffer');
//var WebGLFrameSink = require('./frame-sink.js');
var YUVCanvas = require('yuv-canvas');

function WebGLPlayer(opts){
  if (! (this instanceof WebGLPlayer)) return new WebGLPlayer();
  EventEmitter.call(this);

  var self = this;
  var i = 0;
  self.drawn = false;
  self.state = {
    playing:false,
    reading:false,
    readIndex:0
  };
  self.fbq = []; //frame buffer queue
  var framesAvailable = 0,
      targetFps = 30;

  var _readloop = function(){
    //read and wait for frame callback
    self.decoder.readFrame(function(y,u,v,yS,uS,vS,packetsLeft){
      //console.log('received YUV data');
      self.digestFrame(y,u,v,yS,uS,vS);
      self.renderFrame();
      //scheduleFrame();

      console.log("packets remaining: %d", packetsLeft);
      //if(self.state.readIndex < framesAvailable){
      if(packetsLeft > 0){
        setTimeout(_readloop, targetFps);
      }else{
        console.log("no more packets!");
        //console.log('underrun, wait and read again');
        //if not end of frames we should loop again
        //setTimeout(_readloop, 1000);
      }
    });
  }
  this.start = function(input){
    self.decoder = new ecDecoder();
    self.decoder.open(input, function(
      filename,
      duration,
      width,
      height,
      codec,
      codec_long){

        self.configure({d:duration,w:width,h:height});

        //start decode thread
        self.decoder.ping(
          function(decodedFrameIdx){
            framesAvailable = decodedFrameIdx;
            console.log('packets available', decodedFrameIdx);
            if(!self.state.reading){
              console.log('start read');
              self.state.reading=true;
              _readloop();
            }
          },
          function(lastFrameIdx){
            console.log("all done", lastFrameIdx);
            framesAvailable = lastFrameIdx;
          //renderFrame(y,u,v,yS,uS,vS);
          //webgl.digestFrame(y,u,v,yS,uS,vS,w,h);
          //total = webgl.getBufferIndex();
          //webgl.clear();
        });


    });
  }


  this.configure = function(config){
    this.config = config;
    //opts.canvas.width = config.w;
    //opts.canvas.height = config.h;

    self.yuv = YUVCanvas.attach(opts.canvas);

    self.yuv_format = YUVBuffer.format({
      // Encoded size is 720x480, for classic NTSC standard def video
      width: config.w,
      height: config.h,

      // DVD is also 4:2:0, so halve the chroma dimensions.
      chromaWidth: config.w/2,
      chromaHeight: config.h/2,

      // Full frame is visible.
      cropLeft: 0,
      cropTop: 0,
      cropWidth: config.w,
      cropHeight: config.h,

      // Final display size stretches back out to 16:9 widescreen:
      displayWidth: opts.canvas.width,
      displayHeight: opts.canvas.height
      // y: {data:y, stride:yS},
      // u: {data:u, stride:uS},
      // v: {data:v, stride:vS}
    });

  }

  this.renderFrame = function(){
    //var f = self.fbq[idx];
    // if(typeof self.fbq[idx]=='undefined'){
    //   return -1;
    // }
    console.log('draw');
    self.yuv.drawFrame(self.f);

  }
  this.digestFrame = function(y,u,v,yS,uS,vS,w,h){
    var yuv_frame = YUVBuffer.frame(self.yuv_format,
      YUVBuffer.lumaPlane(self.yuv_format, y, yS, 0),
      YUVBuffer.chromaPlane(self.yuv_format, u, uS, 0),
      YUVBuffer.chromaPlane(self.yuv_format, v, vS, 0)
    );
    self.f = yuv_frame;
    //self.fbq.push(yuv_frame);

    // //calculate can play event
    // if(this.config.duration){
    //   var estFrames = this.config.duration*targetFps;
    //   var loaded = self.fbq.length;
    //
    // }

    //console.log('push', self.fbq.length);
  }

  this.getBufferIndex = function(){
    return self.fbq.length;
  }
  this.clear = function(){
    self.yuv.clear();
  }
}

util.inherits(WebGLPlayer, EventEmitter);

module.exports = WebGLPlayer;
