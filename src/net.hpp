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
    void post(std::string url, std::string body, Callback callback);
    void poll();
    void set_wakeup(std::function<void()>);

  private:
    void request(std::string url, std::string body, bool post, Callback callback);
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace cr
