#include <mpv/client.h>
#include <string>
#include <cstring>
#include "mpv_node.h"
#include "helpers.h"

using namespace v8;
using namespace std;

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
      return Number::New(i, node->u.int64);

    case MPV_FORMAT_NODE_ARRAY: {
      auto arr = Array::New(i, node->u.list->num);
      for (uint32_t j = 0; j < node->u.list->num; ++j) {
        arr->Set(j, mpv_node_to_v8_value(i, &node->u.list->values[j]));
      }
      return arr;
    } break;

    case MPV_FORMAT_NODE_MAP: {
      auto obj = Object::New(i);
      for (uint32_t j = 0; j < node->u.list->num; ++j) {
        obj->Set(String::NewFromUtf8(i, node->u.list->keys[j]), mpv_node_to_v8_value(i, &node->u.list->values[j]));
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

AutoMpvNode::AutoMpvNode(Isolate *i, const v8::Local<v8::Value> &value) {
  init_node(i, node, value);
}

AutoMpvNode::~AutoMpvNode() {
  free_node(node);
}

void AutoMpvNode::init_node(Isolate *i, mpv_node &node, const v8::Local<v8::Value> &value) {
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
    Local<ArrayBuffer> buf = value->IsArrayBufferView()
                             ? value.As<ArrayBufferView>()->Buffer()
                             : value.As<ArrayBuffer>();

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
    Local<SharedArrayBuffer> buf = value.As<SharedArrayBuffer>();

    node.format = MPV_FORMAT_BYTE_ARRAY;
    node.u.ba = new mpv_byte_array;
    node.u.ba->size = buf->ByteLength();
    node.u.ba->data = new uint8_t[buf->ByteLength()];
    memcpy(node.u.ba->data, buf->GetContents().Data(), buf->ByteLength());
  } else if (value->IsArray()) {
    Local<Array> arr = value.As<Array>();
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
    Local<Object> obj = value.As<Object>();
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
      for (int j = 0; j < node.u.list->num; ++j) {
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
      }
      delete node.u.list;
      break;

    case MPV_FORMAT_BYTE_ARRAY:
      delete[] node.u.ba->data;
      delete node.u.ba;
      break;

    default:
      break;
  }
}
