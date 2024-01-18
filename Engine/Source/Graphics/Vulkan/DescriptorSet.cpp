#if API_VULKAN

#include "Graphics/Vulkan/DescriptorSet.h"
#include "Graphics/Vulkan/VulkanContext.h"
#include "Graphics/Vulkan/VulkanUtils.h"

#include "Renderer.h"

#include "Assertion.h"

// Referenced: https://vkguide.dev/docs/extra-chapter/abstracting_descriptors/

DescriptorSet DescriptorSet::Begin(const char* name)
{
    DescriptorSet retSet;
    retSet.mName = name;
}

DescriptorSet& DescriptorSet::WriteImage(int32_t binding, Image* image)
{
    DescriptorBinding bindInfo;
    bindInfo.mType = DescriptorType::Image;
    bindInfo.mObject = image;
    bindInfo.mBinding = binding;
    mBindings.push_back(bindInfo);
}

DescriptorSet& DescriptorSet::WriteImageArray(int32_t binding, const std::vector<Image*>& imageArray)
{
    DescriptorBinding bindInfo;
    bindInfo.mType = DescriptorType::ImageArray;
    bindInfo.mObject = nullptr;
    bindInfo.mImageArray = imageArray;
    bindInfo.mBinding = binding;
    mBindings.push_back(bindInfo);
}

DescriptorSet& DescriptorSet::WriteUniformBuffer(int32_t binding, UniformBuffer* uniformBuffer)
{
    DescriptorBinding bindInfo;
    bindInfo.mType = DescriptorType::Uniform;
    bindInfo.mObject = uniformBuffer;
    bindInfo.mSize = (uint32_t)uniformBuffer->GetSize();
    bindInfo.mBinding = binding;
    mBindings.push_back(bindInfo);
}

DescriptorSet& DescriptorSet::WriteUniformBuffer(int32_t binding, const UniformBlock& block)
{
    DescriptorBinding bindInfo;
    bindInfo.mType = DescriptorType::Uniform;
    bindInfo.mObject = block.mUniformBuffer;
    bindInfo.mOffset = block.mOffset;
    bindInfo.mSize = block.mSize;
    bindInfo.mBinding = binding;
    mBindings.push_back(bindInfo);
}

DescriptorSet& DescriptorSet::WriteStorageBuffer(int32_t binding, Buffer* storageBuffer)
{
    DescriptorBinding bindInfo;
    bindInfo.mType = DescriptorType::StorageBuffer;
    bindInfo.mObject = storageBuffer;
    bindInfo.mBinding = binding;
    mBindings.push_back(bindInfo);
}

DescriptorSet& DescriptorSet::WriteStorageImage(int32_t binding, Image* storageImage)
{
    DescriptorBinding bindInfo;
    bindInfo.mType = DescriptorType::StorageImage;
    bindInfo.mObject = storageImage;
    bindInfo.mBinding = binding;
    mBindings.push_back(bindInfo);
}

DescriptorSet& DescriptorSet::Build()
{
    OCT_ASSERT(mDescriptorSet == VK_NULL_HANDLE);

    // Build or Reuse DescriptorSetLayout from LayoutCache.
#error Build layout using LayoutCache

    // Allocate descriptor set from DescriptorPool
#error Allocate set

    // Update descriptor sets
    UpdateDescriptors();
}

void DescriptorSet::Bind(VkCommandBuffer cb, uint32_t index)
{
    Pipeline* pipeline = GetVulkanContext()->GetBoundPipeline();

    VkPipelineBindPoint bindPoint = pipeline->IsComputePipeline() ?
        VK_PIPELINE_BIND_POINT_COMPUTE :
        VK_PIPELINE_BIND_POINT_GRAPHICS;

    VkPipelineLayout pipelineLayout = pipeline->GetPipelineLayout();

    static std::vector<uint32_t> dynOffsets;
    dynOffsets.clear();
    for (uint32_t i = 0; i < mBindings.size(); ++i)
    {
        // We use a dynamic buffer for uniforms
        if (mBindings[i].mType == DescriptorType::Uniform)
        {
            dynOffsets.push_back(mBindings[i].mOffset);
        }
    }

    vkCmdBindDescriptorSets(
        cb,
        bindPoint,
        pipelineLayout,
        index,
        1,
        &mDescriptorSet,
        (uint32_t)dynOffsets.size(),
        dynOffsets.data());

    mFrameBuilt = GetFrameNumber();
}

VkDescriptorSet DescriptorSet::Get() const
{
    return mDescriptorSet;
}

VkDescriptorSetLayout DescriptorSet::GetLayout() const
{
    return mLayout;
}







DescriptorSet::DescriptorSet(VkDescriptorSetLayout layout, const char* name)
{
    VkDevice device = GetVulkanDevice();
    VkDescriptorPool pool = GetVulkanContext()->GetDescriptorPool();

    VkDescriptorSetLayout layouts[] = { layout };
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = layouts;

    mName = name;

    for (uint32_t i = 0; i < MAX_FRAMES; ++i)
    {
        if (vkAllocateDescriptorSets(device, &allocInfo, &mDescriptorSets[i]) != VK_SUCCESS)
        {
            LogError("Failed to allocate descriptor set");
            OCT_ASSERT(0);
        }

        if (mName != "")
        {
            SetDebugObjectName(VK_OBJECT_TYPE_DESCRIPTOR_SET, (uint64_t)mDescriptorSets[i], mName.c_str());
        }
    }
}

DescriptorSet::~DescriptorSet()
{
    VkDevice device = GetVulkanDevice();
    VkDescriptorPool pool = GetVulkanContext()->GetDescriptorPool();

    for (uint32_t i = 0; i < MAX_FRAMES; ++i)
    {
        vkFreeDescriptorSets(device, pool, 1, &mDescriptorSets[i]);
        mDescriptorSets[i] = VK_NULL_HANDLE;
    }
}

void DescriptorSet::UpdateDescriptors()
{
    VkDevice device = GetVulkanDevice();

    // TODO: Merge all of the updates for this set into a single vkUpdateDescriptorSets() call.
    // Might want to store VkDescriptorImageInfo and VkDescriptorBufferInfo on Image and Buffer resources.
    // And we could possibly just use a static std::vector<> for the VkWriteDescriptorSet structs.
    for (uint32_t i = 0; i < mBindings.size(); ++i)
    {
        DescriptorBinding& binding = mBindings[i];
        if (binding.mObject != nullptr || binding.mType == DescriptorType::ImageArray)
        {
            if (binding.mType == DescriptorType::Image)
            {
                Image* image = reinterpret_cast<Image*>(binding.mObject);

                VkDescriptorImageInfo imageInfo = {};
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfo.imageView = image->GetView();
                imageInfo.sampler = image->GetSampler();

                VkWriteDescriptorSet descriptorWrite = {};
                descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descriptorWrite.dstSet = mDescriptorSet;
                descriptorWrite.dstBinding = i;
                descriptorWrite.dstArrayElement = 0;
                descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                descriptorWrite.descriptorCount = 1;
                descriptorWrite.pImageInfo = &imageInfo;

                vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
            }
            else if (binding.mType == DescriptorType::ImageArray)
            {
                static std::vector<VkDescriptorImageInfo> sDescImageInfo;
                sDescImageInfo.resize(binding.mImageArray.size());

                if (binding.mImageArray.size() > 0)
                {
                    for (uint32_t i = 0; i < binding.mImageArray.size(); ++i)
                    {
                        VkDescriptorImageInfo& imageInfo = sDescImageInfo[i];
                        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                        imageInfo.imageView = binding.mImageArray[i]->GetView();
                        imageInfo.sampler = binding.mImageArray[i]->GetSampler();
                    }

                    VkWriteDescriptorSet descriptorWrite = {};
                    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    descriptorWrite.dstSet = mDescriptorSet;
                    descriptorWrite.dstBinding = i;
                    descriptorWrite.dstArrayElement = 0;
                    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    descriptorWrite.descriptorCount = (uint32_t)binding.mImageArray.size();
                    descriptorWrite.pImageInfo = sDescImageInfo.data();

                    vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
                }
            }
            else if (binding.mType == DescriptorType::Uniform)
            {
                UniformBuffer* uniformBuffer = reinterpret_cast<UniformBuffer*>(binding.mObject);

                VkDescriptorBufferInfo bufferInfo = {};
                bufferInfo.buffer = uniformBuffer->Get();
                bufferInfo.range = binding.mSize;
                bufferInfo.offset = 0;

                VkWriteDescriptorSet descriptorWrite = {};
                descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descriptorWrite.dstSet = mDescriptorSet;
                descriptorWrite.dstBinding = i;
                descriptorWrite.dstArrayElement = 0;
                descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
                descriptorWrite.descriptorCount = 1;
                descriptorWrite.pBufferInfo = &bufferInfo;

                vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
            }
            else if (binding.mType == DescriptorType::StorageBuffer)
            {
                Buffer* buffer = reinterpret_cast<Buffer*>(binding.mObject);

                VkDescriptorBufferInfo bufferInfo = {};
                bufferInfo.buffer = buffer->Get();
                bufferInfo.range = buffer->GetSize();
                bufferInfo.offset = 0;

                VkWriteDescriptorSet descriptorWrite = {};
                descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descriptorWrite.dstSet = mDescriptorSet;
                descriptorWrite.dstBinding = i;
                descriptorWrite.dstArrayElement = 0;
                descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                descriptorWrite.descriptorCount = 1;
                descriptorWrite.pBufferInfo = &bufferInfo;

                vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
            }
            else if (binding.mType == DescriptorType::StorageImage)
            {
                Image* image = reinterpret_cast<Image*>(binding.mObject);

                VkDescriptorImageInfo imageInfo = {};
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                imageInfo.imageView = image->GetView();
                imageInfo.sampler = image->GetSampler();

                VkWriteDescriptorSet descriptorWrite = {};
                descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descriptorWrite.dstSet = mDescriptorSet;
                descriptorWrite.dstBinding = i;
                descriptorWrite.dstArrayElement = 0;
                descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                descriptorWrite.descriptorCount = 1;
                descriptorWrite.pImageInfo = &imageInfo;

                vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
            }
        }
    }
}

#endif