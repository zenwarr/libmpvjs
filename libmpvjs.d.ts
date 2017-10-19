/// <reference types="node" />

declare namespace libmpvjs {
  enum EndFileReason {
    Eof = 0,
    Stop = 2,
    Quit = 3,
    Error = 4,
    Redirect = 5
  }

  enum ErrorCode {
    Success = 0,
    QueueFull = -1,
    NoMem = -2,
    Uninitialized = -3,
    InvalidParameter = -4,
    OptionNotFound = -5,
    OptionFormat = -6,
    OptionError = -7,
    PropertyNotFound = -8,
    PropertyFormat = -9,
    PropertyUnavailable = -10,
    PropertyError = -11,
    Command = -12,
    LoadingFailed = -13,
    AoInitFailed = -14,
    VoInitFailed = -15,
    NothingToPlay = -16,
    UnknownFormat = -17,
    Unsupported = -18,
    NotImplemented = -19,
    Generic = -20
  }

  enum LogLevel {
    None = 0,
    Fatal = 10,
    Error = 20,
    Warn = 30,
    Info = 40,
    Verbose = 50,
    Debug = 60,
    Trace = 70
  }

  interface PlayerOptions {
    onLog?: (text: string, level: LogLevel, prefix: string) => void;
    onFileStart?: () => void;
    onFileEnd?: (reason: EndFileReason, error_code: ErrorCode) => void;
    onFileLoaded?: () => void;
    onIdle?: () => void;
    onVideoReconfig?: () => void;
    onAudioReconfig?: () => void;
    onSeek?: () => void;
    onPlaybackRestart?: () => void;
    onQueueOverflow?: () => void;
    logLevel: string;
  }

  class MpvPlayer {
    constructor(canvas: HTMLCanvasElement, options: PlayerOptions);
    dispose(): void;

    create(): void;
    command(name: string, ...args: any[]): any;
    getProperty(name: string): any;
    setProperty(name: string, value: any): void;
  }
}

export = libmpvjs;
