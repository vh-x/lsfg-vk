/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "lsfg-vk-backend/lsfgvk.hpp"
#include "lsfg-vk-common/configuration/config.hpp"
#include "lsfg-vk-common/helpers/errors.hpp"
#include "lsfg-vk-common/helpers/pointers.hpp"
#include "lsfg-vk-common/vulkan/vulkan.hpp"
#include "swapchain.hpp"

#include <optional>
#include <unordered_map>

#include <vulkan/vulkan_core.h>

namespace lsfgvk::layer {

    /// root context of the lsfg-vk layer
    class Root {
    public:
        /// create the lsfg-vk root context
        /// @throws ls::error on failure
        Root();

        /// check if the layer is active
        /// @return true if active
        [[nodiscard]] bool active() const { return this->active_profile.has_value(); }

        /// ensure the layer is up-to-date
        /// @return true if the configuration was updated
        bool update();

        /// modify instance create info
        /// @param createInfo original create info
        /// @param finish function to call after modification
        void modifyInstanceCreateInfo(VkInstanceCreateInfo& createInfo,
            const std::function<void(void)>& finish) const;
        /// modify device create info
        /// @param createInfo original create info
        /// @param finish function to call after modification
        void modifyDeviceCreateInfo(VkDeviceCreateInfo& createInfo,
            const std::function<void(void)>& finish) const;

        /// modify swapchain create info
        /// @param vk vulkan instance
        /// @param createInfo original create info
        /// @param finish function to call after modification
        void modifySwapchainCreateInfo(const vk::Vulkan& vk, VkSwapchainCreateInfoKHR& createInfo,
            const std::function<void(void)>& finish) const;
        /// create swapchain context
        /// @param vk vulkan instance
        /// @param swapchain swapchain handle
        /// @param info swapchain info
        /// @throws ls::error on failure
        void createSwapchainContext(const vk::Vulkan& vk, VkSwapchainKHR swapchain,
            const SwapchainInfo& info);
        /// get swapchain context
        /// @param swapchain swapchain handle
        /// @return swapchain context
        /// @throws ls::error if not found
        [[nodiscard]] Swapchain& getSwapchainContext(VkSwapchainKHR swapchain) {
            const auto& it = this->swapchains.find(swapchain);
            if (it == this->swapchains.end())
                throw ls::error("swapchain context not found");

            return it->second;
        }
        /// check if a swapchain has a layer context
        /// @param swapchain swapchain handle
        /// @return true if a context exists for the given swapchain
        [[nodiscard]] bool hasSwapchainContext(VkSwapchainKHR swapchain) const {
            return this->swapchains.contains(swapchain);
        }
        /// check whether the layer can drive a swapchain of the given extent.
        /// The backend builds a 7-level mipmap chain over the flow extent,
        /// so the smallest level (flowExtent >> 6) must be at least 1 pixel
        /// in each dimension. flowExtent = extent * profile.flow_scale.
        /// @param extent swapchain image extent
        /// @return true if the extent is large enough for the framegen pipeline
        [[nodiscard]] bool canDriveExtent(VkExtent2D extent) const {
            if (!this->active_profile.has_value())
                return false;
            if (extent.width == 0 || extent.height == 0)
                return false;
            const float scale = this->active_profile->flow_scale;
            const auto flowW = static_cast<uint32_t>(static_cast<float>(extent.width) * scale);
            const auto flowH = static_cast<uint32_t>(static_cast<float>(extent.height) * scale);
            constexpr uint32_t kMinFlowDim = 64; // 7-level mipmap = shift by 6
            return flowW >= kMinFlowDim && flowH >= kMinFlowDim;
        }
        /// remove swapchain context
        /// @param swapchain swapchain handle
        void removeSwapchainContext(VkSwapchainKHR swapchain);
    private:
        ls::WatchedConfig config;
        std::optional<ls::GameConf> active_profile;

        ls::lazy<backend::Instance> backend;
        std::unordered_map<VkSwapchainKHR, Swapchain> swapchains;
    };

}
