#include <ppapi/cpp/instance.h>

class MPVInstance : public pp::Instance {
public:
  explicit MPVInstance(PP_Instance instance): pp::Instance(instance) {

  }

  virtual ~MPVInstance() {

  }

  virtual bool Init(uint32_t argc, const char *argn[], const char *argv[]) {
    if (!InitGL()) {

    }
  }

  virtual void DidChangeView(const pp::View &view) {

  }

  virtual void HandleMessage(const pp::Var &msg) {
    pp::VarDictionary dict(msg);
    std::string msg_type = dict.Get("type").AsString();
    pp::Var msg_data = dict.Get("data");

    if (msg_type == "command") {

    } else if (msg_type == "set_property") {

    } else if (msg_type == "observe_property") {
      
    }
  }
}


