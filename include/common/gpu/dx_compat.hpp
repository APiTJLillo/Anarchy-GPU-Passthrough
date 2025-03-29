#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

namespace anarchy {
namespace gpu {

#ifdef _WIN32
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#else
// Define Windows types for Linux compilation
typedef unsigned long DWORD;
typedef unsigned int UINT;
typedef unsigned long long UINT64;
typedef int HRESULT;
typedef void* HANDLE;
typedef const void* LPCVOID;
typedef void* LPVOID;
typedef struct { char dummy; } *ID3D11Device;
typedef struct { char dummy; } *ID3D11DeviceContext;
typedef struct { char dummy; } *ID3D12Device;
typedef struct { char dummy; } *ID3D12CommandQueue;
typedef struct { char dummy; } *ID3D12CommandList;
typedef struct { char dummy; } *IDXGIFactory;
typedef struct { char dummy; } *IDXGIAdapter;
typedef struct { char dummy; } *ID3D11Resource;

enum D3D11_RESOURCE_DIMENSION {
    D3D11_RESOURCE_DIMENSION_UNKNOWN = 0,
    D3D11_RESOURCE_DIMENSION_BUFFER = 1,
    D3D11_RESOURCE_DIMENSION_TEXTURE1D = 2,
    D3D11_RESOURCE_DIMENSION_TEXTURE2D = 3,
    D3D11_RESOURCE_DIMENSION_TEXTURE3D = 4
};

enum D3D11_USAGE {
    D3D11_USAGE_DEFAULT = 0,
    D3D11_USAGE_IMMUTABLE = 1,
    D3D11_USAGE_DYNAMIC = 2,
    D3D11_USAGE_STAGING = 3
};

enum D3D12_COMMAND_LIST_TYPE {
    D3D12_COMMAND_LIST_TYPE_DIRECT = 0,
    D3D12_COMMAND_LIST_TYPE_BUNDLE = 1,
    D3D12_COMMAND_LIST_TYPE_COMPUTE = 2,
    D3D12_COMMAND_LIST_TYPE_COPY = 3,
    D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE = 4,
    D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS = 5
};

enum D3D12_COMMAND_QUEUE_FLAGS {
    D3D12_COMMAND_QUEUE_FLAG_NONE = 0,
    D3D12_COMMAND_QUEUE_FLAG_DISABLE_GPU_TIMEOUT = 0x1
};

struct GUID {
    unsigned long  Data1;
    unsigned short Data2;
    unsigned short Data3;
    unsigned char  Data4[8];
};
typedef const GUID& REFIID;

#define S_OK ((HRESULT)0)
#define E_FAIL ((HRESULT)0x80004005L)
#endif

class DXCompat {
public:
    struct ResourceInfo {
        D3D11_RESOURCE_DIMENSION dimension;
        D3D11_USAGE usage;
        UINT bind_flags;
        UINT cpu_access_flags;
        UINT misc_flags;
        UINT structure_byte_stride;
    };

    struct CommandQueue {
#ifdef _WIN32
        std::unique_ptr<ID3D12CommandQueue, decltype(&ID3D12CommandQueue::Release)> queue;
#else
        ID3D12CommandQueue* queue;
#endif
        D3D12_COMMAND_LIST_TYPE type;
        UINT node_mask;
        UINT priority;
        D3D12_COMMAND_QUEUE_FLAGS flags;
    };

    struct CommandListBatch {
        std::vector<ID3D12CommandList*> command_lists;
        UINT64 fence_value;
    };

    struct CommandListDependency {
        ID3D12CommandList* dependent_list;
        UINT64 fence_value;
    };

    struct CommandListOptimization {
        std::vector<ID3D12CommandList*> merged_lists;
        UINT64 optimization_timestamp;
    };

    DXCompat() = default;
    virtual ~DXCompat() = default;

    HRESULT initialize() {
#ifdef _WIN32
        // Windows-specific initialization
        return S_OK;
#else
        // Linux implementation
        return E_FAIL;
#endif
    }

private:
    std::unordered_map<ID3D11Resource*, ResourceInfo> resource_info_;
    std::unordered_map<D3D12_COMMAND_LIST_TYPE, CommandQueue> command_queues_;
    UINT64 current_fence_value_;
    std::unordered_map<ID3D12CommandList*, std::vector<CommandListDependency>> command_dependencies_;
    std::unordered_map<ID3D12CommandList*, CommandListOptimization> command_optimizations_;
};

} // namespace gpu
} // namespace anarchy 