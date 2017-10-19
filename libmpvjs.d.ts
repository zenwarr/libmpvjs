/// <reference types="node" />

declare namespace libmpvjs {
  class MpvPlayer {
    constructor(canvas: HTMLCanvasElement);

    create(): void;
    command(name: string, ...args: string[]): void;
    getProperty(name: string): any;
    setProperty(name: string, value: any): void;
  }
}

export = libmpvjs;
