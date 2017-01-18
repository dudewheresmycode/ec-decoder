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
//var YUVCanvas = require('yuv-canvas');
var frameSink = require('./webgl-sink.js');




function WebGLPlayer(opts){
  if (! (this instanceof WebGLPlayer)) return new WebGLPlayer();
  EventEmitter.call(this);

  var self = this;
  var i = 0;
  self.lastFrame=0;
  self.drawn = false;
  self.state = {
    pts: {v:0,a:0,f:0},
    playing:false,
    reading:false,
    readIndex:0
  };
  self.fbq = []; //frame buffer queue

  var framesAvailable = 0,
      targetFps = 30,
      _frameLoaded = 0,
      _startts = false;

  this._initAudio = function(){

    if(!this.audioCtx){ this.audioCtx = new (window.AudioContext || window.webkitAudioContext)(); }
    // Stereo
    this.aChannels = 2;
    // Create an empty two second stereo buffer at the
    // sample rate of the AudioContext
    this.aFrameCount = this.config.sample_rate * this.config.duration;

  }

  var _lts = function(){
    return (new Date()).getTime()-_startts;
  }
  var _decodeTearDown = function(){
    _startts=false;
  }

  var lastLen = 0;
  var _adecodeloop = function(){
    self.decoder.readAudio(1024, function(buffer_l,buffer_r){
      console.log("AUDIO", buffer_l,buffer_l.length);
      _audioRender(buffer_l);
      //requestAnimationFrame(_adecodeloop);
    });
  }
  var _decodeloop = function(timestamp){
    //read and wait for frame callback
    self.decoder.readFrame(function(y,u,v,yS,uS,vS,packetsLeft,pts){
      self.digestFrame(y,u,v,yS,uS,vS,pts);
      _frameLoaded++;
      if(!self.state.playing && _frameLoaded > 5){
        _startts = (new Date()).getTime();
        self.state.playing=true;
        requestAnimationFrame(_renderloop);
      }
      //scheduleFrame();
      self.state.pts.v = pts;
      //console.log("pts: %s, frame: %s", pts, _frameLoaded)
      //console.log("packets in buffer: %d - V:%s/L:%s", packetsLeft, self.state.pts.v, _lts());
      //if(self.state.readIndex < framesAvailable){
      if(packetsLeft > 0){
      //   // _decodeloop();
      //   //setTimeout(_decodeloop, 1);
        requestAnimationFrame(_decodeloop);
      //
      //   //setTimeout(requestAnimationFrame(_decodeloop), adjust);
      //   //setTimeout(_decodeloop, targetFps-adjust);
      }
      // }else{
      //   console.log("no more packets!.. try again in a few...");
      //   //setTimeout(_decodeloop, 1000);
      //   _decodeTearDown();
      //   //console.log('underrun, wait and read again');
      //   //if not end of frames we should loop again
      //   //setTimeout(_decodeloop, 1000);
      // }
    });
  }
  var _audioRender = function(data){
    //just random values between -1.0 and 1.0
    //for (var channel = 0; channel < self.aChannels; channel++) {
     // This gives us the actual ArrayBuffer that contains the data

     self.aBuffer = self.audioCtx.createBuffer(self.aChannels, self.aFrameCount, self.audioCtx.sampleRate);

     var nowBuffering = self.aBuffer.getChannelData(0);
     for (var i = 0; i < self.aFrameCount; i++) {
       nowBuffering[i] = (data[i]/255) * 2 - 1;
    //    // Math.random() is in [0; 1.0]
    //    // audio needs to be in [-1.0; 1.0]
    //    nowBuffering[i] = Math.random() * 2 - 1;
     }
    //}

    // Get an AudioBufferSourceNode.
    // This is the AudioNode to use when we want to play an AudioBuffer
    var source = self.audioCtx.createBufferSource();
    // set the buffer in the AudioBufferSourceNode
    source.buffer = self.aBuffer;
    // connect the AudioBufferSourceNode to the
    // destination so we can hear the sound
    source.connect(self.audioCtx.destination);
    // start the source playing
    source.start();
  }

  var _renderloop = function(timestamp){

    // var adjust = timestamp - (self.state.pts.v*1000);
    //   adjust = adjust > 0 ? adjust : 0;
    // var rfps = _lts() / _frameLoaded;
    //
    //var frame = Math.round((_lts()/1000)*30); //30fps
    var frame = self.fbq.findIndex(function(it){
      return it.pts*1000 >= _lts();
    });
    var f = self.fbq[frame];
    self.lastFrame = frame;
    var statDebug = util.format(
      "Frame:%s / Buffered: %s / PTS_V:%s / TIMEBASE: %s",
      frame, self.fbq.length, self.state.pts.v, _lts()
    );

    //destroy frames we no longer need to save memory?
    if(self.lastFrame>0){
      //self.fbq = self.fbq.slice(self.lastFrame);
    }

    self.emit('debug', statDebug);
    if(f){
      //self.state.pts.f = frame;
      self.yuv.drawFrame(f.yuv);
      delete f;
    }
    //self.state.pts.f++;
    requestAnimationFrame(_renderloop);
    delete f;
  }

  this.start = function(input){
    self.decoder = new ecDecoder();
    self.pts = {v:0,a:0};
    self.decoder.open(input, function(info){

        console.log(info);

        self.configure(info);
        //self._initAudio();


        //start decode thread
        self.decoder.decode(
          function(decodedFrameIdx){
            framesAvailable = decodedFrameIdx;
            //console.log('packets available', decodedFrameIdx);
            if(!self.state.reading){
              console.log('start playback');

              self.state.reading=true;
              requestAnimationFrame(_decodeloop);
              //requestAnimationFrame(_adecodeloop);
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
    config.aspect_ratio = parseFloat(config.aspect_ratio);
    //opts.canvas.width = config.w;
    //opts.canvas.height = config.h;
    //self.yuv = YUVCanvas.attach(opts.canvas);
    //
    // var screen_w = opts.canvas.width;
    // var screen_h = opts.canvas.height;
    var screen_w = 854;
    var screen_h = 480;



    var w=0, h=0, x=0, y=0;
    h = screen_h;
    w = Math.round(h * config.aspect_ratio);

    if(w > screen_w) {
      w = screen_w;
      h = Math.round(w / config.aspect_ratio);
    }

    x = (screen_w - w) / 2;
    y = (screen_h - h) / 2;
    opts.canvas.style.left = x+'px';
    opts.canvas.style.top = y+'px';
    opts.canvas.width = w;
    opts.canvas.height = h;
    console.log(screen_w, screen_h, w, h, x, y);

    //opts.canvas.width = config.width;
    //opts.canvas.height = config.height;
    self.yuv = new frameSink(opts.canvas, config);

    // rect.x = x;
    // rect.y = y;
    // rect.w = w;
    // rect.h = h;

    self.yuv_format = YUVBuffer.format({
      // Encoded size
      width: config.coded_width,
      height: config.coded_height,

      // 4:2:0, so halve the chroma dimensions.
      chromaWidth: config.coded_width/2,
      chromaHeight: config.coded_height/2,

      // Full frame is visible. ?
      cropLeft: 0,
      cropTop: 0,
      //cropWidth: config.width,
      //cropHeight: config.height,

      // Final display size stretches back out to 16:9 widescreen:
      displayWidth: config.width,
      displayHeight: config.height
      // y: {data:y, stride:yS},
      // u: {data:u, stride:uS},
      // v: {data:v, stride:vS}
    });

  }

  this.digestFrame = function(y,u,v,yS,uS,vS,pts){
    var yuv_frame = {
      pts: pts,
      yuv: YUVBuffer.frame(
        self.yuv_format,
        YUVBuffer.lumaPlane(self.yuv_format, y, yS, 0),
        YUVBuffer.chromaPlane(self.yuv_format, u, uS, 0),
        YUVBuffer.chromaPlane(self.yuv_format, v, vS, 0)
      )
    };
    // self.f = yuv_frame;
    self.fbq.push(yuv_frame);

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

function pad(n){
  return Math.ceil(n/2) * 2;
}
