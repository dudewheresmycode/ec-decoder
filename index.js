//var Emitter = require('bindings')('event-emitter.node').Emitter;
var Emitter = require('./build/Release/ec_decoder').Emitter;
var events = require('events');
var util = require('util');


util.inherits(Emitter, events.EventEmitter);
exports.Emitter = Emitter;
