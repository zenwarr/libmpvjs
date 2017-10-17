{
  "targets": [
    {
      "target_name": "mpv-webgl",
      "sources": [
        "main.cpp", "mpv_player.cpp", "helpers.cpp"
      ],
      "libraries": [
        "-L/home/victor/devel/mpv-build/mpv/build/",
        "-L/home/victor/devel/mpv-build/build_libs/lib/",
        "-l:libmpv.a",
        "-l:libavcodec.a",
        "-l:libavformat.a",
        "-l:libavutil.a",
        "-l:libavfilter.a",
        "-l:libavdevice.a",
        "-l:libswscale.a",
        "-l:libswresample.a",
        "-lpulse"
      ],
      "ldflags": [ "-Wl,-Bsymbolic" ],
      "cxxflags": [ "-fexceptions" ]
    }
  ]
}
