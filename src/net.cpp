#include "net.hpp"
#include <deque>
#include <utility>
#ifdef __EMSCRIPTEN__
#include <cstring>
#include <emscripten/fetch.h>
#else
#include <atomic>
#include <condition_variable>
#include <curl/curl.h>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>
#endif
namespace cr {
#ifdef __EMSCRIPTEN__
struct Network::Impl {
    struct Done {
        Callback callback;
        std::string body, error;
    };
    std::deque<Done> done;
    std::function<void()> wakeup;
};
Network::Network() : impl(std::make_unique<Impl>()) {}
Network::~Network() = default;
void Network::poll() {
    auto finished = std::move(impl->done);
    impl->done.clear();
    for (auto &d : finished)
        d.callback(std::move(d.body), std::move(d.error));
}
void Network::set_wakeup(std::function<void()> wakeup) {
    impl->wakeup = std::move(wakeup);
}
void Network::get(std::string url, Callback callback) {
    // The accompanying web server forwards this fixed Yahoo route. No open proxy.
    const std::string origin = "https://query1.finance.yahoo.com";
    if (url.starts_with(origin))
        url = "/yahoo" + url.substr(origin.size());
    auto cb =
        new Callback([this, callback = std::move(callback)](std::string body, std::string error) mutable {
            impl->done.push_back({std::move(callback), std::move(body), std::move(error)});
            if (impl->wakeup)
                impl->wakeup();
        });
    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    std::strcpy(attr.requestMethod, "GET");
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    attr.timeoutMSecs = 25000;
    attr.userData = cb;
    attr.onsuccess = [](emscripten_fetch_t *f) {
        std::unique_ptr<Callback> cb(static_cast<Callback *>(f->userData));
        std::string body(f->data, f->numBytes);
        emscripten_fetch_close(f);
        (*cb)(std::move(body), "");
    };
    attr.onerror = [](emscripten_fetch_t *f) {
        std::unique_ptr<Callback> cb(static_cast<Callback *>(f->userData));
        std::string error = "Yahoo request failed (HTTP " + std::to_string(f->status) +
                            "). Run the supplied web server for data access.";
        emscripten_fetch_close(f);
        (*cb)("", std::move(error));
    };
    emscripten_fetch(&attr, url.c_str());
}
#else
struct Network::Impl {
    struct Job {
        std::string url;
        Callback callback;
    };
    struct Done {
        std::string body, error;
        Callback callback;
    };
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<Job> jobs;
    std::deque<Done> done;
    std::atomic<bool> stop = false;
    std::function<void()> wakeup;
    std::vector<std::thread> threads;
    static size_t write(char *p, size_t a, size_t b, void *data) {
        size_t n = a * b;
        auto &s = *static_cast<std::string *>(data);
        if (s.size() + n > 64 * 1024 * 1024)
            return 0;
        try {
            s.append(p, n);
        } catch (...) {
            return 0;
        }
        return n;
    }
    void worker() {
        for (;;) {
            Job job;
            {
                std::unique_lock lock(mutex);
                cv.wait(lock, [&] { return stop || !jobs.empty(); });
                if (stop)
                    return;
                job = std::move(jobs.front());
                jobs.pop_front();
            }
            Done d;
            d.callback = std::move(job.callback);
            CURL *curl = curl_easy_init();
            if (!curl) {
                d.error = "Could not initialize HTTP client";
            } else {
                curl_easy_setopt(curl, CURLOPT_URL, job.url.c_str());
                curl_easy_setopt(curl, CURLOPT_USERAGENT, "Chartroom/0.1");
                curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
                curl_easy_setopt(curl, CURLOPT_TIMEOUT, 25L);
                curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 8L);
                curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
                curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &write);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, &d.body);
                curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
                curl_easy_setopt(curl, CURLOPT_XFERINFODATA, this);
                curl_easy_setopt(
                    curl, CURLOPT_XFERINFOFUNCTION,
                    +[](void *p, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int {
                        return static_cast<Impl *>(p)->stop ? 1 : 0;
                    });
                auto code = curl_easy_perform(curl);
                long status = 0;
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
                if (code != CURLE_OK)
                    d.error = curl_easy_strerror(code);
                else if (status != 200 && status != 404 && status != 422)
                    d.error =
                        "Yahoo HTTP " + std::to_string(status) + (status == 429 ? " / rate limited" : "");
                curl_easy_cleanup(curl);
            }
            std::function<void()> notify;
            {
                std::lock_guard lock(mutex);
                done.push_back(std::move(d));
                notify = wakeup;
            }
            if (notify)
                notify();
        }
    }
};
Network::Network() : impl(std::make_unique<Impl>()) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    for (int i = 0; i < 2; ++i)
        impl->threads.emplace_back([this] { impl->worker(); });
}
Network::~Network() {
    impl->stop = true;
    impl->cv.notify_all();
    for (auto &t : impl->threads)
        t.join();
    curl_global_cleanup();
}
void Network::set_wakeup(std::function<void()> wakeup) {
    std::lock_guard lock(impl->mutex);
    impl->wakeup = std::move(wakeup);
}
void Network::get(std::string url, Callback callback) {
    {
        std::lock_guard lock(impl->mutex);
        impl->jobs.push_back({std::move(url), std::move(callback)});
    }
    impl->cv.notify_one();
}
void Network::poll() {
    std::deque<Impl::Done> finished;
    {
        std::lock_guard lock(impl->mutex);
        finished.swap(impl->done);
    }
    for (auto &d : finished)
        d.callback(std::move(d.body), std::move(d.error));
}
#endif
} // namespace cr
