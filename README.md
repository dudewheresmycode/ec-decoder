The Extracast decoder was designed to work with [Electron](http://electron.atom.io/). To get started, clone the quickstart app and then we'll make a few modifications.

**Quickstart an Electron App**

```bash
git clone https://github.com/electron/electron-quick-start
cd electron-quick-start
#install dependenices
npm install
#run the sample app
npm start
```

Then clone and install ec-decoder.
```bash
npm install https://github.com/dudewheresmycode/ec-decoder
```

Now we can build a simple player with our electron app.

**Example `render.js`**
```javascript
var ecWebGLPlayer = require('./ec-decoder/').WebGLPlayer;

var canvas = document.getElementById('render');
var statsEle = document.getElementById('stats');

//attach the canvas to our WebGL player
var webgl = new ecWebGLPlayer({canvas:canvas});
webgl.on('debug', function(stats){
  statsEle.innerHTML = stats;
});

//start a decode on file select
var fileinput = document.getElementById('fileid');
fileinput.addEventListener('change',function(e){
  console.log(this.files[0]);
  webgl.start(this.files[0].path);
  this.value='';
});

```
**Example `index.html`**

```html
<!DOCTYPE html>
<html>
  <head>
    <meta charset="UTF-8">
    <title>Hello World!</title>
    <style>
    body {
      margin: 0;
    }
    #player {
      position: relative;
      background: #333;
      width: 854px;
      height: 480px;
      box-sizing: content-box;
      border: 1px solid #000;
    }
    #render {
      position: absolute;
      top:0;
      left: 0;
      background:#000;
      box-sizing: border-box;;
    }
    #stats {
      position: absolute;
      bottom: 10px;
      left: 10px;
      color: #fff;
      background: rgba(0,0,0,0.35);
      right: 10px;

    }
    </style>
  </head>
  <body>
    <div>

      <h1>Hello World!</h1>
      <!-- All of the Node.js APIs are available in this renderer process. -->
      <pre>Node <script>document.write(process.versions.node)</script> / Chromium <script>document.write(process.versions.chrome)</script> / Electron <script>document.write(process.versions.electron)</script>.</pre>
    </div>
    <div>
      <p>
        <input type="file" id="fileid" />
      </p>
      <div id="player">
        <canvas id="render" width="854" height="480"></canvas>
        <pre id="stats"></pre>
      </div>
    </div>
  </body>

  <script>
    // You can also require other files to run in this process
    require('./renderer.js')
  </script>
</html>
```
