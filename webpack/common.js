var fs = require('fs');

const ignoredDirs = ['.bin'];

module.exports.buildExternals = function() {
  var nodeModules = {};
  fs.readdirSync('node_modules').filter(function(x) {
    return ignoredDirs.indexOf(x) === -1;
  }).forEach(function(mod) {
    nodeModules[mod] = mod;
  });

  return nodeModules;
};
