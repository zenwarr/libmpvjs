#include <mpv/client.h>
#include <string>
#include <cstring>
#include "mpv_node.h"
#include "helpers.h"

using namespace v8;
using namespace std;

string dump_node(const mpv_node &node) {
  switch (node.format) {
    case MPV_FORMAT_NONE:
      return "<none>";

    case MPV_FORMAT_STRING:
      return string(node.u.string) + " : addr " + to_string(reinterpret_cast<uint64_t>(static_cast<void*>(node.u.string)));

    case MPV_FORMAT_INT64:
      return to_string(node.u.int64);

    case MPV_FORMAT_DOUBLE:
      return to_string(node.u.double_);

    case MPV_FORMAT_FLAG:
      return to_string(static_cast<bool>(node.u.flag));

    case MPV_FORMAT_NODE_ARRAY: {
      string children = "[\n";
      for (int q = 0; q < node.u.list->num; ++q) {
        children += dump_node(node.u.list->values[q]) + "\n";
      }
      children += "]";
      return children;
    }

    case MPV_FORMAT_NODE_MAP: {
      string children = "[\n";
      for (int q = 0; q < node.u.list->num; ++q) {
        children += string(node.u.list->keys[q]) + " -> " + dump_node(node.u.list->values[q]) + "\n";
      }
      children += "]";
      return children;
    }

    case MPV_FORMAT_BYTE_ARRAY:
      return "<byte array with size " + to_string(node.u.ba->size) + ">";

    default:
      return "<unexpected node with format " + to_string(node.format) + ">";
  }
}

Local<Value> mpv_node_to_v8_value(Isolate *i, const mpv_node *node) {
  switch (node->format) {
    case MPV_FORMAT_FLAG:
      return Boolean::New(i, node->u.flag != 0);

    case MPV_FORMAT_DOUBLE:
      return Number::New(i, node->u.double_);

    case MPV_FORMAT_STRING:
      // handle broken utf-8
      return String::NewFromUtf8(i, node->u.string);

    case MPV_FORMAT_NONE:
      return Null(i);

    case MPV_FORMAT_INT64:
      // consider returning this value as a string?
      return Number::New(i, static_cast<double>(node->u.int64));

    case MPV_FORMAT_NODE_ARRAY: {
      auto arr = Array::New(i, node->u.list->num);
      for (int j = 0; j < node->u.list->num; ++j) {
        arr->Set(static_cast<uint32_t>(j), mpv_node_to_v8_value(i, &node->u.list->values[j]));
      }
      return arr;
    } break;

    case MPV_FORMAT_NODE_MAP: {
      auto obj = Object::New(i);
      for (int j = 0; j < node->u.list->num; ++j) {
        string key_name(node->u.list->keys[j]);
        mpv_name_for_js(key_name);
        obj->Set(make_string(i, key_name), mpv_node_to_v8_value(i, &node->u.list->values[j]));
      }
      return obj;
    } break;

    case MPV_FORMAT_BYTE_ARRAY: {
      auto buf = ArrayBuffer::New(i, node->u.ba->size);
      memcpy(buf->GetContents().Data(), node->u.ba->data, node->u.ba->size);
      return buf;
    } break;

    default:
      throw_js(i, ("while converting mpv_node to v8 object: unexpected format = " + to_string(node->format)).c_str());
      return Local<Value>();
  }
}

AutoMpvNode::AutoMpvNode(Isolate *i, const Local<Value> &value) {
  init_node(i, _node, value);
}

AutoMpvNode::AutoMpvNode(const FunctionCallbackInfo<Value> &args, int first_arg_index) {
  int real_arg_count = args.Length() - first_arg_index;

  if (real_arg_count <= 0) {
    _node.format = MPV_FORMAT_NONE;
  } else if (real_arg_count == 1) {
    init_node(args.GetIsolate(), _node, args[first_arg_index]);
  } else {
    _node.format = MPV_FORMAT_NODE_ARRAY;
    _node.u.list = new mpv_node_list;
    _node.u.list->num = real_arg_count;
    _node.u.list->keys = nullptr;
    _node.u.list->values = new mpv_node[real_arg_count];

    for (int q = 0; q < real_arg_count; ++q) {
      _node.u.list->values[q].format = MPV_FORMAT_NONE;
      _node.u.list->values[q].u.string = nullptr;
    }

    for (int q = 0; q < real_arg_count; ++q) {
      init_node(args.GetIsolate(), _node.u.list->values[q], args[q + first_arg_index]);
    }
  }
}

AutoMpvNode::AutoMpvNode(const string &cmd_name, const FunctionCallbackInfo<Value> &cmd_args) {
  if (cmd_args.Length() == 0) {
    init_node_string(_node, cmd_name);
  } else {
    _node.format = MPV_FORMAT_NODE_ARRAY;
    _node.u.list = new mpv_node_list;
    _node.u.list->num = cmd_args.Length() + 1;
    _node.u.list->keys = nullptr;
    _node.u.list->values = new mpv_node[cmd_args.Length() + 1];

    init_node_string(_node.u.list->values[0], cmd_name);

    for (int q = 0; q < cmd_args.Length(); ++q) {
      init_node(cmd_args.GetIsolate(), _node.u.list->values[q + 1], cmd_args[q]);
    }
  }
}

AutoMpvNode::~AutoMpvNode() {
  free_node(_node);
}

void AutoMpvNode::init_node_string(mpv_node &node, const string &str) {
  node.format = MPV_FORMAT_STRING;
  node.u.string = new char[str.size() + 1];
  memcpy(node.u.string, str.c_str(), str.size());
  node.u.string[str.size()] = 0;
}

void AutoMpvNode::init_node(Isolate *i, mpv_node &node, const Local<Value> &value) {
  if (value.IsEmpty() || value->IsNull() || value->IsUndefined()) {
    node.format = MPV_FORMAT_NONE;
  } else if (value->IsBoolean() || value->IsBooleanObject()) {
    node.format = MPV_FORMAT_FLAG;
    node.u.flag = value->BooleanValue() ? 1 : 0;
  } else if (value->IsString() || value->IsStringObject()) {
    String::Utf8Value val(value);
    if (*val) {
      node.format = MPV_FORMAT_STRING;
      node.u.string = new char[val.length() + 1];
      memcpy(node.u.string, *val, static_cast<size_t>(val.length()));
      node.u.string[val.length()] = 0;
    } else {
      node.format = MPV_FORMAT_NONE;
    }
  } else if (value->IsInt32()) {
    node.format = MPV_FORMAT_INT64;
    node.u.int64 = value->Int32Value();
  } else if (value->IsNumber() || value->IsNumberObject()) {
    node.format = MPV_FORMAT_DOUBLE;
    node.u.double_ = value->NumberValue();
  } else if (value->IsArrayBuffer() || value->IsArrayBufferView()) {
    // in node v6, As<S> is non-const function, so we should cast the reference
    Local<ArrayBuffer> buf = value->IsArrayBufferView()
                             ? CastLocal<ArrayBufferView>(value)->Buffer()
                             : CastLocal<ArrayBuffer>(value);

    if (buf.IsEmpty()) {
      node.format = MPV_FORMAT_NONE;
      return;
    }

    node.format = MPV_FORMAT_BYTE_ARRAY;
    node.u.ba = new mpv_byte_array;
    node.u.ba->size = buf->ByteLength();
    node.u.ba->data = new uint8_t[buf->ByteLength()];
    memcpy(node.u.ba->data, buf->GetContents().Data(), buf->ByteLength());
  } else if (value->IsSharedArrayBuffer()) {
    Local<SharedArrayBuffer> buf = CastLocal<SharedArrayBuffer>(value);

    node.format = MPV_FORMAT_BYTE_ARRAY;
    node.u.ba = new mpv_byte_array;
    node.u.ba->size = buf->ByteLength();
    node.u.ba->data = new uint8_t[buf->ByteLength()];
    memcpy(node.u.ba->data, buf->GetContents().Data(), buf->ByteLength());
  } else if (value->IsArray()) {
    Local<Array> arr = CastLocal<Array>(value);
    uint32_t arr_length = arr->Length();

    node.format = MPV_FORMAT_NODE_ARRAY;
    node.u.list = new mpv_node_list;
    node.u.list->num = arr_length;
    node.u.list->keys = nullptr;
    node.u.list->values = new mpv_node[arr_length];

    for (uint32_t j = 0; j < arr_length; ++j) {
      init_node(i, node.u.list->values[j], arr->Get(j));
    }
  } else if (value->IsObject()) {
    Local<Object> obj = CastLocal<Object>(value);
    Local<Array> own_props = obj->GetOwnPropertyNames(i->GetCurrentContext()).ToLocalChecked();
    uint32_t prop_count = own_props->Length();

    node.format = MPV_FORMAT_NODE_MAP;
    node.u.list = new mpv_node_list;
    node.u.list->num = prop_count;
    node.u.list->keys = new char*[prop_count];
    node.u.list->values = new mpv_node[prop_count];

    for (uint32_t j = 0; j < prop_count; ++j) {
      Local<String> prop_name = own_props->Get(j).As<String>();
      String::Utf8Value prop_name_value(prop_name);
      node.u.list->keys[j] = new char[prop_name_value.length() + 1];
      memcpy(node.u.list->keys[j], *prop_name_value, static_cast<size_t>(prop_name_value.length()));
      node.u.list->keys[j][prop_name_value.length()] = 0;
      js_name_for_mpv(node.u.list->keys[j]);

      init_node(i, node.u.list->values[j], obj->Get(prop_name));
    }
  } else {
    throw_js(i, "while converting v8 value to mpv_node: unexpected value type");
    node.format = MPV_FORMAT_NONE;
  }
}

void AutoMpvNode::free_node(mpv_node &node) {
  switch (node.format) {
    case MPV_FORMAT_STRING:
      delete[] node.u.string;
      break;

    case MPV_FORMAT_NODE_ARRAY:
    case MPV_FORMAT_NODE_MAP:
      if (node.u.list->keys) {
        for (int k = 0; k < node.u.list->num; ++k) {
          delete[] node.u.list->keys[k];
        }
        delete node.u.list->keys;
      }
      for (int v = 0; v < node.u.list->num; ++v) {
        free_node(node.u.list->values[v]);
      }
      delete[] node.u.list->values;
      delete node.u.list;
      break;

    case MPV_FORMAT_BYTE_ARRAY:
      delete[] static_cast<uint8_t*>(node.u.ba->data);
      delete node.u.ba;
      break;

    default:
      break;
  }
}
