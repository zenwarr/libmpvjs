import * as React from 'react';
import * as ReactDOM from 'react-dom';
import {getStore} from "./store";
import {App} from "./components/app/app";

let mpv = require('../../module/build/Release/mpv-webgl.node');

(window as any).mpv = mpv;

let canvas = document.createElement('canvas');
canvas.style.width = "640px";
canvas.style.height = "360px";
document.body.appendChild(canvas);

let p: any = new (window as any).mpv.MpvPlayer(canvas);
console.log('player object created');
console.log(p);
p.create();
p.command("loadfile", "/home/victor/Downloads/mrrobot/mrrobot.mkv");

(window as any).player = p;

let appRoot = document.createElement('div');
document.body.appendChild(appRoot);

let store = getStore();

ReactDOM.render(
    <App s={store} />,
    appRoot
);
