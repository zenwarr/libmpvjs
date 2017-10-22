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
    logLevel?: string;
  }

  type PropertyObserver = (value: any) => void;

  class MpvPlayer {
    constructor(canvas: HTMLCanvasElement, options: PlayerOptions);
    dispose(): void;

    create(): void;
    command(name: string, ...args: any[]): any;
    getProperty(name: string): any;
    setProperty(name: string, value: any): void;
    observeProperty(name: string, handler: PropertyObserver): void;
    cmds: CommandInterface;
  }

  type OptionsMap = { [name: string]: any };

  interface CommandInterface {
    ignore(): void;
    seek(seconds: number, seek_mode?: SeekMode): void;
    revert_seek(mode?: RevertSeekMode): void;
    frame_step(): void;
    frame_back_step(): void;
    set(property: string, value: any): void;
    add(property: string, value?: any): void;
    cycle(property: string, mode?: CyclePropertyMode): void;
    multiply(property: string, factory: number): void;
    screenshot(mode?: ScreenshotMode): void;
    screenshot_to_file(filename: string, mode?: ScreenshotMode): void;
    playlist_next(mode?: PlaylistJumpMode): void;
    loadfile(file: string, mode?: LoadFileMode, options?: OptionsMap): string;
    loadlist(playlist: string, mode?: PlaylistLoadMode): void;
    playlist_clear(): void;
    playlist_remove(what: number|PlaylistItem): void;
    playlist_move(index1: number, index2: number): void;
    playlist_shuffle(): void;
    run(command: string, ...args: string[]): void;
    quit(code?: number): void;
    quit_watch_later(code?: number): void;
    sub_add(file: string, flags?: LoadFlag, title?: string, lang?: string): void;
    sub_remove(id: number): void;
    sub_reload(id: number): void;
    sub_step(skip: number): void;
    sub_seek(skip: number): void;
    print_text(text: string): void;
    show_text(text: string, duration: number, level: number): void;
    expand_text(text: string): string;
    show_progress(): void;
    write_watch_later_config(): void;
    stop(): void;
    mouse(x: number, y: number, button?: number, mode?: MouseClickMode): void;
    keypress(key_name: string): void;
    keydown(key_name: string): void;
    keyup(key_name: string): void;
    audio_add(file: string, flags?: LoadFlag, title?: string, lang?: string): void;
    audio_remove(id: number): void;
    audio_reload(id: number): void;
    rescan_external_files(mode?: RescanMode): void;
  }

  enum SeekMode {
    Relative = 'relative',
    Absolute = 'absolute',
    AbsolutePercent = 'absolute-percent',
    RelativePercent = 'relative-percent',
    Keyframes = 'keyframes',
    Exact = 'exact'
  }

  enum RevertSeekMode {
    Mark = "mark"
  }

  enum CyclePropertyMode {
    Up = 'up',
    Down = 'down'
  }

  enum ScreenshotMode {
    Subtitles = 'subtitles',
    Video = 'video',
    Window = 'window',
    EachFrame = 'each-frame'
  }

  enum PlaylistJumpMode {
    Weak = 'weak',
    Force = 'force'
  }

  enum LoadFileMode {
    Replace = 'replace',
    Append = 'append',
    AppendPlay = 'append-play'
  }

  enum PlaylistLoadMode {
    Replace = 'replace',
    Append = 'append'
  }

  enum PlaylistItem {
    Current = 'current'
  }

  enum LoadFlag {
    Select = 'select',
    Auto = 'auto',
    Cached = 'cached'
  }

  enum MouseClickMode {
    Single = 'single',
    Double = 'double'
  }

  enum RescanMode {
    Reselect = 'reselect',
    KeepSelection = 'keep-selection'
  }
}

export = libmpvjs;
