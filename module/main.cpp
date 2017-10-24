#include <node.h>
#include "mpv_player.h"

using namespace v8;

void Init(Local<Object> exports) {
  MpvPlayer::Init(exports);
}

NODE_MODULE(libmpvjs, Init);
