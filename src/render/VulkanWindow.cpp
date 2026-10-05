#include "VulkanWindow.h"

#include "Document.h"

#include <QCoreApplication>
#include <QFile>
#include <QKeyEvent>
#include <QLineF>
#include <QLoggingCategory>
#include <QMouseEvent>
#include <QMutexLocker>
#include <QSize>
#include <QVulkanDeviceFunctions>
#include <QVulkanFunctions>
#include <QVulkanInstance>

#include <vulkan/vulkan.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>

namespace {

constexpr quint32 kSolidMode = 0;
constexpr quint32 kCanvasMode = 1;
constexpr quint32 kBrushMode = 2;
constexpr quint32 kSelectionMode = 3;
constexpr quint32 kImageMode = 4;
constexpr quint32 kPremultipliedImageMode = 5;
constexpr quint32 kPremultipliedBrushMode = 6;
constexpr quint32 kNormalBlendPipeline = 0;
constexpr quint32 kMultiplyBlendPipeline = 1;
constexpr quint32 kScreenBlendPipeline = 2;
constexpr quint32 kBlendPipelineCount = 3;

struct alignas(16) DrawParameters {
    float center[2]{};
    float halfExtent[2]{};
    float color[4]{};
    std::uint32_t mode = 0;
    std::uint32_t padding[3]{};
};
static_assert(sizeof(DrawParameters) == 48);

bool createShader(QVulkanDeviceFunctions* functions, VkDevice device, const QString& path,
                  VkShaderModule* shader, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("Could not read Vulkan shader %1: %2")
                     .arg(path, file.errorString());
        return false;
    }
    const QByteArray bytes = file.readAll();
    if (bytes.isEmpty() || bytes.size() % static_cast<qsizetype>(sizeof(std::uint32_t)) != 0) {
        *error = QStringLiteral("Vulkan shader %1 has an invalid SPIR-V size.").arg(path);
        return false;
    }
    QVector<std::uint32_t> words(bytes.size() / static_cast<qsizetype>(sizeof(std::uint32_t)));
    std::memcpy(words.data(), bytes.constData(), static_cast<size_t>(bytes.size()));

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = static_cast<size_t>(bytes.size());
    createInfo.pCode = words.constData();
    const VkResult result = functions->vkCreateShaderModule(
        device, &createInfo, nullptr, shader);
    if (result != VK_SUCCESS) {
        *error = QStringLiteral("Could not create Vulkan shader module (%1).")
                     .arg(static_cast<int>(result));
        return false;
    }
    return true;
}

QPointF toNdc(const QPointF& point, const QSize& windowSize)
{
    return QPointF(
        (2.0 * point.x() / qMax(1, windowSize.width())) - 1.0,
        (2.0 * point.y() / qMax(1, windowSize.height())) - 1.0);
}

class CanvasRenderer final : public QVulkanWindowRenderer
{
public:
    explicit CanvasRenderer(VulkanWindow* window)
        : m_window(window)
    {
    }

    void initResources() override
    {
        m_functions = m_window->vulkanInstance()->deviceFunctions(m_window->device());
        if (m_functions == nullptr) {
            m_window->reportRenderError(QStringLiteral("Qt could not load Vulkan device functions."));
            return;
        }

        const QString shaderDirectory = QCoreApplication::applicationDirPath()
            + QStringLiteral("/") + QStringLiteral(VELOX_SHADER_DIR);
        QString error;
        if (!createShader(m_functions, m_window->device(),
                          shaderDirectory + QStringLiteral("/canvas.vert.spv"),
                          &m_vertexShader, &error)
            || !createShader(m_functions, m_window->device(),
                             shaderDirectory + QStringLiteral("/canvas.frag.spv"),
                             &m_fragmentShader, &error)) {
            m_window->reportRenderError(error);
            releaseResources();
            return;
        }

        VkDescriptorSetLayoutBinding imageBinding{};
        imageBinding.binding = 0;
        imageBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        imageBinding.descriptorCount = 1;
        imageBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo descriptorLayoutInfo{};
        descriptorLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        descriptorLayoutInfo.bindingCount = 1;
        descriptorLayoutInfo.pBindings = &imageBinding;
        VkResult result = m_functions->vkCreateDescriptorSetLayout(
            m_window->device(), &descriptorLayoutInfo, nullptr, &m_descriptorSetLayout);
        if (result != VK_SUCCESS) {
            m_window->reportRenderError(
                QStringLiteral("Could not create Vulkan image descriptor layout (%1).")
                    .arg(static_cast<int>(result)));
            releaseResources();
            return;
        }

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(DrawParameters);
        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &m_descriptorSetLayout;
        result = m_functions->vkCreatePipelineLayout(
            m_window->device(), &layoutInfo, nullptr, &m_pipelineLayout);
        if (result != VK_SUCCESS) {
            m_window->reportRenderError(
                QStringLiteral("Could not create Vulkan canvas pipeline layout (%1).")
                    .arg(static_cast<int>(result)));
            releaseResources();
            return;
        }

        const auto* properties = m_window->physicalDeviceProperties();
        if (properties != nullptr) {
            const QString name = QString::fromUtf8(properties->deviceName);
            qInfo().noquote() << "Vulkan device initialized:" << name;
            m_window->reportDeviceInitialized(name);
        }
    }

    void initSwapChainResources() override
    {
        if (m_pipelineLayout == VK_NULL_HANDLE || m_vertexShader == VK_NULL_HANDLE
            || m_fragmentShader == VK_NULL_HANDLE) {
            return;
        }

        VkPipelineShaderStageCreateInfo shaderStages[2]{};
        shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        shaderStages[0].module = m_vertexShader;
        shaderStages[0].pName = "main";
        shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        shaderStages[1].module = m_fragmentShader;
        shaderStages[1].pName = "main";

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewport{};
        viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport.viewportCount = 1;
        viewport.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterization{};
        rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterization.polygonMode = VK_POLYGON_MODE_FILL;
        rasterization.cullMode = VK_CULL_MODE_NONE;
        rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterization.lineWidth = 1.0F;

        VkPipelineMultisampleStateCreateInfo multisample{};
        multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        const VkDynamicState dynamicStates[]{
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = 2;
        dynamicState.pDynamicStates = dynamicStates;

        VkResult result = VK_SUCCESS;
        for (quint32 blendPipeline = 0; blendPipeline < kBlendPipelineCount; ++blendPipeline) {
            VkPipelineColorBlendAttachmentState blendAttachment{};
            blendAttachment.blendEnable = VK_TRUE;
            blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
            blendAttachment.colorWriteMask =
                VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            if (blendPipeline == kNormalBlendPipeline) {
                blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
                blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            } else if (blendPipeline == kMultiplyBlendPipeline) {
                blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
                blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            } else {
                blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
                blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
            }
            blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            VkPipelineColorBlendStateCreateInfo colorBlend{};
            colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            colorBlend.attachmentCount = 1;
            colorBlend.pAttachments = &blendAttachment;

            VkGraphicsPipelineCreateInfo pipelineInfo{};
            pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            pipelineInfo.stageCount = 2;
            pipelineInfo.pStages = shaderStages;
            pipelineInfo.pVertexInputState = &vertexInput;
            pipelineInfo.pInputAssemblyState = &inputAssembly;
            pipelineInfo.pViewportState = &viewport;
            pipelineInfo.pRasterizationState = &rasterization;
            pipelineInfo.pMultisampleState = &multisample;
            pipelineInfo.pColorBlendState = &colorBlend;
            pipelineInfo.pDynamicState = &dynamicState;
            pipelineInfo.layout = m_pipelineLayout;
            pipelineInfo.renderPass = m_window->defaultRenderPass();
            pipelineInfo.subpass = 0;

            result = m_functions->vkCreateGraphicsPipelines(
                m_window->device(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                &m_pipelines[blendPipeline]);
            if (result != VK_SUCCESS) {
                releaseSwapChainResources();
                m_window->reportRenderError(
                    QStringLiteral("Could not create Vulkan canvas pipeline (%1).")
                        .arg(static_cast<int>(result)));
                return;
            }
        }
        qInfo() << "Vulkan canvas graphics pipelines initialized.";
    }

    void releaseSwapChainResources() override
    {
        if (m_functions != nullptr) {
            for (VkPipeline& pipeline : m_pipelines) {
                if (pipeline != VK_NULL_HANDLE) {
                    m_functions->vkDestroyPipeline(m_window->device(), pipeline, nullptr);
                    pipeline = VK_NULL_HANDLE;
                }
            }
        }
    }

    void releaseResources() override
    {
        if (m_functions == nullptr) {
            return;
        }
        if (m_vertexShader != VK_NULL_HANDLE) {
            m_functions->vkDestroyShaderModule(m_window->device(), m_vertexShader, nullptr);
            m_vertexShader = VK_NULL_HANDLE;
        }
        if (m_fragmentShader != VK_NULL_HANDLE) {
            m_functions->vkDestroyShaderModule(m_window->device(), m_fragmentShader, nullptr);
            m_fragmentShader = VK_NULL_HANDLE;
        }
        if (m_pipelineLayout != VK_NULL_HANDLE) {
            m_functions->vkDestroyPipelineLayout(m_window->device(), m_pipelineLayout, nullptr);
            m_pipelineLayout = VK_NULL_HANDLE;
        }
        destroyLayerTextures();
        if (m_sampler != VK_NULL_HANDLE) {
            m_functions->vkDestroySampler(m_window->device(), m_sampler, nullptr);
            m_sampler = VK_NULL_HANDLE;
        }
        if (m_descriptorSetLayout != VK_NULL_HANDLE) {
            m_functions->vkDestroyDescriptorSetLayout(
                m_window->device(), m_descriptorSetLayout, nullptr);
            m_descriptorSetLayout = VK_NULL_HANDLE;
        }
    }

    void startNextFrame() override
    {
        const QSize imageSize = m_window->swapChainImageSize();
        const std::shared_ptr<const CanvasRenderData> data = m_window->renderData();
        const bool texturesReady = data != nullptr && updateLayerTextures(*data);
        VkClearValue clearValue{};
        clearValue.color = {{0.11F, 0.12F, 0.15F, 1.0F}};

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = m_window->defaultRenderPass();
        renderPassInfo.framebuffer = m_window->currentFramebuffer();
        renderPassInfo.renderArea.extent = {
            static_cast<std::uint32_t>(imageSize.width()),
            static_cast<std::uint32_t>(imageSize.height())
        };
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearValue;

        if (m_functions != nullptr) {
            const VkCommandBuffer commandBuffer = m_window->currentCommandBuffer();
            m_functions->vkCmdBeginRenderPass(
                commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
            if (texturesReady && pipelinesReady()
                && imageSize.width() > 0 && imageSize.height() > 0) {
                VkViewport viewport{};
                viewport.width = static_cast<float>(imageSize.width());
                viewport.height = static_cast<float>(imageSize.height());
                viewport.minDepth = 0.0F;
                viewport.maxDepth = 1.0F;
                VkRect2D scissor{};
                scissor.extent = renderPassInfo.renderArea.extent;
                m_functions->vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
                m_functions->vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
                m_functions->vkCmdBindPipeline(
                    commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    m_pipelines[kNormalBlendPipeline]);

                drawQuad(commandBuffer, QPointF(0.0, 0.0), QSizeF(1.0, 1.0),
                         QColor(QStringLiteral("#202127")), kSolidMode);
                drawCanvas(commandBuffer, *data);
            }
            m_functions->vkCmdEndRenderPass(commandBuffer);
        }
        m_window->frameReady();
    }

private:
    struct LayerTexture {
        int layerId = 0;
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkDescriptorSet descriptor = VK_NULL_HANDLE;
    };

    bool findMemoryType(std::uint32_t bits, VkMemoryPropertyFlags properties,
                        std::uint32_t* index)
    {
        VkPhysicalDeviceMemoryProperties memoryProperties{};
        m_window->vulkanInstance()->functions()->vkGetPhysicalDeviceMemoryProperties(
            m_window->physicalDevice(), &memoryProperties);
        for (std::uint32_t candidate = 0; candidate < memoryProperties.memoryTypeCount;
             ++candidate) {
            if ((bits & (1U << candidate)) != 0
                && (memoryProperties.memoryTypes[candidate].propertyFlags & properties)
                    == properties) {
                *index = candidate;
                return true;
            }
        }
        return false;
    }

    void destroyTexture(LayerTexture& texture)
    {
        if (texture.view != VK_NULL_HANDLE) {
            m_functions->vkDestroyImageView(m_window->device(), texture.view, nullptr);
            texture.view = VK_NULL_HANDLE;
        }
        if (texture.image != VK_NULL_HANDLE) {
            m_functions->vkDestroyImage(m_window->device(), texture.image, nullptr);
            texture.image = VK_NULL_HANDLE;
        }
        if (texture.memory != VK_NULL_HANDLE) {
            m_functions->vkFreeMemory(m_window->device(), texture.memory, nullptr);
            texture.memory = VK_NULL_HANDLE;
        }
        texture.descriptor = VK_NULL_HANDLE;
    }

    void destroyLayerTextures()
    {
        if (m_functions == nullptr) {
            return;
        }
        if (m_descriptorPool != VK_NULL_HANDLE) {
            m_functions->vkDestroyDescriptorPool(
                m_window->device(), m_descriptorPool, nullptr);
            m_descriptorPool = VK_NULL_HANDLE;
        }
        for (LayerTexture& texture : m_layerTextures) {
            destroyTexture(texture);
        }
        m_layerTextures.clear();
        m_defaultDescriptor = VK_NULL_HANDLE;
        m_textureGeneration = std::numeric_limits<quint64>::max();
        m_failedTextureGeneration = std::numeric_limits<quint64>::max();
    }

    bool uploadImage(const QImage& source, LayerTexture* texture, QString* error)
    {
        const QImage image = source.convertToFormat(QImage::Format_RGBA8888);
        if (image.isNull()) {
            *error = QStringLiteral("Could not convert a layer image to RGBA pixels.");
            return false;
        }
        const VkDevice device = m_window->device();
        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        const auto cleanup = [&] {
            if (fence != VK_NULL_HANDLE) {
                m_functions->vkDestroyFence(device, fence, nullptr);
            }
            if (commandBuffer != VK_NULL_HANDLE) {
                m_functions->vkFreeCommandBuffers(
                    device, m_window->graphicsCommandPool(), 1, &commandBuffer);
            }
            if (stagingBuffer != VK_NULL_HANDLE) {
                m_functions->vkDestroyBuffer(device, stagingBuffer, nullptr);
            }
            if (stagingMemory != VK_NULL_HANDLE) {
                m_functions->vkFreeMemory(device, stagingMemory, nullptr);
            }
        };

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = static_cast<VkDeviceSize>(image.sizeInBytes());
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkResult result = m_functions->vkCreateBuffer(
            device, &bufferInfo, nullptr, &stagingBuffer);
        if (result != VK_SUCCESS) {
            *error = QStringLiteral("Could not create Vulkan staging buffer (%1).")
                         .arg(static_cast<int>(result));
            cleanup();
            return false;
        }

        VkMemoryRequirements bufferRequirements{};
        m_functions->vkGetBufferMemoryRequirements(device, stagingBuffer, &bufferRequirements);
        std::uint32_t memoryType = 0;
        if (!findMemoryType(bufferRequirements.memoryTypeBits,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                                | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                            &memoryType)) {
            *error = QStringLiteral("No host-coherent Vulkan memory is available for image upload.");
            cleanup();
            return false;
        }
        VkMemoryAllocateInfo allocation{};
        allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocation.allocationSize = bufferRequirements.size;
        allocation.memoryTypeIndex = memoryType;
        result = m_functions->vkAllocateMemory(device, &allocation, nullptr, &stagingMemory);
        if (result == VK_SUCCESS) {
            result = m_functions->vkBindBufferMemory(device, stagingBuffer, stagingMemory, 0);
        }
        if (result != VK_SUCCESS) {
            *error = QStringLiteral("Could not allocate Vulkan staging memory (%1).")
                         .arg(static_cast<int>(result));
            cleanup();
            return false;
        }
        void* mapped = nullptr;
        result = m_functions->vkMapMemory(
            device, stagingMemory, 0, bufferInfo.size, 0, &mapped);
        if (result != VK_SUCCESS || mapped == nullptr) {
            *error = QStringLiteral("Could not map Vulkan staging memory (%1).")
                         .arg(static_cast<int>(result));
            cleanup();
            return false;
        }
        std::memcpy(mapped, image.constBits(), static_cast<size_t>(image.sizeInBytes()));
        m_functions->vkUnmapMemory(device, stagingMemory);

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        imageInfo.extent = {static_cast<std::uint32_t>(image.width()),
                            static_cast<std::uint32_t>(image.height()), 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        result = m_functions->vkCreateImage(device, &imageInfo, nullptr, &texture->image);
        if (result != VK_SUCCESS) {
            *error = QStringLiteral("Could not create Vulkan layer image (%1).")
                         .arg(static_cast<int>(result));
            cleanup();
            return false;
        }
        VkMemoryRequirements imageRequirements{};
        m_functions->vkGetImageMemoryRequirements(device, texture->image, &imageRequirements);
        if (!findMemoryType(imageRequirements.memoryTypeBits,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &memoryType)) {
            *error = QStringLiteral("No device-local Vulkan memory is available for layer images.");
            cleanup();
            destroyTexture(*texture);
            return false;
        }
        allocation.allocationSize = imageRequirements.size;
        allocation.memoryTypeIndex = memoryType;
        result = m_functions->vkAllocateMemory(device, &allocation, nullptr, &texture->memory);
        if (result == VK_SUCCESS) {
            result = m_functions->vkBindImageMemory(device, texture->image, texture->memory, 0);
        }
        if (result != VK_SUCCESS) {
            *error = QStringLiteral("Could not allocate Vulkan layer-image memory (%1).")
                         .arg(static_cast<int>(result));
            cleanup();
            destroyTexture(*texture);
            return false;
        }

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = texture->image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        result = m_functions->vkCreateImageView(
            device, &viewInfo, nullptr, &texture->view);
        if (result != VK_SUCCESS) {
            *error = QStringLiteral("Could not create Vulkan layer-image view (%1).")
                         .arg(static_cast<int>(result));
            cleanup();
            destroyTexture(*texture);
            return false;
        }

        VkCommandBufferAllocateInfo commandAllocation{};
        commandAllocation.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        commandAllocation.commandPool = m_window->graphicsCommandPool();
        commandAllocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandAllocation.commandBufferCount = 1;
        result = m_functions->vkAllocateCommandBuffers(
            device, &commandAllocation, &commandBuffer);
        if (result != VK_SUCCESS) {
            *error = QStringLiteral("Could not allocate Vulkan image-upload commands (%1).")
                         .arg(static_cast<int>(result));
            cleanup();
            destroyTexture(*texture);
            return false;
        }
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        result = m_functions->vkBeginCommandBuffer(commandBuffer, &beginInfo);
        if (result != VK_SUCCESS) {
            *error = QStringLiteral("Could not begin Vulkan image-upload commands (%1).")
                         .arg(static_cast<int>(result));
            cleanup();
            destroyTexture(*texture);
            return false;
        }
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = texture->image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        m_functions->vkCmdPipelineBarrier(
            commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = imageInfo.extent;
        m_functions->vkCmdCopyBufferToImage(
            commandBuffer, stagingBuffer, texture->image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        m_functions->vkCmdPipelineBarrier(
            commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        result = m_functions->vkEndCommandBuffer(commandBuffer);
        if (result != VK_SUCCESS) {
            *error = QStringLiteral("Could not finish Vulkan image-upload commands (%1).")
                         .arg(static_cast<int>(result));
            cleanup();
            destroyTexture(*texture);
            return false;
        }
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        result = m_functions->vkCreateFence(device, &fenceInfo, nullptr, &fence);
        if (result == VK_SUCCESS) {
            VkSubmitInfo submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &commandBuffer;
            result = m_functions->vkQueueSubmit(m_window->graphicsQueue(), 1, &submitInfo, fence);
        }
        if (result == VK_SUCCESS) {
            result = m_functions->vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
        }
        if (result != VK_SUCCESS) {
            *error = QStringLiteral("Vulkan layer image upload failed (%1).")
                         .arg(static_cast<int>(result));
            m_functions->vkQueueWaitIdle(m_window->graphicsQueue());
            cleanup();
            destroyTexture(*texture);
            return false;
        }
        cleanup();
        return true;
    }

    bool updateLayerTextures(const CanvasRenderData& data)
    {
        if (data.imageGeneration == m_textureGeneration) {
            return true;
        }
        if (data.imageGeneration == m_failedTextureGeneration) {
            return false;
        }
        const auto fail = [this, &data](const QString& message) {
            m_failedTextureGeneration = data.imageGeneration;
            m_window->reportRenderError(message);
            return false;
        };
        const VkResult idleResult = m_functions->vkQueueWaitIdle(m_window->graphicsQueue());
        if (idleResult != VK_SUCCESS) {
            return fail(QStringLiteral(
                "Could not synchronize Vulkan before updating layers (%1).")
                            .arg(static_cast<int>(idleResult)));
        }
        destroyLayerTextures();
        int imageCount = 0;
        for (const CanvasRenderLayer& layer : data.layers) {
            imageCount += layer.image.isNull() ? 0 : 1;
        }
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = static_cast<std::uint32_t>(imageCount + 1);
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = static_cast<std::uint32_t>(imageCount + 1);
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        VkResult result = m_functions->vkCreateDescriptorPool(
            m_window->device(), &poolInfo, nullptr, &m_descriptorPool);
        if (result != VK_SUCCESS) {
            return fail(QStringLiteral("Could not create Vulkan image descriptor pool (%1).")
                            .arg(static_cast<int>(result)));
        }
        if (m_sampler == VK_NULL_HANDLE) {
            VkSamplerCreateInfo samplerInfo{};
            samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerInfo.magFilter = VK_FILTER_LINEAR;
            samplerInfo.minFilter = VK_FILTER_LINEAR;
            samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.maxLod = 0.0F;
            result = m_functions->vkCreateSampler(
                m_window->device(), &samplerInfo, nullptr, &m_sampler);
            if (result != VK_SUCCESS) {
                destroyLayerTextures();
                return fail(QStringLiteral("Could not create Vulkan layer-image sampler (%1).")
                                .arg(static_cast<int>(result)));
            }
        }

        QImage fallbackImage(1, 1, QImage::Format_RGBA8888);
        fallbackImage.fill(Qt::white);
        LayerTexture fallback;
        fallback.layerId = 0;
        QString error;
        if (!uploadImage(fallbackImage, &fallback, &error)
            || !allocateTextureDescriptor(&fallback, &error)) {
            destroyTexture(fallback);
            destroyLayerTextures();
            return fail(error);
        }
        m_defaultDescriptor = fallback.descriptor;
        m_layerTextures.append(fallback);

        for (const CanvasRenderLayer& layer : data.layers) {
            if (layer.image.isNull()) {
                continue;
            }
            LayerTexture texture;
            texture.layerId = layer.id;
            if (!uploadImage(layer.image, &texture, &error)
                || !allocateTextureDescriptor(&texture, &error)) {
                destroyTexture(texture);
                destroyLayerTextures();
                return fail(error);
            }
            m_layerTextures.append(texture);
        }
        m_textureGeneration = data.imageGeneration;
        m_failedTextureGeneration = std::numeric_limits<quint64>::max();
        return true;
    }

    bool allocateTextureDescriptor(LayerTexture* texture, QString* error)
    {
        VkDescriptorSetAllocateInfo setInfo{};
        setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        setInfo.descriptorPool = m_descriptorPool;
        setInfo.descriptorSetCount = 1;
        setInfo.pSetLayouts = &m_descriptorSetLayout;
        const VkResult result = m_functions->vkAllocateDescriptorSets(
            m_window->device(), &setInfo, &texture->descriptor);
        if (result != VK_SUCCESS) {
            *error = QStringLiteral("Could not allocate a Vulkan layer descriptor (%1).")
                         .arg(static_cast<int>(result));
            return false;
        }
        VkDescriptorImageInfo descriptorImage{};
        descriptorImage.sampler = m_sampler;
        descriptorImage.imageView = texture->view;
        descriptorImage.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = texture->descriptor;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &descriptorImage;
        m_functions->vkUpdateDescriptorSets(m_window->device(), 1, &write, 0, nullptr);
        return true;
    }

    const LayerTexture* textureForLayer(int layerId) const
    {
        for (const LayerTexture& texture : m_layerTextures) {
            if (texture.layerId == layerId) {
                return &texture;
            }
        }
        return nullptr;
    }

    quint32 pipelineForBlendMode(velox::BlendMode blendMode) const
    {
        switch (blendMode) {
        case velox::BlendMode::Multiply:
            return kMultiplyBlendPipeline;
        case velox::BlendMode::Screen:
            return kScreenBlendPipeline;
        default:
            return kNormalBlendPipeline;
        }
    }

    bool pipelinesReady() const
    {
        for (const VkPipeline pipeline : m_pipelines) {
            if (pipeline == VK_NULL_HANDLE) {
                return false;
            }
        }
        return true;
    }

    void drawLayerImage(VkCommandBuffer commandBuffer, const QRectF& canvas,
                        const QSize& windowSize, qreal scale,
                        const CanvasRenderLayer& layer)
    {
        const LayerTexture* texture = textureForLayer(layer.id);
        if (texture == nullptr) {
            return;
        }
        const QPointF center(
            canvas.left() + (layer.offset.x() + layer.image.width() * 0.5) * scale,
            canvas.top() + (layer.offset.y() + layer.image.height() * 0.5) * scale);
        const QSizeF extent(
            layer.image.width() * scale / qMax(1, windowSize.width()),
            layer.image.height() * scale / qMax(1, windowSize.height()));
        DrawParameters parameters{};
        const QPointF centerNdc = toNdc(center, windowSize);
        parameters.center[0] = static_cast<float>(centerNdc.x());
        parameters.center[1] = static_cast<float>(centerNdc.y());
        parameters.halfExtent[0] = static_cast<float>(extent.width());
        parameters.halfExtent[1] = static_cast<float>(extent.height());
        parameters.color[3] = static_cast<float>(layer.opacity);
        const quint32 blendPipeline = pipelineForBlendMode(layer.blendMode);
        parameters.mode = blendPipeline == kNormalBlendPipeline
            ? kImageMode : kPremultipliedImageMode;
        m_functions->vkCmdBindPipeline(
            commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_pipelines[blendPipeline]);
        m_functions->vkCmdBindDescriptorSets(
            commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
            0, 1, &texture->descriptor, 0, nullptr);
        m_functions->vkCmdPushConstants(
            commandBuffer, m_pipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(parameters), &parameters);
        m_functions->vkCmdDraw(commandBuffer, 6, 1, 0, 0);
    }

    void drawQuad(VkCommandBuffer commandBuffer, const QPointF& center,
                  const QSizeF& halfExtent, const QColor& color, quint32 mode)
    {
        if (m_defaultDescriptor != VK_NULL_HANDLE) {
            m_functions->vkCmdBindDescriptorSets(
                commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
                0, 1, &m_defaultDescriptor, 0, nullptr);
        }
        DrawParameters parameters{};
        const QPointF centerNdc = toNdc(center, m_window->size());
        parameters.center[0] = static_cast<float>(centerNdc.x());
        parameters.center[1] = static_cast<float>(centerNdc.y());
        parameters.halfExtent[0] = static_cast<float>(halfExtent.width());
        parameters.halfExtent[1] = static_cast<float>(halfExtent.height());
        parameters.color[0] = color.redF();
        parameters.color[1] = color.greenF();
        parameters.color[2] = color.blueF();
        parameters.color[3] = color.alphaF();
        parameters.mode = mode;
        m_functions->vkCmdPushConstants(
            commandBuffer, m_pipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(parameters), &parameters);
        m_functions->vkCmdDraw(commandBuffer, 6, 1, 0, 0);
    }

    void drawCanvas(VkCommandBuffer commandBuffer, const CanvasRenderData& data)
    {
        const QRectF canvas = canvasRect(data.canvasSize);
        const QSize windowSize = m_window->size();
        if (canvas.isEmpty() || windowSize.isEmpty()) {
            return;
        }

        const QPointF center = canvas.center();
        const QSizeF halfExtent(canvas.width() / windowSize.width(),
                                canvas.height() / windowSize.height());
        drawQuad(commandBuffer, center, halfExtent, Qt::white, kCanvasMode);

        const QSize framebufferSize = m_window->swapChainImageSize();
        const qreal pixelRatio = m_window->devicePixelRatio();
        const int left = qBound(0, static_cast<int>(std::floor(canvas.left() * pixelRatio)),
                                framebufferSize.width());
        const int top = qBound(0, static_cast<int>(std::floor(canvas.top() * pixelRatio)),
                               framebufferSize.height());
        const int right = qBound(left, static_cast<int>(std::ceil(canvas.right() * pixelRatio)),
                                 framebufferSize.width());
        const int bottom = qBound(top, static_cast<int>(std::ceil(canvas.bottom() * pixelRatio)),
                                  framebufferSize.height());
        VkRect2D canvasScissor{};
        canvasScissor.offset = {left, top};
        canvasScissor.extent = {
            static_cast<std::uint32_t>(right - left),
            static_cast<std::uint32_t>(bottom - top)
        };
        m_functions->vkCmdSetScissor(commandBuffer, 0, 1, &canvasScissor);

        const qreal scale = canvas.width() / data.canvasSize.width();
        for (const CanvasRenderLayer& layer : data.layers) {
            if (!layer.visible) {
                continue;
            }
            if (!layer.image.isNull()) {
                drawLayerImage(commandBuffer, canvas, windowSize, scale, layer);
            }
            m_functions->vkCmdBindPipeline(
                commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                m_pipelines[pipelineForBlendMode(layer.blendMode)]);
            for (const velox::BrushStroke& stroke : layer.strokes) {
                drawStroke(commandBuffer, canvas, data.canvasSize, layer, stroke, scale);
            }
            m_functions->vkCmdBindPipeline(
                commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                m_pipelines[kNormalBlendPipeline]);
        }
        if (data.activeStroke) {
            m_functions->vkCmdBindPipeline(
                commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                m_pipelines[kNormalBlendPipeline]);
            CanvasRenderLayer activeLayer;
            activeLayer.opacity = 1.0;
            activeLayer.visible = true;
            drawStroke(commandBuffer, canvas, data.canvasSize, activeLayer,
                       *data.activeStroke, scale);
        }
        if (data.hasSelection && !data.selection.isEmpty()) {
            m_functions->vkCmdBindPipeline(
                commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                m_pipelines[kNormalBlendPipeline]);
            const QRectF selection(canvas.left() + data.selection.left() * scale,
                                   canvas.top() + data.selection.top() * scale,
                                   data.selection.width() * scale,
                                   data.selection.height() * scale);
            const QPointF selectionCenter = selection.center();
            const QSizeF selectionExtent(selection.width() / windowSize.width(),
                                         selection.height() / windowSize.height());
            drawQuad(commandBuffer, selectionCenter, selectionExtent,
                     QColor(71, 127, 229, 235), kSelectionMode);
        }
    }

    void drawStroke(VkCommandBuffer commandBuffer, const QRectF& canvas, const QSize& canvasSize,
                    const CanvasRenderLayer& layer, const velox::BrushStroke& stroke, qreal scale)
    {
        const QSize windowSize = m_window->size();
        const qreal radius = stroke.diameter * scale * 0.5;
        const QSizeF extent((2.0 * radius) / qMax(1, windowSize.width()),
                            (2.0 * radius) / qMax(1, windowSize.height()));
        QColor color = stroke.color;
        color.setAlphaF(color.alphaF() * layer.opacity);
        const quint32 pipeline = pipelineForBlendMode(layer.blendMode);
        const quint32 mode = pipeline == kNormalBlendPipeline
            ? kBrushMode : kPremultipliedBrushMode;
        for (const QPointF& point : stroke.points) {
            const QPointF canvasPoint(
                canvas.left() + (point.x() + layer.offset.x()) * scale,
                canvas.top() + (point.y() + layer.offset.y()) * scale);
            drawQuad(commandBuffer, canvasPoint, extent, color, mode);
        }
        Q_UNUSED(canvasSize);
    }

    QRectF canvasRect(const QSize& canvasSize) const
    {
        const qreal availableWidth = qMax(1, m_window->width() - 64);
        const qreal availableHeight = qMax(1, m_window->height() - 64);
        const qreal scale = qMin(availableWidth / qMax(1, canvasSize.width()),
                                 availableHeight / qMax(1, canvasSize.height()));
        const QSizeF displaySize(canvasSize.width() * scale, canvasSize.height() * scale);
        return QRectF((m_window->width() - displaySize.width()) * 0.5,
                      (m_window->height() - displaySize.height()) * 0.5,
                      displaySize.width(), displaySize.height());
    }

    VulkanWindow* m_window;
    QVulkanDeviceFunctions* m_functions = nullptr;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;
    VkDescriptorSet m_defaultDescriptor = VK_NULL_HANDLE;
    VkShaderModule m_vertexShader = VK_NULL_HANDLE;
    VkShaderModule m_fragmentShader = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipelines[kBlendPipelineCount]{};
    QVector<LayerTexture> m_layerTextures;
    quint64 m_textureGeneration = std::numeric_limits<quint64>::max();
    quint64 m_failedTextureGeneration = std::numeric_limits<quint64>::max();
};

} // namespace

VulkanWindow::VulkanWindow(QWindow* parent)
    : QVulkanWindow(parent)
    , m_document(std::make_unique<velox::Document>(QSize(1024, 768),
                                                   QStringLiteral("Untitled")))
{
    m_document->addLayer(QStringLiteral("Layer 1"));
    m_document->setModified(false);
    m_activeLayerId = m_document->layerAt(0)->id();
    publishRenderDataLocked();
}

void VulkanWindow::reportDeviceInitialized(const QString& deviceName)
{
    Q_EMIT deviceInitialized(deviceName);
}

void VulkanWindow::reportRenderError(const QString& message)
{
    qCritical().noquote() << message;
    Q_EMIT renderError(message);
}

void VulkanWindow::setTool(CanvasTool tool)
{
    m_tool = tool;
    setCursor(tool == CanvasTool::Brush ? Qt::CrossCursor : Qt::SizeAllCursor);
}

CanvasTool VulkanWindow::tool() const
{
    return m_tool;
}

void VulkanWindow::setBrushColor(const QColor& color)
{
    if (!color.isValid()) {
        return;
    }
    m_brushColor = color;
}

void VulkanWindow::setBrushDiameter(qreal diameter)
{
    m_brushDiameter = qBound(1.0, diameter, 512.0);
}

void VulkanWindow::addLayer()
{
    {
        QMutexLocker lock(&m_mutex);
        const int index = m_document->layerCount() + 1;
        velox::Layer* layer = m_document->addLayer(
            QStringLiteral("Layer %1").arg(index));
        m_activeLayerId = layer->id();
        m_document->setModified(true);
        publishRenderDataLocked();
    }
    requestUpdate();
    Q_EMIT layersChanged();
    Q_EMIT documentChanged();
}

void VulkanWindow::setActiveLayer(int layerId)
{
    QMutexLocker lock(&m_mutex);
    if (m_document->layerById(layerId) != nullptr) {
        m_activeLayerId = layerId;
    }
}

void VulkanWindow::setLayerVisible(int layerId, bool visible)
{
    {
        QMutexLocker lock(&m_mutex);
        velox::Layer* layer = m_document->layerById(layerId);
        if (layer == nullptr || layer->visible() == visible) {
            return;
        }
        layer->setVisible(visible);
        m_document->setModified(true);
        publishRenderDataLocked();
    }
    requestUpdate();
    Q_EMIT documentChanged();
}

void VulkanWindow::setLayerBlendMode(int layerId, velox::BlendMode blendMode)
{
    {
        QMutexLocker lock(&m_mutex);
        velox::Layer* layer = m_document->layerById(layerId);
        if (layer == nullptr || layer->blendMode() == blendMode) {
            return;
        }
        layer->setBlendMode(blendMode);
        m_document->setModified(true);
        publishRenderDataLocked();
    }
    requestUpdate();
    Q_EMIT documentChanged();
}

bool VulkanWindow::importImageLayer(const QString& name, const QImage& image)
{
    const qint64 pixels = static_cast<qint64>(image.width()) * image.height();
    if (image.isNull() || image.width() > 8192 || image.height() > 8192
        || pixels <= 0 || pixels > 64LL * 1024LL * 1024LL) {
        Q_EMIT renderError(QStringLiteral(
            "Imported images must be valid and no larger than 8192 × 8192 pixels."));
        return false;
    }
    bool layerLimitReached = false;
    bool imagePixelLimitReached = false;
    {
        QMutexLocker lock(&m_mutex);
        if (m_document->layerCount() >= 256) {
            layerLimitReached = true;
        } else {
            qint64 existingImagePixels = 0;
            for (int index = 0; index < m_document->layerCount(); ++index) {
                const QSize layerSize = m_document->layerAt(index)->image().size();
                existingImagePixels += static_cast<qint64>(layerSize.width()) * layerSize.height();
            }
            imagePixelLimitReached =
                existingImagePixels + pixels > 64LL * 1024LL * 1024LL;
        }
        if (!layerLimitReached && !imagePixelLimitReached) {
            velox::Layer* layer = m_document->addLayerWithImage(
                name.isEmpty() ? QStringLiteral("Imported Image") : name, image);
            m_activeLayerId = layer->id();
            m_document->setModified(true);
            ++m_imageGeneration;
            publishRenderDataLocked();
        }
    }
    if (layerLimitReached) {
        Q_EMIT renderError(QStringLiteral("A project cannot contain more than 256 layers."));
        return false;
    }
    if (imagePixelLimitReached) {
        Q_EMIT renderError(QStringLiteral(
            "Imported image layers exceed the 64-megapixel project limit."));
        return false;
    }
    requestUpdate();
    Q_EMIT layersChanged();
    Q_EMIT documentChanged();
    return true;
}

void VulkanWindow::undo()
{
    {
        QMutexLocker lock(&m_mutex);
        if (m_undo.isEmpty()) {
            return;
        }
        const UndoRecord record = m_undo.takeLast();
        velox::Layer* layer = m_document->layerById(record.layerId);
        if (layer != nullptr) {
            layer->takeLastStroke();
            m_redo.append(record);
            m_document->setModified(true);
            publishRenderDataLocked();
        }
    }
    requestUpdate();
    Q_EMIT documentChanged();
}

void VulkanWindow::redo()
{
    {
        QMutexLocker lock(&m_mutex);
        if (m_redo.isEmpty()) {
            return;
        }
        const UndoRecord record = m_redo.takeLast();
        velox::Layer* layer = m_document->layerById(record.layerId);
        if (layer != nullptr) {
            layer->addStroke(record.stroke);
            m_undo.append(record);
            m_document->setModified(true);
            publishRenderDataLocked();
        }
    }
    requestUpdate();
    Q_EMIT documentChanged();
}

void VulkanWindow::clearSelection()
{
    {
        QMutexLocker lock(&m_mutex);
        m_hasSelection = false;
        m_isSelecting = false;
        m_selection = {};
        publishRenderDataLocked();
    }
    requestUpdate();
    Q_EMIT selectionChanged(false);
}

void VulkanWindow::newCanvas(const QSize& size)
{
    if (size.width() <= 0 || size.height() <= 0
        || size.width() > 8192 || size.height() > 8192) {
        Q_EMIT renderError(QStringLiteral("Canvas dimensions must be between 1 and 8192 pixels."));
        return;
    }
    auto document = std::make_unique<velox::Document>(size, QStringLiteral("Untitled"));
    document->addLayer(QStringLiteral("Layer 1"));
    replaceDocument(std::move(document));
}

void VulkanWindow::replaceDocument(std::unique_ptr<velox::Document> document)
{
    if (!document || document->layerCount() <= 0) {
        Q_EMIT renderError(QStringLiteral("A canvas document must contain at least one layer."));
        return;
    }
    {
        QMutexLocker lock(&m_mutex);
        m_document = std::move(document);
        m_activeLayerId = m_document->layerAt(m_document->layerCount() - 1)->id();
        ++m_imageGeneration;
        m_undo.clear();
        m_redo.clear();
        m_activeStroke.reset();
        m_selection = {};
        m_hasSelection = false;
        m_isSelecting = false;
        publishRenderDataLocked();
    }
    requestUpdate();
    Q_EMIT layersChanged();
    Q_EMIT documentChanged();
    Q_EMIT selectionChanged(false);
}

QVector<CanvasLayerInfo> VulkanWindow::layers() const
{
    QMutexLocker lock(&m_mutex);
    QVector<CanvasLayerInfo> result;
    result.reserve(m_document->layerCount());
    for (int index = m_document->layerCount() - 1; index >= 0; --index) {
        const velox::Layer* layer = m_document->layerAt(index);
        result.append({layer->id(), layer->name(), layer->visible(),
                       layer->locked(), layer->blendMode()});
    }
    return result;
}

QStringList VulkanWindow::historyEntries() const
{
    QMutexLocker lock(&m_mutex);
    QStringList entries;
    entries.reserve(m_undo.size());
    for (qsizetype index = m_undo.size(); index > 0; --index) {
        entries.append(QStringLiteral("Brush stroke — %1")
                           .arg(m_document->layerById(m_undo.at(index - 1).layerId)->name()));
    }
    return entries;
}

int VulkanWindow::undoCount() const
{
    QMutexLocker lock(&m_mutex);
    return m_undo.size();
}

int VulkanWindow::redoCount() const
{
    QMutexLocker lock(&m_mutex);
    return m_redo.size();
}

void VulkanWindow::markSaved()
{
    {
        QMutexLocker lock(&m_mutex);
        m_document->setModified(false);
    }
    Q_EMIT documentChanged();
}

int VulkanWindow::activeLayerId() const
{
    QMutexLocker lock(&m_mutex);
    return m_activeLayerId;
}

bool VulkanWindow::isModified() const
{
    QMutexLocker lock(&m_mutex);
    return m_document->isModified();
}

QRectF VulkanWindow::selection() const
{
    QMutexLocker lock(&m_mutex);
    return m_selection;
}

bool VulkanWindow::hasSelection() const
{
    QMutexLocker lock(&m_mutex);
    return m_hasSelection;
}

std::unique_ptr<velox::Document> VulkanWindow::documentSnapshot() const
{
    QMutexLocker lock(&m_mutex);
    return m_document->clone();
}

std::shared_ptr<const CanvasRenderData> VulkanWindow::renderData() const
{
    QMutexLocker lock(&m_mutex);
    return m_renderData;
}

QVulkanWindowRenderer* VulkanWindow::createRenderer()
{
    return new CanvasRenderer(this);
}

void VulkanWindow::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !canvasRectFor(m_document->canvasSize())
                                              .contains(event->position())) {
        QVulkanWindow::mousePressEvent(event);
        return;
    }

    const QPointF point = mapToCanvas(event->position());
    bool changed = false;
    bool blockedBySelection = false;
    {
        QMutexLocker lock(&m_mutex);
        if (m_tool == CanvasTool::Brush) {
            velox::Layer* layer = m_document->layerById(m_activeLayerId);
            if (layer != nullptr && !layer->locked()
                && (!m_hasSelection || m_selection.contains(point))) {
                m_activeStroke = velox::BrushStroke{};
                m_activeStroke->color = m_brushColor;
                m_activeStroke->diameter = m_brushDiameter;
                m_activeStroke->points.append(point);
                changed = true;
            } else if (layer != nullptr && !layer->locked() && m_hasSelection) {
                blockedBySelection = true;
            }
        } else {
            m_isSelecting = true;
            m_selectionStart = point;
            m_selection = QRectF(point, point);
            m_hasSelection = false;
            changed = true;
        }
        if (changed) {
            publishRenderDataLocked();
        }
    }
    if (blockedBySelection) {
        event->accept();
        return;
    }
    if (m_tool == CanvasTool::Brush && !changed) {
        Q_EMIT renderError(QStringLiteral("The active layer is locked or unavailable."));
    }
    if (changed) {
        requestUpdate();
        if (m_tool == CanvasTool::RectangleSelection) {
            Q_EMIT selectionChanged(false);
        }
    }
    event->accept();
}

void VulkanWindow::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF point = mapToCanvas(event->position());
    bool changed = false;
    bool selectionActive = false;
    {
        QMutexLocker lock(&m_mutex);
        if (m_activeStroke && !m_activeStroke->points.isEmpty()) {
            const QPointF previous = m_activeStroke->points.constLast();
            const qreal distance = QLineF(previous, point).length();
            const qreal spacing = qMax(1.0, m_brushDiameter * 0.18);
            const int steps = qBound(1, static_cast<int>(std::ceil(distance / spacing)), 256);
            for (int step = 1; step <= steps; ++step) {
                const qreal amount = static_cast<qreal>(step) / steps;
                const QPointF sample = previous + (point - previous) * amount;
                if (!m_hasSelection || m_selection.contains(sample)) {
                    m_activeStroke->points.append(sample);
                }
            }
            changed = true;
        } else if (m_isSelecting) {
            const QPointF current = point;
            m_selection = QRectF(m_selectionStart, current).normalized();
            m_hasSelection = m_selection.width() > 1.0 && m_selection.height() > 1.0;
            selectionActive = m_hasSelection;
            changed = true;
        }
        if (changed) {
            publishRenderDataLocked();
        }
    }
    if (changed) {
        requestUpdate();
        if (m_isSelecting) {
            Q_EMIT selectionChanged(selectionActive);
        }
    }
    QVulkanWindow::mouseMoveEvent(event);
}

void VulkanWindow::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        bool committedStroke = false;
        bool selectionActive = false;
        {
            QMutexLocker lock(&m_mutex);
            if (m_activeStroke) {
                velox::Layer* layer = m_document->layerById(m_activeLayerId);
                if (layer != nullptr && !m_activeStroke->points.isEmpty()) {
                    layer->addStroke(*m_activeStroke);
                    m_undo.append({m_activeLayerId, *m_activeStroke});
                    m_redo.clear();
                    m_document->setModified(true);
                    committedStroke = true;
                }
                m_activeStroke.reset();
                publishRenderDataLocked();
            }
            if (m_isSelecting) {
                m_isSelecting = false;
                selectionActive = m_hasSelection;
                publishRenderDataLocked();
            }
        }
        requestUpdate();
        if (committedStroke) {
            Q_EMIT documentChanged();
            Q_EMIT layersChanged();
        }
        if (m_tool == CanvasTool::RectangleSelection) {
            Q_EMIT selectionChanged(selectionActive);
        }
        event->accept();
        return;
    }
    QVulkanWindow::mouseReleaseEvent(event);
}

void VulkanWindow::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        clearSelection();
        event->accept();
        return;
    }
    QVulkanWindow::keyPressEvent(event);
}

QRectF VulkanWindow::canvasRectFor(const QSize& size) const
{
    const qreal availableWidth = qMax(1, width() - 64);
    const qreal availableHeight = qMax(1, height() - 64);
    const qreal scale = qMin(availableWidth / qMax(1, size.width()),
                             availableHeight / qMax(1, size.height()));
    const QSizeF displaySize(size.width() * scale, size.height() * scale);
    return QRectF((width() - displaySize.width()) * 0.5,
                  (height() - displaySize.height()) * 0.5,
                  displaySize.width(), displaySize.height());
}

QPointF VulkanWindow::mapToCanvas(const QPointF& position) const
{
    QSize canvasSize;
    {
        QMutexLocker lock(&m_mutex);
        canvasSize = m_document->canvasSize();
    }
    const QRectF rect = canvasRectFor(canvasSize);
    if (rect.isEmpty()) {
        return {};
    }
    const qreal x = (position.x() - rect.left()) * canvasSize.width() / rect.width();
    const qreal y = (position.y() - rect.top()) * canvasSize.height() / rect.height();
    return QPointF(qBound(0.0, x, static_cast<qreal>(canvasSize.width())),
                   qBound(0.0, y, static_cast<qreal>(canvasSize.height())));
}

void VulkanWindow::publishRenderDataLocked()
{
    auto data = std::make_shared<CanvasRenderData>();
    data->generation = ++m_renderGeneration;
    data->imageGeneration = m_imageGeneration;
    data->canvasSize = m_document->canvasSize();
    data->selection = m_selection;
    data->hasSelection = m_hasSelection;
    data->activeStroke = m_activeStroke;
    data->layers.reserve(m_document->layerCount());
    for (int index = 0; index < m_document->layerCount(); ++index) {
        const velox::Layer* layer = m_document->layerAt(index);
        data->layers.append({layer->id(), layer->offset(), layer->opacity(), layer->visible(),
                             layer->blendMode(), layer->image(), layer->strokes()});
    }
    m_renderData = std::move(data);
}
