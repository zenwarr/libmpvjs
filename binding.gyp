{
  "targets": [
    {
      "target_name": "libmpvjs",
      "sources": [
        "module/main.cpp", "module/mpv_player.cpp", "module/helpers.cpp", "module/mpv_node.cpp"
      ],
      "ldflags": [ "-Wl,-Bsymbolic" ],
      "cxxflags": [ "-fexceptions" ],
      "conditions": [
        ["OS=='win'", {
          "include_dirs": [ "./deps" ],
          "libraries": [
            "-l../deps/win_libs/mpv-1"
          ]
        }, "OS=='linux'", {
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
            "-l:libass.a",
            "-lpulse",
            "-lfribidi"
          ]
        }]
      ]
    },
    {
      "target_name": "action_after_build",
      "type": "none",
      "dependencies": [ "libmpvjs" ],
      "copies": [
        {
          "files": [ "build/Release/libmpvjs.node" ],
          "destination": "module_dist"
        }
      ],
      "conditions": [
        ["OS=='win'", {
          "copies": [
            {
              "files": [ "deps/win_libs/mpv-1.dll" ],
              "destination": "module_dist"
            }
          ]
        }]
      ]
    }
  ]
}
