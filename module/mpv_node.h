#ifndef ELECTRON_MPV_MPV_NODE_H
#define ELECTRON_MPV_MPV_NODE_H

#include <v8.h>
#include <memory>
#include <mpv/client.h>

class AutoMpvNode {
public:
  explicit AutoMpvNode(v8::Isolate *i, const v8::Local<v8::Value> &value);
  explicit AutoMpvNode(const v8::FunctionCallbackInfo<v8::Value> &args, int first_arg_index = 0);
  ~AutoMpvNode();

  mpv_node *ptr() { return &_node; }
  bool valid()const { return _node.format != MPV_FORMAT_NONE; }

private:
  mpv_node _node;
  AutoMpvNode(const AutoMpvNode &); // disable copying

  static void init_node(v8::Isolate *i, mpv_node &node, const v8::Local<v8::Value> &value);
  static void free_node(mpv_node &node);
};

struct AutoForeignMpvNode {
  AutoForeignMpvNode() {
    node.format = MPV_FORMAT_NONE;
  }

  ~AutoForeignMpvNode() {
    if (node.format != MPV_FORMAT_NONE) {
      mpv_free_node_contents(&node);
    }
  }

  mpv_node node;
};

v8::Local<v8::Value> mpv_node_to_v8_value(v8::Isolate *i, const mpv_node *node);
std::string dump_node(const mpv_node &node);

#endif //ELECTRON_MPV_MPV_NODE_H
