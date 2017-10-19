#pragma once

#include <v8.h>
#include <node_object_wrap.h>
#include <string>
#include <map>

struct mpv_handle;
struct mpv_opengl_cb_context;
class MPImpl;
class PlayerOptions;

class MpvPlayer : public node::ObjectWrap {
public:
  static void Init(v8::Local<v8::Object> exports);

private:
  std::unique_ptr<MPImpl> d;
  
  MpvPlayer(v8::Isolate *isolate,
            const std::shared_ptr<v8::Persistent<v8::Object>> &canvas,
            const std::shared_ptr<v8::Persistent<v8::Object>> &renderingContext,
            const PlayerOptions &opts
  );
  MpvPlayer(const MpvPlayer &); // disable copying

  static void New(const v8::FunctionCallbackInfo<v8::Value> &args);
  static void Create(const v8::FunctionCallbackInfo<v8::Value> &args);
  static void Command(const v8::FunctionCallbackInfo<v8::Value> &args);
  static void SetProperty(const v8::FunctionCallbackInfo<v8::Value> &args);
  static void GetProperty(const v8::FunctionCallbackInfo<v8::Value> &args);
  static void ObserveProperty(const v8::FunctionCallbackInfo<v8::Value> &args);
  static void Dispose(const v8::FunctionCallbackInfo<v8::Value> &args);
  static v8::Persistent<v8::Function> constructor;
};
