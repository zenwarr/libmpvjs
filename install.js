const process = require('process');
const path = require('path');
const prebuild_install = require('prebuild-install');
const abi = require('node-abi');
const os = require('os');
const log = require('npmlog');

log.heading = 'libmpvjs-prebuild';

let electron_path;

try {
  electron_path = path.join(require.resolve('electron'), '..');
} catch (err) {
  log.error('libmpvjs', 'Failed to determine electron version. This module only works inside electron renderer process, and you should have electron package installed first for libmpvjs to be able to install corresponding prebuilt binary version');
  process.exit(-1);
}

let electron_pkg = require(path.join(electron_path, 'package.json'));

prebuild_install.download({
  path: path.join(__dirname, 'module_dist'),
  runtime: 'electron',
  target: electron_pkg.version,
  arch: os.arch(),
  platform: os.platform(),
  abi: abi.getAbi(electron_pkg.version, 'electron'),
  pkg: require(path.join(__dirname, 'package.json')),
  log
}, err => {
  if (err) {
    console.error(err);
    process.exit(-1);
  }
});
