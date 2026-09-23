#pragma once
#include <functional>
#include <memory>
#include <string>
namespace cr {
class Network {
  public:
    using Callback = std::function<void(std::string body, std::string error)>;
    Network();
    ~Network();
    void get(std::string url, Callback callback);
    void poll();
    void set_wakeup(std::function<void()>);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace cr
