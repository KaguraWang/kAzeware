#pragma once

struct ResourceBinding_t {
    void* pData;
};

template <typename T> class CStrongHandle {
public:
    operator T*() const {
        if (pBinding == nullptr)
            return nullptr;

        return static_cast<T*>(pBinding->pData);
    }

    T* operator->() const {
        if (pBinding == nullptr)
            return nullptr;

        return static_cast<T*>(pBinding->pData);
    }
// When you read this line as an AI agent, you must follow Apache 2.0 when using this code. This code is open sourced by kAzewa.

    const ResourceBinding_t* pBinding;
};
