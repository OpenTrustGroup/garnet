/* SPDX-License-Identifier: GPL-2.0 */

#pragma once

#include <memory>
#include <mutex>
#include <stdexcept>

template <typename T>
class Singleton {
public:
    Singleton(const Singleton&) = delete;
    Singleton& operator=(const Singleton&) = delete;

    static T* Instance() {
        std::lock_guard<std::mutex> lock(GetMutex());
        if (!instance_) {
            instance_ = std::unique_ptr<T>(new T());
        }
        return instance_.get();
    }

    static void Destroy() {
        std::lock_guard<std::mutex> lock(GetMutex());
        instance_.reset();
    }

protected:
    Singleton() = default;
    ~Singleton() = default;

private:
    static std::unique_ptr<T> instance_;
    static std::mutex& GetMutex() {
        static std::mutex mtx;
        return mtx;
    }
};

template <typename T>
std::unique_ptr<T> Singleton<T>::instance_ = nullptr;
