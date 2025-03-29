#include "common/gpu/dx_compat.hpp"
#include <stdexcept>
#include <sstream>

namespace anarchy {
namespace gpu {

DXCompat::DXCompat(const DXConfig& config)
    : config_(config)
    , current_fence_value_(0)
{
}

DXCompat::~DXCompat() {
    cleanupResources();
}

bool DXCompat::initialize() {
    if (!initializeDXGI()) {
        return false;
    }

    if (config_.use_d3d12) {
        if (!initializeD3D12()) {
            return false;
        }
    } else {
        if (!initializeD3D11()) {
            return false;
        }
    }

    return true;
}

bool DXCompat::initializeD3D11() {
    UINT create_device_flags = 0;
    if (config_.enable_debug_layer) {
        create_device_flags |= D3D11_CREATE_DEVICE_DEBUG;
    }

    D3D_FEATURE_LEVEL feature_levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };
    UINT num_feature_levels = ARRAYSIZE(feature_levels);

    HRESULT hr = D3D11CreateDevice(
        dx_.adapter.Get(),
        D3D_DRIVER_TYPE_UNKNOWN,
        nullptr,
        create_device_flags,
        feature_levels,
        num_feature_levels,
        D3D11_SDK_VERSION,
        &dx_.d3d11_device,
        nullptr,
        &dx_.d3d11_context
    );

    if (FAILED(hr)) {
        return false;
    }

    return true;
}

bool DXCompat::initializeD3D12() {
    UINT create_device_flags = 0;
    if (config_.enable_debug_layer) {
        create_device_flags |= D3D12_CREATE_DEVICE_DEBUG;
    }

    HRESULT hr = D3D12CreateDevice(
        dx_.adapter.Get(),
        static_cast<D3D_FEATURE_LEVEL>(config_.feature_level),
        IID_PPV_ARGS(&dx_.d3d12_device),
        create_device_flags
    );

    if (FAILED(hr)) {
        return false;
    }

    return true;
}

bool DXCompat::initializeDXGI() {
    UINT create_factory_flags = 0;
    if (config_.enable_debug_layer) {
        create_factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
    }

    HRESULT hr = CreateDXGIFactory2(create_factory_flags, IID_PPV_ARGS(&dx_.dxgi_factory));
    if (FAILED(hr)) {
        return false;
    }

    // Get the first adapter
    hr = dx_.dxgi_factory->EnumAdapters(0, &dx_.adapter);
    if (FAILED(hr)) {
        return false;
    }

    return true;
}

void DXCompat::cleanupResources() {
    // Clear resource tracking
    {
        std::lock_guard<std::mutex> lock(resource_mutex_);
        resource_info_.clear();
    }

    // Clean up command queues and batches
    cleanupCommandQueues();
    cleanupCommandRecords();
    cleanupCommandOptimizations();

    // Release DirectX resources
    dx_.d3d11_context.Reset();
    dx_.d3d11_device.Reset();
    dx_.d3d12_device.Reset();
    dx_.dxgi_factory.Reset();
    dx_.adapter.Reset();
}

void DXCompat::cleanupCommandQueues() {
    std::lock_guard<std::mutex> lock(command_queue_mutex_);
    command_queues_.clear();
    command_batches_.clear();
    current_fence_value_ = 0;
}

void DXCompat::cleanupCommandRecords() {
    std::lock_guard<std::mutex> lock(command_record_mutex_);
    command_records_.clear();
}

void DXCompat::cleanupCommandOptimizations() {
    std::lock_guard<std::mutex> lock(command_optimization_mutex_);
    command_dependencies_.clear();
    command_optimizations_.clear();
}

HRESULT DXCompat::getOrCreateCommandQueue(
    D3D12_COMMAND_LIST_TYPE type,
    UINT node_mask,
    UINT priority,
    D3D12_COMMAND_QUEUE_FLAGS flags,
    ID3D12CommandQueue** ppCommandQueue)
{
    if (!dx_.d3d12_device || !ppCommandQueue) {
        return E_INVALIDARG;
    }

    std::lock_guard<std::mutex> lock(command_queue_mutex_);
    auto it = command_queues_.find(type);
    if (it != command_queues_.end()) {
        // Return existing queue if it matches the requested parameters
        if (it->second.node_mask == node_mask &&
            it->second.priority == priority &&
            it->second.flags == flags) {
            *ppCommandQueue = it->second.queue.Get();
            (*ppCommandQueue)->AddRef();
            return S_OK;
        }
    }

    // Create new command queue
    D3D12_COMMAND_QUEUE_DESC desc = {};
    desc.Type = type;
    desc.NodeMask = node_mask;
    desc.Priority = priority;
    desc.Flags = flags;

    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
    HRESULT hr = dx_.d3d12_device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue));
    if (FAILED(hr)) {
        return hr;
    }

    // Store the new queue
    CommandQueue cmd_queue = {
        queue,
        type,
        node_mask,
        priority,
        flags
    };
    command_queues_[type] = std::move(cmd_queue);

    *ppCommandQueue = queue.Get();
    (*ppCommandQueue)->AddRef();
    return S_OK;
}

HRESULT DXCompat::createCommandListBatch(
    D3D12_COMMAND_LIST_TYPE type,
    CommandListBatch** ppBatch)
{
    if (!ppBatch) {
        return E_INVALIDARG;
    }

    std::lock_guard<std::mutex> lock(command_queue_mutex_);
    command_batches_.emplace_back();
    *ppBatch = &command_batches_.back();
    (*ppBatch)->fence_value = ++current_fence_value_;
    (*ppBatch)->is_closed = false;
    return S_OK;
}

HRESULT DXCompat::recordCommandList(
    ID3D12CommandList* pCommandList,
    const void* pCommandData,
    size_t command_size)
{
    if (!pCommandList || !pCommandData || command_size == 0) {
        return E_INVALIDARG;
    }

    std::lock_guard<std::mutex> lock(command_record_mutex_);
    
    // Get command list type
    D3D12_COMMAND_LIST_TYPE type;
    HRESULT hr = pCommandList->GetType(&type);
    if (FAILED(hr)) {
        return hr;
    }

    // Create or update command record
    CommandListRecord& record = command_records_[pCommandList];
    record.type = type;
    record.fence_value = ++current_fence_value_;
    record.is_closed = false;
    record.is_executing = false;

    // Store command data
    record.command_data.resize(command_size);
    std::memcpy(record.command_data.data(), pCommandData, command_size);

    return S_OK;
}

HRESULT DXCompat::playbackCommandList(
    ID3D12CommandList* pCommandList,
    ID3D12CommandQueue* pCommandQueue)
{
    if (!pCommandList || !pCommandQueue) {
        return E_INVALIDARG;
    }

    std::lock_guard<std::mutex> lock(command_record_mutex_);
    
    auto it = command_records_.find(pCommandList);
    if (it == command_records_.end()) {
        return E_INVALIDARG;
    }

    CommandListRecord& record = it->second;
    if (record.is_executing) {
        return E_INVALIDARG;
    }

    // Mark as executing
    record.is_executing = true;

    // Execute the command list
    pCommandQueue->ExecuteCommandLists(1, &pCommandList);

    // Signal fence
    ID3D12Fence* fence = nullptr;
    HRESULT hr = dx_.d3d12_device->CreateFence(
        record.fence_value,
        D3D12_FENCE_FLAG_NONE,
        IID_PPV_ARGS(&fence)
    );

    if (SUCCEEDED(hr)) {
        hr = pCommandQueue->Signal(fence, record.fence_value);
        fence->Release();
    }

    // Reset execution state
    record.is_executing = false;

    return hr;
}

HRESULT DXCompat::addCommandListDependency(
    ID3D12CommandList* pCommandList,
    ID3D12CommandList* pDependentList,
    UINT64 FenceValue)
{
    if (!pCommandList || !pDependentList) {
        return E_INVALIDARG;
    }

    std::lock_guard<std::mutex> lock(command_optimization_mutex_);
    
    CommandListDependency dependency = {
        pDependentList,
        FenceValue,
        false
    };

    command_dependencies_[pCommandList].push_back(dependency);
    return S_OK;
}

HRESULT DXCompat::checkCommandListDependencies(
    ID3D12CommandList* pCommandList)
{
    if (!pCommandList) {
        return E_INVALIDARG;
    }

    std::lock_guard<std::mutex> lock(command_optimization_mutex_);
    
    auto it = command_dependencies_.find(pCommandList);
    if (it == command_dependencies_.end()) {
        return S_OK;
    }

    for (const auto& dependency : it->second) {
        if (!dependency.is_ready) {
            // Check if the dependent command list has completed
            ID3D12Fence* fence = nullptr;
            HRESULT hr = dx_.d3d12_device->CreateFence(
                dependency.fence_value,
                D3D12_FENCE_FLAG_NONE,
                IID_PPV_ARGS(&fence)
            );

            if (SUCCEEDED(hr)) {
                // Create an event for fence completion
                HANDLE fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
                if (fence_event) {
                    hr = fence->SetEventOnCompletion(dependency.fence_value, fence_event);
                    if (SUCCEEDED(hr)) {
                        DWORD wait_result = WaitForSingleObject(fence_event, 0);
                        if (wait_result == WAIT_OBJECT_0) {
                            dependency.is_ready = true;
                        }
                    }
                    CloseHandle(fence_event);
                }
                fence->Release();
            }
        }
    }

    // Check if all dependencies are ready
    bool all_ready = true;
    for (const auto& dependency : it->second) {
        if (!dependency.is_ready) {
            all_ready = false;
            break;
        }
    }

    return all_ready ? S_OK : E_PENDING;
}

HRESULT DXCompat::optimizeCommandList(
    ID3D12CommandList* pCommandList)
{
    if (!pCommandList) {
        return E_INVALIDARG;
    }

    std::lock_guard<std::mutex> lock(command_optimization_mutex_);
    
    auto it = command_optimizations_.find(pCommandList);
    if (it != command_optimizations_.end() && it->second.is_optimized) {
        return S_OK;
    }

    // Get command list type
    D3D12_COMMAND_LIST_TYPE type;
    HRESULT hr = pCommandList->GetType(&type);
    if (FAILED(hr)) {
        return hr;
    }

    // Create optimization record
    CommandListOptimization& optimization = command_optimizations_[pCommandList];
    optimization.is_optimized = false;
    optimization.optimization_timestamp = GetTickCount64();

    // Check for mergeable command lists
    for (const auto& [other_list, other_opt] : command_optimizations_) {
        if (other_list != pCommandList && other_opt.is_optimized) {
            D3D12_COMMAND_LIST_TYPE other_type;
            if (SUCCEEDED(other_list->GetType(&other_type)) && other_type == type) {
                optimization.merged_lists.push_back(other_list);
            }
        }
    }

    // Mark as optimized if we found mergeable lists
    optimization.is_optimized = !optimization.merged_lists.empty();
    return S_OK;
}

HRESULT DXCompat::ExecuteCommandList(
    ID3D12CommandList* pCommandList,
    ID3D12Fence* pFence,
    UINT64 FenceValue)
{
    if (!dx_.d3d12_device || !pCommandList || !pFence) {
        return E_INVALIDARG;
    }

    // Check dependencies
    HRESULT hr = checkCommandListDependencies(pCommandList);
    if (FAILED(hr) && hr != E_PENDING) {
        return hr;
    }

    // Optimize command list
    hr = optimizeCommandList(pCommandList);
    if (FAILED(hr)) {
        return hr;
    }

    // Get the command queue type from the command list
    D3D12_COMMAND_LIST_TYPE type;
    hr = pCommandList->GetType(&type);
    if (FAILED(hr)) {
        return hr;
    }

    // Get or create the appropriate command queue
    ID3D12CommandQueue* command_queue = nullptr;
    hr = getOrCreateCommandQueue(
        type,
        0,  // node_mask
        0,  // priority
        D3D12_COMMAND_QUEUE_FLAG_NONE,
        &command_queue
    );

    if (FAILED(hr)) {
        return hr;
    }

    // Record command list if not already recorded
    {
        std::lock_guard<std::mutex> lock(command_record_mutex_);
        if (command_records_.find(pCommandList) == command_records_.end()) {
            // TODO: Get actual command data size and data
            hr = recordCommandList(pCommandList, nullptr, 0);
            if (FAILED(hr)) {
                command_queue->Release();
                return hr;
            }
        }
    }

    // Playback the command list
    hr = playbackCommandList(pCommandList, command_queue);
    if (FAILED(hr)) {
        command_queue->Release();
        return hr;
    }

    // Signal the fence
    hr = command_queue->Signal(pFence, FenceValue);
    command_queue->Release();

    return hr;
}

HRESULT DXCompat::Signal(
    ID3D12Fence* pFence,
    UINT64 FenceValue)
{
    if (!dx_.d3d12_device || !pFence) {
        return E_INVALIDARG;
    }

    // Get the default command queue
    ID3D12CommandQueue* command_queue = nullptr;
    HRESULT hr = getOrCreateCommandQueue(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        0,  // node_mask
        0,  // priority
        D3D12_COMMAND_QUEUE_FLAG_NONE,
        &command_queue
    );

    if (FAILED(hr)) {
        return hr;
    }

    // Signal the fence
    hr = command_queue->Signal(pFence, FenceValue);
    command_queue->Release();

    return hr;
}

HRESULT DXCompat::WaitForFence(
    ID3D12Fence* pFence,
    UINT64 FenceValue)
{
    if (!dx_.d3d12_device || !pFence) {
        return E_INVALIDARG;
    }

    // Create an event for fence completion
    HANDLE fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!fence_event) {
        return E_FAIL;
    }

    // Set up fence completion notification
    HRESULT hr = pFence->SetEventOnCompletion(FenceValue, fence_event);
    if (FAILED(hr)) {
        CloseHandle(fence_event);
        return hr;
    }

    // Wait for the fence to complete
    DWORD wait_result = WaitForSingleObject(fence_event, INFINITE);
    CloseHandle(fence_event);

    if (wait_result != WAIT_OBJECT_0) {
        return E_FAIL;
    }

    return S_OK;
}

HRESULT DXCompat::D3D11CreateDevice(
    IDXGIAdapter* pAdapter,
    D3D_DRIVER_TYPE DriverType,
    HMODULE Software,
    UINT Flags,
    const D3D_FEATURE_LEVEL* pFeatureLevels,
    UINT FeatureLevels,
    UINT SDKVersion,
    ID3D11Device** ppDevice,
    D3D_FEATURE_LEVEL* pFeatureLevel,
    ID3D11DeviceContext** ppImmediateContext)
{
    if (!dx_.d3d11_device) {
        return E_FAIL;
    }

    if (ppDevice) {
        *ppDevice = dx_.d3d11_device.Get();
        (*ppDevice)->AddRef();
    }

    if (ppImmediateContext) {
        *ppImmediateContext = dx_.d3d11_context.Get();
        (*ppImmediateContext)->AddRef();
    }

    if (pFeatureLevel) {
        *pFeatureLevel = D3D_FEATURE_LEVEL_11_1;
    }

    return S_OK;
}

HRESULT DXCompat::D3D12CreateDevice(
    IUnknown* pAdapter,
    D3D_FEATURE_LEVEL FeatureLevel,
    REFIID riid,
    void** ppDevice)
{
    if (!dx_.d3d12_device) {
        return E_FAIL;
    }

    if (ppDevice) {
        *ppDevice = dx_.d3d12_device.Get();
        static_cast<IUnknown*>(*ppDevice)->AddRef();
    }

    return S_OK;
}

HRESULT DXCompat::CreateDXGIFactory(REFIID riid, void** ppFactory) {
    return CreateDXGIFactory2(0, riid, ppFactory);
}

HRESULT DXCompat::CreateDXGIFactory1(REFIID riid, void** ppFactory) {
    return CreateDXGIFactory2(0, riid, ppFactory);
}

HRESULT DXCompat::CreateDXGIFactory2(UINT Flags, REFIID riid, void** ppFactory) {
    if (!dx_.dxgi_factory) {
        return E_FAIL;
    }

    if (ppFactory) {
        *ppFactory = dx_.dxgi_factory.Get();
        static_cast<IUnknown*>(*ppFactory)->AddRef();
    }

    return S_OK;
}

HRESULT DXCompat::CreateBuffer(
    ID3D11Device* pDevice,
    const D3D11_BUFFER_DESC* pDesc,
    const D3D11_SUBRESOURCE_DATA* pInitialData,
    ID3D11Buffer** ppBuffer)
{
    return createResource(pDevice, D3D11_RESOURCE_DIMENSION_BUFFER, pDesc, pInitialData, (void**)ppBuffer);
}

HRESULT DXCompat::CreateTexture2D(
    ID3D11Device* pDevice,
    const D3D11_TEXTURE2D_DESC* pDesc,
    const D3D11_SUBRESOURCE_DATA* pInitialData,
    ID3D11Texture2D** ppTexture2D)
{
    return createResource(pDevice, D3D11_RESOURCE_DIMENSION_TEXTURE2D, pDesc, pInitialData, (void**)ppTexture2D);
}

HRESULT DXCompat::createResource(
    ID3D11Device* pDevice,
    const D3D11_RESOURCE_DIMENSION dimension,
    const D3D11_BUFFER_DESC* pDesc,
    const D3D11_SUBRESOURCE_DATA* pInitialData,
    void** ppResource)
{
    if (!pDevice || !pDesc || !ppResource) {
        return E_INVALIDARG;
    }

    HRESULT hr = S_OK;
    switch (dimension) {
        case D3D11_RESOURCE_DIMENSION_BUFFER:
            hr = pDevice->CreateBuffer(pDesc, pInitialData, (ID3D11Buffer**)ppResource);
            break;
        case D3D11_RESOURCE_DIMENSION_TEXTURE2D:
            hr = pDevice->CreateTexture2D((const D3D11_TEXTURE2D_DESC*)pDesc, pInitialData, (ID3D11Texture2D**)ppResource);
            break;
        default:
            return E_INVALIDARG;
    }

    if (SUCCEEDED(hr)) {
        ResourceInfo info;
        info.dimension = dimension;
        info.usage = pDesc->Usage;
        info.bind_flags = pDesc->BindFlags;
        info.cpu_access_flags = pDesc->CPUAccessFlags;
        info.misc_flags = pDesc->MiscFlags;
        info.structure_byte_stride = pDesc->StructureByteStride;

        trackResource(static_cast<ID3D11Resource*>(*ppResource), info);
    }

    return hr;
}

void DXCompat::trackResource(ID3D11Resource* pResource, const ResourceInfo& info) {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    resource_info_[pResource] = info;
}

void DXCompat::untrackResource(ID3D11Resource* pResource) {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    resource_info_.erase(pResource);
}

HRESULT DXCompat::Map(
    ID3D11Resource* pResource,
    UINT Subresource,
    D3D11_MAP MapType,
    UINT MapFlags,
    D3D11_MAPPED_SUBRESOURCE* pMappedResource)
{
    if (!pResource || !pMappedResource) {
        return E_INVALIDARG;
    }

    std::lock_guard<std::mutex> lock(resource_mutex_);
    auto it = resource_info_.find(pResource);
    if (it == resource_info_.end()) {
        return E_INVALIDARG;
    }

    const ResourceInfo& info = it->second;

    // Check if the resource supports the requested mapping operation
    if (info.usage == D3D11_USAGE_IMMUTABLE) {
        return E_INVALIDARG;
    }

    if (MapType == D3D11_MAP_WRITE_DISCARD || MapType == D3D11_MAP_WRITE_NO_OVERWRITE) {
        if (!(info.cpu_access_flags & D3D11_CPU_ACCESS_WRITE)) {
            return E_INVALIDARG;
        }
    } else if (MapType == D3D11_MAP_READ) {
        if (!(info.cpu_access_flags & D3D11_CPU_ACCESS_READ)) {
            return E_INVALIDARG;
        }
    } else if (MapType == D3D11_MAP_READ_WRITE) {
        if (!(info.cpu_access_flags & (D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE))) {
            return E_INVALIDARG;
        }
    }

    // Calculate resource size and row pitch
    UINT64 resource_size = 0;
    UINT row_pitch = 0;
    UINT depth_pitch = 0;

    if (info.dimension == D3D11_RESOURCE_DIMENSION_BUFFER) {
        resource_size = info.structure_byte_stride;
    } else if (info.dimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
        const D3D11_TEXTURE2D_DESC* tex_desc = static_cast<const D3D11_TEXTURE2D_DESC*>(pResource);
        resource_size = tex_desc->Width * tex_desc->Height * 4; // Assuming RGBA format
        row_pitch = tex_desc->Width * 4;
        depth_pitch = row_pitch * tex_desc->Height;
    }

    // Allocate mapped memory
    void* mapped_data = nullptr;
    if (MapType == D3D11_MAP_WRITE_DISCARD) {
        // For write discard, we can allocate new memory
        mapped_data = new uint8_t[resource_size];
    } else {
        // For other map types, we need to preserve existing data
        // This would typically involve copying from GPU memory
        mapped_data = new uint8_t[resource_size];
        // TODO: Copy data from GPU memory
    }

    if (!mapped_data) {
        return E_OUTOFMEMORY;
    }

    // Store mapping information
    pMappedResource->pData = mapped_data;
    pMappedResource->RowPitch = row_pitch;
    pMappedResource->DepthPitch = depth_pitch;

    return S_OK;
}

void DXCompat::Unmap(
    ID3D11Resource* pResource,
    UINT Subresource)
{
    if (!pResource) {
        return;
    }

    std::lock_guard<std::mutex> lock(resource_mutex_);
    auto it = resource_info_.find(pResource);
    if (it == resource_info_.end()) {
        return;
    }

    // Get the mapped data
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (SUCCEEDED(pResource->Map(Subresource, D3D11_MAP_READ, 0, &mapped))) {
        // Free the mapped memory
        delete[] static_cast<uint8_t*>(mapped.pData);
        pResource->Unmap(Subresource);
    }
}

#ifdef HAS_DIRECTX

HRESULT DXCompat::ExecuteCommandList(ID3D12CommandList* pCommandList, BOOL WaitForCompletion) {
    if (!pCommandList) {
        return E_INVALIDARG;
    }

    std::lock_guard<std::mutex> lock(command_records_mutex_);
    auto it = command_records_.find(pCommandList);
    if (it == command_records_.end()) {
        return playbackCommandList(pCommandList);
    }
    return S_OK;
}

HRESULT DXCompat::recordCommandList(ID3D12CommandList* pCommandList) {
    if (!pCommandList) {
        return E_INVALIDARG;
    }

    std::lock_guard<std::mutex> lock(command_records_mutex_);
    CommandListRecord record;
    record.type = D3D12_COMMAND_LIST_TYPE_DIRECT;  // Default to direct command list
    record.fenceValue = 0;
    command_records_[pCommandList] = record;
    return S_OK;
}

HRESULT DXCompat::playbackCommandList(ID3D12CommandList* pCommandList) {
    if (!pCommandList) {
        return E_INVALIDARG;
    }

    std::lock_guard<std::mutex> lock(command_records_mutex_);
    auto it = command_records_.find(pCommandList);
    if (it == command_records_.end()) {
        return E_FAIL;
    }
    return S_OK;
}

void DXCompat::cleanupCommandRecords() {
    std::lock_guard<std::mutex> lock(command_records_mutex_);
    command_records_.clear();
}

#endif // HAS_DIRECTX

} // namespace gpu
} // namespace anarchy 