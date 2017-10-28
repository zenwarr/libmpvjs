"use strict";

const path = require('path');
const os = require('os');
const fs = require('fs');
const process = require('process');
const nugget = require('nugget');
const tar = require('tar');
const child_process = require('child_process');

const WIN_DEPS_BASE_URL = 'https://github.com/zenwarr/libmpvjs/releases/download/v';
const WIN_DEPS_ARCHIVE = 'win_deps.tar.gz';

function exists(path) {
  try {
    fs.statSync(path);
    return true;
  } catch (err) {
    if (err.code === 'ENOENT') {
      return false;
    } else {
      throw err;
    }
  }
}

function isEmpty(path) {
  let result = fs.readdirSync(path);
  return result.length === 0;
}

async function resolve_deps_win() {
  return new Promise((resolve, reject) => {
    const package_json = require(path.join(__dirname, 'package.json'));
    let version = package_json.version;
    let dep_url = WIN_DEPS_BASE_URL + version + '/' + WIN_DEPS_ARCHIVE;

    let temp_dir = path.join(os.tmpdir(), 'libmpvjs-tmp-' + (new Date()).getTime());
    fs.mkdirSync(temp_dir);

    let out_dir = path.join(__dirname, 'deps');
    let out_dir_stat = null;
    try {
      out_dir_stat = fs.statSync(out_dir);
    } catch (err) {
      if (err.code !== 'ENOENT') {
        reject(err);
        return;
      }
    }

    if (out_dir_stat && !out_dir_stat.isDirectory()) {
      reject(new Error(`out path ${out_dir} exists, but it is not a directory`));
      return;
    }

    if (!out_dir_stat) {
      fs.mkdirSync(out_dir);
    } else if (!isEmpty(out_dir) && exists(path.join(out_dir, 'win_libs'))) {
      console.log(`Output directory ${out_dir} exists and it is not empty, assuming dependencies already resolved, skipping...`);
      resolve();
      return;
    }

    console.log("Downloading dependencies archive...");
    nugget(dep_url, {
      dir: temp_dir
    }, err => {
      if (err) {
        reject(err);
        return;
      }

      console.log('Unpacking downloaded archive...');
      fs.createReadStream(path.join(temp_dir, WIN_DEPS_ARCHIVE))
          .pipe(new tar.Unpack.Sync({
            cwd: path.join(__dirname, 'deps')
          }))
          .on('error', reject)
          .on('end', resolve);
    });
  });
}

async function resolve_deps_linux() {
  const tasks = [
    {
      cmd: 'git',
      condition: function() {
        return !exists(path.join(__dirname, 'deps/mpv-build'))
      },
      args: [ 'clone', 'https://github.com/mpv-player/mpv-build.git', '--depth=1', './deps/mpv-build' ]
    },
    {
      cmd: 'chmod',
      args: [ '+x', './build-mpv' ]
    },
    {
      cmd: './build-mpv',
      condition: function() {
        return !exists(path.join(__dirname, 'deps/mpv-build/mpv/build/libmpv.a'));
      }
    }
  ];

  return new Promise((resolve, reject) => {
    let cur_task_index = 0;

    function next_task() {
      if (cur_task_index >= tasks.length) {
        resolve();
        return;
      }

      let task = tasks[cur_task_index];
      let cmd = task.cmd + ' ' + (task.args ? task.args.join(' ') : '');

      if (task.condition && !task.condition()) {
        console.log(`Skipping command ${cmd}...`);

        ++cur_task_index;
        next_task();
      } else {
        console.log(`Executing command ${cmd}...`);

        let proc = child_process.spawn(task.cmd, task.args);
        proc.stdout.pipe(process.stdout);
        proc.stderr.pipe(process.stderr);
        proc.on('exit', code => {
          if (code !== 0) {
            reject(new Error(`Error while executing command ${cmd}`));
            return;
          }

          ++cur_task_index;
          next_task();
        });
      }
    }

    next_task();
  });
}

let resolve_func;

switch (os.platform()) {
  case 'win32':
    resolve_func = resolve_deps_win;
    break;

  case 'linux':
    resolve_func = resolve_deps_linux;
    break;

  default:
    console.error(`Unsupported platform ${os.platform()}`);
    process.exit(-1);
}

resolve_func().then(() => {
  console.log("Dependecies resolved");
}, err => {
  console.error("Error while resolving dependencies: ", err);
  process.exit(-1);
});
