#ifndef ELECTRON_MPV_MPV_NODE_H
#define ELECTRON_MPV_MPV_NODE_H

#include <v8.h>
#include <memory>
#include <mpv/client.h>

class AutoMpvNode {
public:
  AutoMpvNode(v8::Isolate *i, const v8::Local<v8::Value> &value);
  ~AutoMpvNode();

  mpv_node *ptr() { return &node; }
  bool valid()const { return node.format != MPV_FORMAT_NONE; }

private:
  mpv_node node;
  AutoMpvNode(const AutoMpvNode &); // disable copying

  void init_node(v8::Isolate *i, mpv_node &node, const v8::Local<v8::Value> &value);
  void free_node(mpv_node &node);
};

v8::Local<v8::Value> mpv_node_to_v8_value(v8::Isolate *i, const mpv_node *node);

#endif //ELECTRON_MPV_MPV_NODE_H
