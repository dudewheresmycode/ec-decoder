//var Emitter = require('bindings')('event-emitter.node').Emitter;
var Decoder = require('../build/Release/ec_decoder').Emitter;
var EventEmitter = require('events').EventEmitter;
var util = require('util');


util.inherits(Decoder, EventEmitter);
module.exports = Decoder;
