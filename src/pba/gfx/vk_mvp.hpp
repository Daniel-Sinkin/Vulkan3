#pragma once

#include <memory>

namespace ds_pba {

class VulkanMvp final {
public:
    VulkanMvp();
    ~VulkanMvp();

    VulkanMvp(const VulkanMvp &) = delete;
    VulkanMvp &operator=(const VulkanMvp &) = delete;

    void run();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ds_pba
