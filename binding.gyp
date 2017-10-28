{
  "targets": [
    {
      "target_name": "mpvjs",
      "sources": [
        "module/main.cpp", "module/mpv_player.cpp", "module/helpers.cpp", "module/mpv_node.cpp"
      ],
      "dependencies": [ "action_before_build" ],
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
            "-L../deps/mpv-build/mpv/build/",
            "-L../deps/mpv-build/build_libs/lib/",
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
      "dependencies": [ "mpvjs" ],
      "copies": [
        {
          "files": [ "build/Release/mpvjs.node" ],
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
    },
    {
      "target_name": "action_before_build",
      "type": "none",
      "actions": [
        {
          "action_name": "resolve_deps",
          "inputs": [ ],
          "outputs": [ "./deps" ],
          "action": [
            "node",
            "./resolve-deps.js"
          ]
        }
      ]
    }
  ]
}
