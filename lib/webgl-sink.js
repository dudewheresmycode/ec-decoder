
var shaders = {
  vertex: "attribute vec2 aPosition;\nattribute vec2 aLumaPosition;\nattribute vec2 aChromaPosition;\nvarying vec2 vLumaPosition;\nvarying vec2 vChromaPosition;\nvoid main() {\n    gl_Position = vec4(aPosition, 0, 1);\n    vLumaPosition = aLumaPosition;\n    vChromaPosition = aChromaPosition;\n}\n",
  fragment: "// inspired by https://github.com/mbebenita/Broadway/blob/master/Player/canvas.js\n\nprecision mediump float;\nuniform sampler2D uTextureY;\nuniform sampler2D uTextureCb;\nuniform sampler2D uTextureCr;\nvarying vec2 vLumaPosition;\nvarying vec2 vChromaPosition;\nvoid main() {\n   // Y, Cb, and Cr planes are uploaded as LUMINANCE textures.\n   float fY = texture2D(uTextureY, vLumaPosition).x;\n   float fCb = texture2D(uTextureCb, vChromaPosition).x;\n   float fCr = texture2D(uTextureCr, vChromaPosition).x;\n\n   // Premultipy the Y...\n   float fYmul = fY * 1.1643828125;\n\n   // And convert that to RGB!\n   gl_FragColor = vec4(\n     fYmul + 1.59602734375 * fCr - 0.87078515625,\n     fYmul - 0.39176171875 * fCb - 0.81296875 * fCr + 0.52959375,\n     fYmul + 2.017234375   * fCb - 1.081390625,\n     1\n   );\n}\n",
  fragmentStripe: "// inspired by https://github.com/mbebenita/Broadway/blob/master/Player/canvas.js\n// extra 'stripe' texture fiddling to work around IE 11's poor performance on gl.LUMINANCE and gl.ALPHA textures\n\nprecision mediump float;\nuniform sampler2D uStripeLuma;\nuniform sampler2D uStripeChroma;\nuniform sampler2D uTextureY;\nuniform sampler2D uTextureCb;\nuniform sampler2D uTextureCr;\nvarying vec2 vLumaPosition;\nvarying vec2 vChromaPosition;\nvoid main() {\n   // Y, Cb, and Cr planes are mapped into a pseudo-RGBA texture\n   // so we can upload them without expanding the bytes on IE 11\n   // which doesn\\'t allow LUMINANCE or ALPHA textures.\n   // The stripe textures mark which channel to keep for each pixel.\n   vec4 vStripeLuma = texture2D(uStripeLuma, vLumaPosition);\n   vec4 vStripeChroma = texture2D(uStripeChroma, vChromaPosition);\n\n   // Each texture extraction will contain the relevant value in one\n   // channel only.\n   vec4 vY = texture2D(uTextureY, vLumaPosition) * vStripeLuma;\n   vec4 vCb = texture2D(uTextureCb, vChromaPosition) * vStripeChroma;\n   vec4 vCr = texture2D(uTextureCr, vChromaPosition) * vStripeChroma;\n\n   // Now assemble that into a YUV vector, and premultipy the Y...\n   vec3 YUV = vec3(\n     (vY.x  + vY.y  + vY.z  + vY.w) * 1.1643828125,\n     (vCb.x + vCb.y + vCb.z + vCb.w),\n     (vCr.x + vCr.y + vCr.z + vCr.w)\n   );\n   // And convert that to RGB!\n   gl_FragColor = vec4(\n     YUV.x + 1.59602734375 * YUV.z - 0.87078515625,\n     YUV.x - 0.39176171875 * YUV.y - 0.81296875 * YUV.z + 0.52959375,\n     YUV.x + 2.017234375   * YUV.y - 1.081390625,\n     1\n   );\n}\n"
};

(function() {
	"use strict";

  function WebGLFrameSink(canvas, opts) {

    var self = this,
			gl = canvas.getContext('webgl') || canvas.getContext('experimental-webgl'),
			debug = false; // swap this to enable more error checks, which can slow down rendering

      var cubeTexture,
          buffer,
          squareVerticesBuffer,
          vertexShader,
    			fragmentShader,
    			program,
    			buf,
    			err,
          horizAspect = 480.0/640.0;

    var textures = {};

		if (gl === null) {
			throw new Error('WebGL unavailable');
		}

    initBuffers();


		// GL!
		function checkError() {
			if (debug) {
				err = gl.getError();
				if (err !== 0) {
					throw new Error("GL error " + err);
				}
			}
		}


    self.drawFrame = function(buffer){

      var rectangle = new Float32Array([
  			// First triangle (top left, clockwise)
  			-1.0, -1.0,
  			+1.0, -1.0,
  			-1.0, +1.0,

  			// Second triangle (bottom right, clockwise)
  			-1.0, +1.0,
  			+1.0, -1.0,
  			+1.0, +1.0
  		]);

      var format = buffer.format;

			// if (canvas.width !== format.displayWidth || canvas.height !== format.displayHeight) {
			// 	// Keep the canvas at the right size...
			// 	canvas.width = format.displayWidth;
			// 	canvas.height = format.displayHeight;
			// 	self.clear();
			// }

      buf = gl.createBuffer();
			checkError();

			gl.bindBuffer(gl.ARRAY_BUFFER, buf);
			checkError();

			gl.bufferData(gl.ARRAY_BUFFER, rectangle, gl.STATIC_DRAW);
			checkError();

      var positionLocation = gl.getAttribLocation(program, 'aPosition');
			checkError();

			gl.enableVertexAttribArray(positionLocation);
			checkError();

			gl.vertexAttribPointer(positionLocation, 2, gl.FLOAT, false, 0, 0);
			checkError();

      setupTexturePosition('aLumaPosition', buffer.y.stride, format);
			setupTexturePosition('aChromaPosition', buffer.u.stride * format.width / format.chromaWidth, format);


      function paddStride(stride){
        var unpackAlignment = gl.getParameter(gl.UNPACK_ALIGNMENT);
        //var BpP = (type=='y'?8:2); //bytes/pixel
        var unpaddedRowSize = stride; //1 bytes per pixel?
        return Math.floor((stride + unpackAlignment - 1) / unpackAlignment)  * unpackAlignment;
      }
      function mallocArr(b,type){
        var unpackAlignment = gl.getParameter(gl.UNPACK_ALIGNMENT);
        //var BpP = (type=='y'?8:2); //bytes/pixel
        var paddedRowSize = paddStride(b.stride);
        var sizeNeeded = paddedRowSize * (format.height - 1) + paddedRowSize;
        // var sizeNeeded = parseInt(b.bytes.length);

        var arr = new Uint8Array(sizeNeeded);
        for(var i=0;i<sizeNeeded;i++){
          arr[i] = b.bytes[i] || 0;
        }
        return arr;
      }
      //
      // var arr_y = new Uint8Array(sizeNeeded);
      // for(var i=0;i<sizeNeeded;i++){
      //   arr_y[i] = buffer.y.bytes[i]
      // }
      //
      // var arr_u = new Uint8Array(sizeNeeded/2);
      // for(var i=0;i<(sizeNeeded/2);i++){
      //   arr_u[i] = buffer.u.bytes[i]
      // }
      //
      // var arr_v = new Uint8Array(sizeNeeded/2);
      // for(var i=0;i<(sizeNeeded/2);i++){
      //   arr_v[i] = buffer.v.bytes[i]
      // }
      // Create the textures...
			var textureY = attachTexture(
				'uTextureY',
				gl.TEXTURE0,
				0,
        buffer.y.stride,
				format.height,
				buffer.y.bytes
			);
			var textureCb = attachTexture(
				'uTextureCb',
				gl.TEXTURE1,
				1,
        buffer.u.stride,
				format.chromaHeight,
        buffer.u.bytes
				//buffer.u.bytes
			);
			var textureCr = attachTexture(
				'uTextureCr',
				gl.TEXTURE2,
				2,
				paddStride(buffer.v.stride),
				format.chromaHeight,
        buffer.v.bytes
				//buffer.v.bytes
			);

			// Aaaaand draw stuff.
			gl.drawArrays(gl.TRIANGLES, 0, rectangle.length / 2);
			checkError();
    }
    self.clear = function(){
      console.log('clear!');
    }


    function compileShader(type, source) {
			var shader = gl.createShader(type);
			gl.shaderSource(shader, source);
			gl.compileShader(shader);

			if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
				var err = gl.getShaderInfoLog(shader);
				gl.deleteShader(shader);
				throw new Error('GL shader compilation for ' + type + ' failed: ' + err);
			}

			return shader;
		}



    function initBuffers() {
      vertexShader = compileShader(gl.VERTEX_SHADER, shaders.vertex);
      fragmentShader = compileShader(gl.FRAGMENT_SHADER, shaders.fragment);

      program = gl.createProgram();
			gl.attachShader(program, vertexShader);
			checkError();

			gl.attachShader(program, fragmentShader);
			checkError();

      gl.linkProgram(program);
			if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
				var err = gl.getProgramInfoLog(program);
				gl.deleteProgram(program);
				throw new Error('GL program linking failed: ' + err);
			}
      gl.useProgram(program);
			checkError();

      squareVerticesBuffer = gl.createBuffer();
      gl.bindBuffer(gl.ARRAY_BUFFER, squareVerticesBuffer);

      var vertices = [
        1.0,  1.0,  0.0,
        -1.0, 1.0,  0.0,
        1.0,  -1.0, 0.0,
        -1.0, -1.0, 0.0
      ];

      gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(vertices), gl.STATIC_DRAW);

    }

    // function initTextures() {
    //   cubeTexture = gl.createTexture();
    //   gl.bindTexture(gl.TEXTURE_2D, cubeTexture);
    //   gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    //   gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    //   gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    //   gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    //
    // }

    function updateTexture() {

      var buffer = gl.createBuffer();
      gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
      checkError();

      gl.bindTexture(gl.TEXTURE_2D, cubeTexture);
      gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, true);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA,
            gl.UNSIGNED_BYTE, videoElement);
    }


    function setupTexturePosition(varname, texWidth, format) {
      // Warning: assumes that the stride for Cb and Cr is the same size in output pixels
      var textureX0 = format.cropLeft / texWidth;
      var textureX1 = (format.cropLeft + format.cropWidth) / texWidth;
      var textureY0 = (format.cropTop + format.cropHeight) / format.height;
      var textureY1 = format.cropTop / format.height;
      var textureRectangle = new Float32Array([
        textureX0, textureY0,
        textureX1, textureY0,
        textureX0, textureY1,
        textureX0, textureY1,
        textureX1, textureY0,
        textureX1, textureY1
      ]);

      var texturePositionBuffer = gl.createBuffer();
      gl.bindBuffer(gl.ARRAY_BUFFER, texturePositionBuffer);
      checkError();

      gl.bufferData(gl.ARRAY_BUFFER, textureRectangle, gl.STATIC_DRAW);
      checkError();

      var texturePositionLocation = gl.getAttribLocation(program, varname);
      checkError();

      gl.enableVertexAttribArray(texturePositionLocation);
      checkError();

      gl.vertexAttribPointer(texturePositionLocation, 2, gl.FLOAT, false, 0, 0);
      checkError();
    }


    function attachTexture(name, register, index, width, height, data) {
			var texture,
				texWidth = WebGLFrameSink.stripe ? (width / 4) : width,
				format = WebGLFrameSink.stripe ? gl.RGBA : gl.LUMINANCE,
				filter = WebGLFrameSink.stripe ? gl.NEAREST : gl.LINEAR;

			if (textures[name]) {
				// Reuse & update the existing texture
				texture = textures[name];
			} else {
				textures[name] = texture = gl.createTexture();
				checkError();

				gl.uniform1i(gl.getUniformLocation(program, name), index);
				checkError();
			}
			gl.activeTexture(register);
			checkError();
			//texture.generateMipmaps = false;
			gl.bindTexture(gl.TEXTURE_2D, texture);
			checkError();
			gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
			checkError();
			gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
			checkError();
			gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, filter);
			//checkError();
			//gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, filter);


			checkError();
			//console.log("THIS ONE");
			gl.texImage2D(
				gl.TEXTURE_2D,
				0, // mip level
				format, // internal format
				texWidth,
				height,
				0, // border
				format, // format
				gl.UNSIGNED_BYTE, //type
				data // data!
			);
			checkError();

			return texture;
		}

  }

	module.exports = WebGLFrameSink;
})();
