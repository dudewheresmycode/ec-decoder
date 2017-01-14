var decode = require('./build/Release/ec_decoder');
var input = "/Users/addroid/Movies/SampleVideo_720x480_1mb.mp4";

var res = decode.config(input);
console.log(res);
setTimeout(function(){
  decode.start(input,
    function(d){
      console.log('progress', d);
    },
    function(d){
      console.log('complete', d);
    }
  );
  // decode.frame(0, function(y){
  //   //var buf = Buffer.from(y);
  //   console.log('callback:', y);
  //   //console.log(y,u,v);
  // });


},2000);
