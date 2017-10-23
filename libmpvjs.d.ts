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
    props: PropsInterface;
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

    // the following commands are marked as possible subjects to change
    af(mode: FilterChangeMode, params: string): void;
    vf(mode: FilterChangeMode, params: string): void;
  }

  enum FilterChangeMode {
    Set = 'set',
    Add = 'add',
    Toggle = 'toggle',
    Del = 'del',
    Clr = 'clr'
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

  interface PropsInterface extends Options {
    readonly audio_speed_correction: number;
    readonly video_speed_correction: number;
    readonly display_sync_active: boolean;
    readonly filename: string;
    readonly $filename: {
      readonly no_ext: string
    };
    readonly file_size: number;
    readonly estimated_frame_count: number;
    readonly estimated_frame_number: number;
    readonly path: string;
    readonly media_title: string;
    readonly file_format: string;
    readonly current_demuxer: string;
    readonly stream_path: string;
    readonly stream_pos: number;
    readonly stream_end: number;
    readonly duration: number;
    readonly avsync: number;
    readonly total_avsync_change: number;
    readonly decoder_frame_drop_count: number;
    readonly frame_drop_count: number;
    readonly mistimed_frame_count: number;
    readonly vsync_ratio: number;
    readonly vo_delayed_frame_count: number;
    percent_pos: number;
    time_pos: number;
    readonly time_remaining: number;
    readonly audio_pts: number;
    readonly playtime_remaining: number;
    playback_time: number;
    chapter: number;
    readonly disc_titles: DiscTitles;
    readonly disc_title_list: DiscTitle[];
    disc_title: string;
    readonly chapters: number;
    readonly editions: number;
    readonly edition_list: {
      count: number;
      [index: number]: Edition;
    };
    readonly angle: number;
    readonly metadata: Metadata;
    readonly filtered_metadata: Metadata;
    readonly chapter_metadata: Metadata;
    //todo: vf-metadata, af-metadata
    readonly idle_active: boolean;
    readonly core_idle: boolean;
    readonly cache: number|string;
    cache_size: number;
    readonly cache_free: number;
    readonly cache_used: number;
    readonly cache_speed: number;
    readonly cache_idle: boolean;
    readonly demuxer_cache_duration: number;
    readonly demuxer_cache_time: number;
    readonly demuxer_cache_idle: number;
    readonly demuxer_via_network: boolean;
    readonly demuxer_start_time: number;
    readonly paused_for_cache: boolean;
    readonly cache_buffering_state: number;
    readonly eof_reached: boolean;
    readonly seeking: boolean;
    readonly mixer_active: boolean;
    ao_volume: number;
    ao_mute: boolean;
    readonly audio_codec: string;
    readonly audio_codec_name: string;
    readonly audio_params: AudioParams;
    readonly audio_out_params: AudioParams;
    readonly colormatrix: string;
    readonly colormatrix_input_range: string;
    readonly colormatrix_primaries: string;
    hwdec: string;
    readonly hwdec_current: string;
    readonly hwdec_interop: string;
    readonly video_format: string;
    readonly video_codec: string;
    readonly width: number;
    readonly height: number;
    readonly video_params: VideoParams;
    readonly dwidth: number;
    readonly dheight: number;
    readonly video_dec_params: VideoParams;
    readonly video_out_params: VideoParams;
    readonly video_frame_info: VideoFrameInfo;
    readonly container_fps: number;
    readonly estimated_vf_fps: number;
    window_scale: number;
    readonly window_minimized: number;
    readonly display_names: string;
    display_fps: number;
    readonly estimated_display_fps: number;
    readonly vsync_jitter: number;
    video_aspect: number;
    readonly osd_width: number;
    readonly osd_height: number;
    readonly osd_par: number;
    program: string; // write-only
    dvb_channel: string; // write-only
    dvb_channel_name: string; // write-only
    readonly sub_text: string;
    tv_brightness: number;
    tv_contrast: number;
    tv_saturation: number;
    tv_hue: number;
    playlist_pos: number;
    playlist_pos_1: number;
    playlist_count: number;
    readonly playlist: Playlist;
    readonly track_list: TrackList;
    readonly chapter_list: ChapterList;
    readonly af: FilterData;
    readonly vf: FilterData;
    readonly seekable: boolean;
    readonly partially_seekable: boolean;
    readonly playback_abort: boolean;
    cursor_autohide: boolean;
    readonly osd_sym_cc: string;
    readonly osd_ass_cc: {
      readonly 0: string;
      readonly 1: string;
    };
    readonly vo_configured: boolean;
    // readonly vo_passes: VoPasses;
    readonly video_bitrate: number;
    readonly audio_bitrate: number;
    readonly sub_bitrate: number;
    readonly packed_video_bitrate: number;
    readonly packet_audio_bitrate: number;
    readonly packed_sub_bitrate: number;
    readonly audio_device_list: AudioDevice[];
    audio_device: string;
    readonly current_vo: string;
    readonly current_ao: string;
    readonly audio_out_detected_device: string;
    readonly working_directory: string;
    readonly protocol_list: string[];
    readonly decoder_list: Coder[];
    readonly encoder_list: Coder[];
    readonly mpv_version: string;
    readonly mpv_configuration: string;
    readonly ffmpeg_version: string;
    readonly options: {
      readonly [name: string]: any;
    };
    readonly $options: {
      [name: string]: any;
    };
    readonly file_local_options: {
      readonly [name: string]: any;
    };
    readonly $file_local_options: {
      [name: string]: any;
    };
    readonly $option_info: {
      [name: string]: OptionInfo;
    };
    readonly property_list: string[];
    readonly profile_list: any;
  }

  interface Options {
    alang: string|string[];
    slang: string|string[];
    aid: number|string;
    sid: number|string;
    vid: number|string;
    ff_aid: number|string;
    ff_sid: number|string;
    ff_vid: number|string;
    edition: number|string;
    track_auto_selection: boolean;
    start: string;
    end: string;
    length: string;
    rebase_start: boolean;
    speed: number;
    pause: boolean;
    shuffle: boolean;
    // chapter: string;
    playlist_start: number|string;
    // playlist: string;
    chapter_merge_treshold: number;
    chapter_seek_treshold: number;
    hr_seek: HrSeekMode|string;
    hr_seek_demuxer_offset: number;
    hr_seek_framedrop: boolean;
    index: IndexMode|string;
    load_unsafe_playlists: boolean;
    access_references: boolean;
    loop_playlist: number|LoopMode;
    loop_file: number|LoopFileMode;
    ab_loop_a: number;
    ab_loop_b: number;
    ordered_chapters: boolean;
    ordered_chapters_files: string;
    chapters_file: string;
    sstep: number;
    stop_playback_on_init_failure: boolean;
    log_file: string;
    config_dir: string;
    save_position_on_quit: boolean;
    load_scripts: boolean;
    merge_files: boolean;
    no_resume_playback: boolean;
    reset_on_next_file: string|string[];
    write_filename_in_watch_later_config: boolean;
    ignore_path_in_watch_later_config: boolean;
    show_profile: string;
    use_filedir_conf: boolean;
    ytdl: boolean;
    ytdl_format: string;
    ytdl_raw_options: { [name: string]: string };
  }

  enum LoopMode {
    Inf = 'inf',
    Force = 'force',
    No = 'no'
  }

  enum LoopFileMode {
    Inf = 'inf',
    No = 'no'
  }

  enum IndexMode {
    Default = 'default',
    Recreate = 'recreate'
  }

  enum HrSeekMode {
    No = 'no',
    Absolute = 'absolute',
    Yes = 'yes'
  }

  enum AutoOption {
    Auto = 'auto'
  }

  enum TrackOption {
    No = 'no',
    Auto = 'auto'
  }

  interface OptionInfo {
    name: string;
    type: string;
    set_from_commandline: boolean;
    set_locally: boolean;
    default_value: any;
    min?: number;
    max?: number;
    choices?: string[];
  }

  interface Coder {
    family: string;
    codec: string;
    driver: string;
    description: string;
  }

  interface AudioDevice {
    name: string;
    description: string;
  }

  interface Countable {
    count: number;
  }

  interface Playlist extends Countable {
    [index: number]: PlaylistItemData;
  }

  interface PlaylistItemData {
    filename: string;
    current?: boolean;
    playing: boolean;
    title: string;
  }

  interface TrackList extends Countable {
    [index: number]: TrackListItem;
  }

  interface TrackListItem {
    id: number;
    type: string;
    src_id: number;
    title: string;
    lang: string;
    albumart: boolean;
    default: boolean;
    forced: boolean;
    selected: boolean;
    external: boolean;
    external_filename: string;
    codec: string;
    ff_index: number;
    decoder_desc: string;
    demux_w: number;
    demux_h: number;
    demux_channel_count: number;
    demux_channels: string;
    demux_samplerate: number;
    demux_fps: number;
    audio_channels: number;
    replaygain_track_peak: number;
    replaygain_track_gain: number;
    replaygain_album_peak: number;
    replaygain_album_gain: number;
  }

  interface ChapterList extends Countable {
    [index: number]: ChapterListItem;
  }

  interface ChapterListItem {
    title: string;
    time: number;
  }

  interface FilterData {
    [name: string]: FilterEntry;
  }

  interface FilterEntry {
    name: string;
    label?: string;
    enabled?: boolean;
    params: ParamMap;
  }

  interface ParamMap {
    [name: string]: {
      key: string;
      value: string;
    }
  }

  interface DiscTitles extends Countable {
    [index: number]: DiscTitle;
  }

  interface DiscTitle {
    id: number;
    length: number;
  }

  interface Edition {
    id: number;
    title: string;
    default: boolean;
  }

  interface AudioParams {
    format: string;
    samplerate: number;
    channels: string;
    hr_channels: string;
    channel_count: number;
  }

  interface VideoParams {
    pixelformat: string;
    w: number;
    h: number;
    dw: number;
    dh: number;
    aspect: number;
    par: number;
    colormatrix: string;
    colorlevels: string;
    primaries: string;
    gamma: string;
    sig_peak: number;
    light: string;
    chroma_location: string;
    rotate: number;
    stereo_in: string;
  }

  interface VideoFrameInfo {
    picture_type: string;
    interlaced: boolean;
    tff: boolean;
    repeat: boolean;
  }

  interface Metadata {
    by_key: {
      [name: string]: string;
    };
    list: {
      count: number;
      [index: number]: {
        name: string;
        value: string;
      };
    };
  }
}

export = libmpvjs;
