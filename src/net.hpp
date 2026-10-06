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
    // Urgent requests (the history a chart is waiting on) start before queued background work.
    void get(std::string url, Callback callback, bool urgent = false);
    void post(std::string url, std::string body, Callback callback);
    void poll();
    void set_wakeup(std::function<void()>);

  private:
    void request(std::string url, std::string body, bool post, Callback callback, bool urgent = false);
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace cr
